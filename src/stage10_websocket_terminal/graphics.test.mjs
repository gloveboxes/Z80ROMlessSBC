import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import vm from 'node:vm';

const source = readFileSync(new URL('./Terminal/graphics.js', import.meta.url), 'utf8');

function graphics({ unavailable = false, shaderFailure = false } = {}) {
  const calls = [];
  const sent = [];
  const errors = [];
  const events = new Map();
  const gl = new Proxy({
    isContextLost: () => false,
    createShader: () => ({}), createProgram: () => ({}), createBuffer: () => ({}),
    getShaderParameter: () => !shaderFailure, getProgramParameter: () => true,
    getShaderInfoLog: () => 'test shader failure',
    getAttribLocation: (_, name) => name === 'p' ? 0 : 1,
  }, {
    get(target, name) {
      if (name in target) return target[name];
      if (String(name).toUpperCase() === name) return name;
      return (...args) => calls.push([name, ...args.map(a => a instanceof Float32Array ? [...a] : a)]);
    }
  });
  const panel = { hidden: true, scrollIntoView() {} };
  const canvas = {
    addEventListener: (name, handler) => events.set(name, handler),
    removeEventListener: name => events.delete(name),
    getContext: () => unavailable ? null : gl
  };
  const context = vm.createContext({ Float32Array, console });
  vm.runInContext(source + ';globalThis.Addon = Z80GraphicsAddon;', context);
  const addon = new context.Addon(panel, canvas, x => sent.push(x), x => errors.push(x));
  let handler, resetHandler, disposed = 0;
  addon.activate({ parser: {
    registerOscHandler(id, callback) {
      assert.equal(id, 777); handler = callback;
      return { dispose() { ++disposed; } };
    },
    registerEscHandler(id, callback) {
      assert.equal(id.final, 'c'); resetHandler = callback;
      return { dispose() { ++disposed; } };
    }
  } });
  return {
    addon, panel, canvas, gl, calls, sent, errors, events,
    command: command => handler(`Z80G;1;${command}`),
    raw: data => handler(data), reset: () => resetHandler(), disposed: () => disposed
  };
}

test('addon claims only its private protocol and negotiates a bounded canvas', () => {
  const g = graphics();
  assert.equal(g.raw('unrelated OSC payload'), false);
  assert.equal(g.command('open;320;240'), true);
  assert.deepEqual(g.sent, ['Z80G1\r']);
  assert.equal(g.panel.hidden, false);
  assert.equal(g.canvas.width, 320);
  assert.equal(g.canvas.height, 240);
  assert.deepEqual(g.errors, []);
});

test('rectangles, lines and retained sprites form one bounded GPU draw per present', () => {
  const g = graphics();
  g.command('open;320;240');
  g.command('clear;1');
  g.command('rect;0;0;320;240;15');
  g.command('line;0;0;319;239;14');
  g.command('sprite;31;48;8;11');
  g.command('move;31;272;232;1');
  g.command('present');
  assert.deepEqual(g.errors, []);
  assert.deepEqual(g.calls.filter(c => c[0] === 'drawArrays').at(-1).slice(1), ['TRIANGLES', 0, 18]);
  const vertices = g.calls.filter(c => c[0] === 'bufferData').at(-1)[2];
  assert.equal(vertices.length, 90);
  assert.deepEqual(vertices.slice(0, 5), [-1, 1, 1, 1, 1]);
  g.command('clear;0');
  g.command('present');
  assert.equal(g.calls.filter(c => c[0] === 'drawArrays').at(-1)[3], 6, 'clear retains sprites');
  g.command('move;31;0;0;0');
  g.command('present');
  assert.equal(g.calls.filter(c => c[0] === 'drawArrays').at(-1)[3], 0);
});

test('invalid inputs explicitly fail closed without unbounded scene growth', () => {
  for (const command of [
    'rect;0;0;0;1;1', 'rect;319;239;2;2;1', 'rect;-1;0;1;1;1',
    'rect;0;0;1;1;16', 'line;0;0;320;240;1', 'clear;01',
    'clear;1;2', 'clear;NaN', 'clear;1.5', 'clear;99999', 'unknown',
    'sprite;32;8;8;1', 'sprite;0;321;8;1', 'move;0;0;0;1',
    'present;0', `rect;${'1'.repeat(129)}`,
  ]) {
    const g = graphics();
    g.command('open;320;240');
    g.command(command);
    assert.equal(g.errors.length, 1, command);
    assert.equal(g.panel.hidden, true, command);
    assert.equal(g.addon.shapes.length, 0);
    assert.equal(g.addon.sprites.size, 0);
  }
  const g = graphics();
  g.command('open;320;240');
  for (let i = 0; i < 256; ++i) g.command('rect;0;0;1;1;1');
  assert.equal(g.addon.shapes.length, 256);
  g.command('rect;0;0;1;1;1');
  assert.match(g.errors[0], /256 shapes/);
});

test('open failure replies, unsupported versions and shader failures are visible', () => {
  for (const options of [{ unavailable: true }, { shaderFailure: true }, {}]) {
    const g = graphics(options);
    g.command(options.unavailable || options.shaderFailure ? 'open;320;240' : 'open;641;480');
    assert.deepEqual(g.sent, ['Z80G0\r']);
    assert.equal(g.errors.length, 1);
    assert.equal(g.panel.hidden, true);
  }
  const g = graphics();
  g.raw('Z80G;2;open;320;240');
  assert.deepEqual(g.sent, ['Z80G0\r']);
  assert.match(g.errors[0], /version/);
});

test('close, terminal reset, context loss and disposal release graphics resources', () => {
  const g = graphics();
  for (const action of [
    () => g.command('close'), () => assert.equal(g.reset(), false),
    () => g.events.get('webglcontextlost')({ preventDefault() {} })
  ]) {
    g.command('open;320;240');
    g.command('sprite;0;8;8;14');
    action();
    assert.equal(g.panel.hidden, true);
    assert.equal(g.addon.sprites.size, 0);
    assert.equal(g.addon.gl, null);
  }
  assert.match(g.errors[0], /context lost/);
  g.command('open;320;240');
  g.addon.dispose();
  assert.equal(g.disposed(), 2);
  assert.equal(g.events.size, 0);
  assert.equal(g.panel.hidden, true);
  assert.equal(g.calls.filter(c => c[0] === 'deleteProgram').length, 4);
});

test('disconnect and stop discard queued commands without reopening or spurious errors', () => {
  const g = graphics();
  g.command('open;320;240');
  g.addon.cancel();
  g.command('open;320;240');
  g.command('move;0;0;0;1');
  assert.equal(g.panel.hidden, true);
  assert.deepEqual(g.sent, ['Z80G1\r'], 'queued opens cannot reply into a replacement session');
  assert.deepEqual(g.errors, []);
  g.addon.reset();
  g.command('open;320;240');
  assert.equal(g.panel.hidden, false);
  g.addon.cancel(true);
  g.command('move;0;0;0;1');
  g.command('present');
  assert.equal(g.panel.hidden, true);
  assert.deepEqual(g.errors, []);
  g.command('close');
  g.command('open;320;240');
  assert.equal(g.panel.hidden, false, 'a new application can open after stopping');
});
