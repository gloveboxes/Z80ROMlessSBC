import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import vm from 'node:vm';
import { gunzipSync } from 'node:zlib';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

const html = readFileSync(new URL('./Terminal/index.html', import.meta.url), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1]
  .replace('        initialize,', '        state, elements, setupTerminalEventHandlers, setupMonitorButton, setupRebootButton, setupWebSocketEventHandlers, getWebSocketUrl, handleProtocolFrame, resetProtocol, closeConnection, scheduleReconnect, initialize,');

function frame(type, sequence, payload = []) {
  const bytes = new Uint8Array(5 + payload.length);
  bytes[0] = type;
  new DataView(bytes.buffer).setUint32(1, sequence, true);
  bytes.set(payload, 5);
  return bytes;
}

function terminal(location = { hostname: '192.0.2.1', port: '8088', search: '' }) {
  const sent = [];
  let keydown;
  let onData;
  const writeCallbacks = [];
  let deferWrite = false;
  const written = [];
  const timers = new Map();
  let nextTimer = 1;
  const errors = [];
  const term = {
    focus() {},
    onData(handler) { onData = handler; return { dispose() {} }; },
    attachCustomKeyEventHandler(handler) { keydown = handler; },
    write(data, callback) {
      written.push([...data]);
      if (deferWrite) writeCallbacks.push(callback);
      else callback();
    },
  };
  const context = vm.createContext({
    navigator: { onLine: true }, TextDecoder, TextEncoder, Uint8Array, DataView,
    WebSocket: { OPEN: 1 }, console, URLSearchParams,
    document: { getElementById() { return null; } },
    setTimeout(callback) { const id = nextTimer++; timers.set(id, callback); return id; },
    clearTimeout(id) { timers.delete(id); },
    window: { location, confirm() { return true; } },
  });
  vm.runInContext(script + ';globalThis.api = AltairTerminal;', context);
  const api = context.api;
  api.state.connected = true;
  api.state.ws = { readyState: 1, send(data) { sent.push([...data]); } };
  api.state.term = term;
  api.elements.error = { textContent: '' };
  api.setupTerminalEventHandlers();
  // Use the actual error display, capturing its console notification.
  context.console = { ...console, error(...args) { errors.push(args.join(' ')); } };
  const receive = bytes => api.handleProtocolFrame(bytes.buffer, api.state.ws);
  receive(frame(5, 4));
  sent.length = 0;
  return {
    api, context, sent, written, errors, timers, receive, type: data => onData(data),
    acknowledge() { receive(frame(4, api.state.inputSequence)); },
    retry() {
      const callback = timers.get(api.state.inputTimer);
      timers.delete(api.state.inputTimer);
      assert.ok(callback);
      callback();
    },
    deferOutput() { deferWrite = true; },
    finishOutput() { writeCallbacks.shift()(); },
    press(key, keyCode, modifiers = {}) {
      return keydown({ type: 'keydown', key, keyCode, ...modifiers });
    },
  };
}

test('CP/M key mappings match the tested USB terminal; Ctrl-M is CR', () => {
  const input = terminal();
  for (const [key, code, expected] of [
    ['ArrowUp', 38, 5], ['ArrowDown', 40, 24], ['ArrowLeft', 37, 19],
    ['ArrowRight', 39, 4], ['Insert', 45, 15], ['Delete', 46, 7],
    ['Backspace', 8, 8], ['PageUp', 33, 18], ['PageDown', 34, 22],
  ]) {
    assert.equal(input.press(key, code), false);
    assert.deepEqual(input.sent.at(-1).slice(5), [expected]);
    input.acknowledge();
  }
  assert.equal(input.press('m', 77, { ctrlKey: true }), false);
  assert.deepEqual(input.sent.at(-1).slice(5), [13]);
  input.acknowledge();
  input.api.elements.monitorBtn = {};
  input.api.setupMonitorButton();
  input.api.elements.monitorBtn.onclick();
  assert.deepEqual(input.sent.at(-1).slice(5), [13]);
});

