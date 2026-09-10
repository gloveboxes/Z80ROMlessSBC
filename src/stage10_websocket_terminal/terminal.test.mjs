import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import vm from 'node:vm';

const html = readFileSync(new URL('./terminal.html', import.meta.url), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];

function terminal() {
  const sent = [];
  let keydown;
  let socket;
  const screen = {
    focus() {},
    addEventListener(name, handler) {
      if (name === 'keydown') keydown = handler;
    },
  };
  class WebSocketMock {
    static OPEN = 1;
    readyState = 1;
    constructor() { socket = this; }
    send(data) { sent.push([...data]); }
  }
  vm.runInNewContext(script, {
    document: { querySelector: selector => selector === '#screen' ? screen : {} },
    TextDecoder, TextEncoder, WebSocket: WebSocketMock,
    location: { host: 'test.invalid' }, setTimeout() {},
  });
  return {
    sent, socket,
    press(key, modifiers = {}) {
      let prevented = false;
      keydown({ key, ...modifiers, preventDefault() { prevented = true; } });
      return prevented;
    },
  };
}

test('ASCII control letters include CP/M interrupt and EOF', () => {
  const input = terminal();
  for (let code = 65; code <= 90; ++code) {
    for (const key of [String.fromCharCode(code), String.fromCharCode(code + 32)]) {
      assert.equal(input.press(key, { ctrlKey: true }), true);
      assert.deepEqual(input.sent.at(-1), [code & 31]);
    }
  }
});

test('control punctuation and NUL', () => {
  const input = terminal();
  for (const [key, expected] of [['@', 0], [' ', 0], ['[', 27], ['\\', 28], [']', 29], ['^', 30], ['_', 31], ['?', 127]]) {
    assert.equal(input.press(key, { ctrlKey: true }), true);
    assert.deepEqual(input.sent.at(-1), [expected]);
  }
});

test('ordinary input and terminal special keys remain unchanged', () => {
  const input = terminal();
  for (const [key, expected] of [['c', [99]], ['Enter', [13]], ['Backspace', [8]], ['Tab', [9]], ['ArrowUp', [27, 91, 65]]]) {
    assert.equal(input.press(key), true);
    assert.deepEqual(input.sent.at(-1), expected);
  }
});

test('browser shortcuts, composition and disconnected input are not sent', () => {
  const input = terminal();
  for (const modifiers of [{ metaKey: true }, { altKey: true }, { isComposing: true }])
    assert.equal(input.press('c', modifiers), false);
  assert.equal(input.press('ArrowUp', { ctrlKey: true }), false);
  input.socket.readyState = 3;
  assert.equal(input.press('c', { ctrlKey: true }), false);
  assert.deepEqual(input.sent, []);
});