test('xterm input forwards bytes, CP/M controls and pastes; disconnected input is suppressed', () => {
  const input = terminal();
  assert.equal(input.press('c', 67, { ctrlKey: true }), true);
  input.type('\x03\x1aDIR\r');
  assert.deepEqual(input.sent.at(-1), [2, 1, 0, 0, 0, 3, 26, 68, 73, 82, 13]);
  assert.equal(input.api.getWebSocketUrl(), 'ws://192.0.2.1:8088/');
  input.api.state.connected = false;
  const count = input.sent.length;
  input.type('not sent');
  assert.equal(input.sent.length, count);
});

test('Pico WebSocket always uses port 8088 and root, including separately hosted pages', () => {
  for (const port of ['', '80', '8088', '5500']) {
    const input = terminal({ hostname: '192.0.2.1', port, search: '' });
    assert.equal(input.api.getWebSocketUrl(), 'ws://192.0.2.1:8088/');
  }
  const remote = terminal({
    hostname: 'localhost', port: '5500', search: '?altair=192.0.2.50',
  });
  assert.equal(remote.api.getWebSocketUrl(), 'ws://192.0.2.50:8088/');
  const local = terminal({ hostname: '', port: '', search: '' });
  assert.equal(local.api.getWebSocketUrl(), 'ws://localhost:8088/');
});

test('ESP32 terminal layout keeps 80 columns and a separate scrollbar strip', () => {
  assert.match(html, /XTERM_COLS:\s*80,/);
  assert.match(html, /XTERM_ROWS:\s*30,/);
  const container = html.match(/#terminal\s*\{([^}]+)\}/)[1];
  assert.match(container, /max-width:\s*calc\(100vw - 40px\)/);
  assert.match(container, /padding:\s*0 12px 0 0/);
  assert.match(container, /overflow:\s*auto/);
  const viewport = html.match(/\.xterm \.xterm-viewport\s*\{([^}]+)\}/)[1];
  assert.match(viewport, /right:\s*-12px/);
  assert.match(viewport, /scrollbar-width:\s*thin/);
  assert.match(html, /<button id="monitorBtn"[^>]*>Enter<\/button>/);
  assert.match(html, /id="terminal_error" role="alert"/);
});

test('1024-byte paste waits for queue credits and retries without advancing', () => {
  const input = terminal();
  const paste = Array.from({ length: 1024 }, (_, i) => i & 127);
  input.type(Uint8Array.from(paste));
  assert.equal(input.sent.length, 1);
  assert.equal(input.sent[0].length, 69);
  const first = input.sent[0];
  input.retry();
  assert.deepEqual(input.sent[1], first);
  input.receive(frame(4, 99));
  assert.equal(input.api.state.inputSequence, 1);
  const delivered = [];
  for (let i = 0; i < 16; ++i) {
    const next = input.sent.at(-1);
    assert.equal(new DataView(Uint8Array.from(next).buffer).getUint32(1, true), i + 1);
    delivered.push(...next.slice(5));
    input.acknowledge();
  }
  assert.deepEqual(delivered, paste);
  assert.equal(input.api.state.inputBuffer.length, 0);
  assert.equal(input.timers.size, 0);
  input.receive(frame(4, 16));
  assert.equal(input.api.state.inputSequence, 17);
});

test('output is rendered once; duplicates are ACKed only after xterm consumes it', () => {
  const input = terminal();
  input.deferOutput();
  const output = frame(1, 1, [27, 91, 72, 65]);
  input.receive(output);
  input.receive(output);
  assert.deepEqual(input.written, [[27, 91, 72, 65]]);
  assert.equal(input.sent.length, 0);
  input.finishOutput();
  assert.deepEqual(input.sent.at(-1), [3, 1, 0, 0, 0]);
  input.receive(output);
  assert.equal(input.written.length, 1);
  assert.deepEqual(input.sent.at(-1), [3, 1, 0, 0, 0]);
  assert.throws(() => input.receive(frame(1, 4, [66])), /Out-of-order/);
});

test('input limit rejects the whole new paste and reconnect never replays pending input', () => {
  const input = terminal();
  input.type('X'.repeat(65536));
  assert.equal(input.api.state.inputBuffer.length, 65536);
  input.type('rejected');
  assert.equal(input.api.state.inputBuffer.length, 65536);
  assert.ok(input.errors.some(error => error.includes('queue full')));
  assert.match(input.api.elements.error.textContent, /queue full/);
  input.api.resetProtocol(true);
  assert.ok(input.errors.some(error => error.includes('unconfirmed')));
  assert.match(input.api.elements.error.textContent, /unconfirmed/);
  assert.equal(input.api.state.inputBuffer.length, 0);
  assert.equal(input.timers.size, 0);
  input.type('not ready');
  assert.equal(input.api.state.inputBuffer.length, 0);
  input.receive(frame(5, 4));
  input.type('N');
  assert.deepEqual(input.sent.at(-1), [2, 1, 0, 0, 0, 78]);
});

test('an old xterm callback cannot acknowledge bytes on a replacement connection', () => {
  const input = terminal();
  input.deferOutput();
  input.receive(frame(1, 1, [65]));
  input.api.resetProtocol();
  input.api.state.ws = { readyState: 1, send(data) { input.sent.push([...data]); } };
  input.receive(frame(5, 4));
  const count = input.sent.length;
  input.finishOutput();
  assert.equal(input.sent.length, count);
  assert.equal(input.api.state.outputSequence, 1);
});

test('protocol readiness is required and sequence counters wrap at uint32', () => {
  const input = terminal();
  input.api.state.protocolReady = false;
  input.type('A');
  assert.equal(input.sent.length, 0);
  assert.match(input.api.elements.error.textContent, /handshake/);
  input.receive(frame(5, 4));
  assert.deepEqual(input.sent.shift(), [6, 4, 0, 0, 0]);

  input.api.state.inputSequence = 0xffffffff;
  input.type('AB'.repeat(64));
  assert.deepEqual(input.sent[0].slice(1, 5), [255, 255, 255, 255]);
  input.receive(frame(4, 0xffffffff));
  assert.deepEqual(input.sent[1].slice(1, 5), [0, 0, 0, 0]);
  input.receive(frame(4, 0));
  assert.equal(input.api.state.inputBuffer.length, 0);
  assert.equal(input.timers.size, 0);

  input.api.state.outputSequence = 0xffffffff;
  input.api.state.outputReceivedSequence = 0xffffffff;
  input.receive(frame(1, 0xffffffff, [65]));
  input.receive(frame(1, 0xffffffff, [65]));
  input.receive(frame(1, 0, [66]));
  assert.deepEqual(input.written, [[65], [66]]);
  assert.equal(input.api.state.outputSequence, 1);
});

test('four output frames enter xterm before any consumption ACK; duplicates do not render', () => {
  const input = terminal();
  input.deferOutput();
  for (let sequence = 1; sequence <= 4; ++sequence) {
    const payload = Array(512).fill(64 + sequence);
    input.receive(frame(1, sequence, payload));
    input.receive(frame(1, sequence, payload));
    assert.deepEqual(input.written[sequence - 1], payload);
  }
  assert.equal(input.written.length, 4);
  assert.equal(input.sent.length, 0);
  assert.equal(input.api.state.outputPending, 4);
  assert.throws(() => input.receive(frame(1, 5, [69])), /window exceeded/);
  for (let sequence = 1; sequence <= 4; ++sequence) {
    input.finishOutput();
    assert.deepEqual(input.sent.at(-1), [3, sequence, 0, 0, 0]);
  }
  input.receive(frame(1, 1, [65]));
  assert.deepEqual(input.sent.at(-1), [3, 4, 0, 0, 0]);
  assert.equal(input.written.length, 4);
  assert.equal(input.api.state.outputPending, 0);
});

test('512-byte output limit agrees with firmware and rejects oversized or empty payloads', () => {
  const header = readFileSync(new URL('./web_terminal.h', import.meta.url), 'utf8');
  assert.match(header, /Z80_WEB_TERMINAL_FRAME_BYTES\s*=\s*512,/);
  assert.match(header, /Z80_WEB_TERMINAL_OUTPUT_WINDOW\s*=\s*4,/);
  assert.match(html, /OUTPUT_FRAME_BYTES:\s*512,/);
  assert.match(html, /OUTPUT_WINDOW:\s*4,/);
  const input = terminal();
  input.receive(frame(1, 1, Array(512).fill(65)));
  assert.equal(input.written[0].length, 512);
  assert.throws(() => input.receive(frame(1, 2, Array(513).fill(66))), /Unsupported/);
  assert.throws(() => input.receive(frame(1, 2)), /Unsupported/);
  assert.equal(input.written.length, 1);
});

test('protocol reset invalidates callbacks even if the socket object is unchanged', () => {
  const input = terminal();
  input.deferOutput();
  input.receive(frame(1, 1, [65]));
  input.api.resetProtocol();
  input.receive(frame(5, 4));
  const count = input.sent.length;
  input.finishOutput();
  assert.equal(input.sent.length, count);
  assert.equal(input.api.state.outputPending, 0);
  assert.equal(input.api.state.outputSequence, 1);
});

test('version 1/2/3 handshakes are rejected and reconnect attempts really stop at five', () => {
  const input = terminal();
  assert.throws(() => input.receive(frame(5, 1)), /Unsupported/);
  assert.throws(() => input.receive(frame(5, 2)), /Unsupported/);
  assert.throws(() => input.receive(frame(5, 3)), /Unsupported/);
  input.api.state.connected = false;
  for (let attempt = 0; attempt < 7; ++attempt) {
    input.api.state.reconnectTimer = null;
    input.api.scheduleReconnect();
  }
  assert.equal(input.api.state.reconnectAttempts, 5);
  assert.equal(input.timers.size, 5);
});

test('embedded gzip is deterministic and decompresses to the exact copied terminal page', () => {
  const temporary = mkdtempSync(join(tmpdir(), 'z80-terminal-'));
  try {
    const output = join(temporary, 'static_html_hex.h');
    const tool = fileURLToPath(new URL('./embed_html.py', import.meta.url));
    const source = fileURLToPath(new URL('./Terminal/index.html', import.meta.url));
    const run = () => {
      const result = spawnSync('python3', [tool, source, output], { encoding: 'utf8' });
      assert.equal(result.status, 0, result.stderr);
      return readFileSync(output, 'utf8');
    };
    const header = run();
    assert.equal(header, run());
    const bytes = Buffer.from([...header.matchAll(/0x([0-9a-f]{2})/g)]
      .map(match => parseInt(match[1], 16)));
    assert.equal(gunzipSync(bytes).toString('utf8'), html);
    assert.match(header, new RegExp(`static_html_gz_len = ${bytes.length}u`));
  } finally {
    rmSync(temporary, { recursive: true });
  }
});


test('reboot bypasses terminal input, requires confirmation, and suppresses duplicate clicks', () => {
  const input = terminal();
  input.api.elements.rebootBtn = {};
  input.api.elements.rebootStatus = { textContent: '' };
  input.api.setupRebootButton();
  input.context.window.confirm = () => false;
  input.api.elements.rebootBtn.onclick();
  assert.equal(input.sent.length, 0);
  input.context.window.confirm = () => true;
  input.type('busy'); // Leave ordinary input unacknowledged.
  input.sent.length = 0;
  input.api.elements.rebootBtn.onclick();
  assert.deepEqual(input.sent, [[7, 0, 0, 0, 0]]);
  assert.equal(input.api.elements.rebootBtn.disabled, true);
  input.api.elements.rebootBtn.onclick();
  input.type('ignored');
  assert.equal(input.sent.length, 1);
  input.receive(frame(8, 2));
  assert.match(input.api.elements.rebootStatus.textContent, /Flushing/);
  input.receive(frame(8, 3));
  assert.equal(input.api.elements.rebootStatus.textContent, 'Flushing disks -> Restarting...');
  input.receive(frame(8, 3));
  assert.equal(input.api.elements.rebootStatus.textContent, 'Flushing disks -> Restarting...');
  input.receive(frame(8, 7)); // A pre-reboot ready report cannot complete this request.
  assert.equal(input.api.state.rebootPending, true);
  input.api.state.rebootDisconnected = true;
  input.api.resetProtocol();
  input.receive(frame(5, 4));
  assert.equal(input.api.state.rebootPending, false);
  assert.equal(input.api.elements.rebootStatus.textContent, '');
  input.receive(frame(8, 7));
  assert.equal(input.api.state.rebootPending, false);
  assert.equal(input.api.elements.rebootBtn.disabled, false);
  assert.equal(input.api.elements.rebootStatus.textContent, '');
  assert.equal(input.api.state.rebootTimer, null);
});

test('reboot reports storage failure, timeout and rejection without forcing reset', () => {
  for (const [status, message] of [[4, /storage error/], [5, /timed out/], [6, /rejected/]]) {
    const input = terminal();
    input.api.elements.rebootBtn = {};
    input.api.elements.rebootStatus = { textContent: '' };
    input.api.setupRebootButton();
    input.api.elements.rebootBtn.onclick();
    input.receive(frame(8, status));
    assert.equal(input.api.state.rebootPending, false);
    assert.match(input.api.elements.rebootStatus.textContent, message);
    assert.equal(input.api.state.rebootTimer, null);
    assert.equal(input.sent.filter(f => f[0] === 7).length, 1);
  }
});


test('only a requested reboot automatically reconnects after socket closure', () => {
  for (const reboot of [false, true]) {
    const input = terminal();
    input.api.elements.rebootBtn = {};
    input.api.elements.rebootStatus = { textContent: '' };
    input.api.setupRebootButton();
    input.api.setupWebSocketEventHandlers('192.0.2.1');
    if (reboot) input.api.elements.rebootBtn.onclick();
    input.api.state.ws.onclose({ code: 1006, reason: '' });
    if (reboot) {
      assert.equal(input.api.elements.rebootStatus.textContent, 'Reconnecting...');
    }
    assert.equal(input.api.state.rebootDisconnected, reboot);
    assert.equal(input.api.state.reconnectTimer !== null, reboot);
    assert.equal(input.api.state.connected, false);
    assert.equal(input.api.elements.rebootBtn.disabled, true);
  }
});

test('an unconfirmed reboot times out without resending the request', () => {
  const input = terminal();
  input.api.elements.rebootBtn = {};
  input.api.elements.rebootStatus = { textContent: '' };
  input.api.setupRebootButton();
  input.api.elements.rebootBtn.onclick();
  const callback = input.timers.get(input.api.state.rebootTimer);
  callback();
  assert.equal(input.api.state.rebootPending, false);
  assert.match(input.api.elements.rebootStatus.textContent, /not confirmed/);
  assert.equal(input.sent.filter(f => f[0] === 7).length, 1);
});


test('reconnect clears reboot progress without claiming the reboot was confirmed', () => {
  const input = terminal();
  input.api.elements.rebootBtn = {};
  input.api.elements.rebootStatus = { textContent: '' };
  input.api.setupRebootButton();
  input.api.elements.rebootBtn.onclick();
  input.api.state.rebootDisconnected = true;
  input.api.resetProtocol();
  input.receive(frame(5, 4));
  input.receive(frame(8, 7));
  assert.equal(input.api.elements.rebootStatus.textContent, '');
});
