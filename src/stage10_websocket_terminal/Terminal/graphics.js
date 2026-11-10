/* Private OSC 777 graphics peripheral. No application-supplied JavaScript. */
class Z80GraphicsAddon {
  constructor(panel, canvas, sendInput, reportError) {
    this.panel = panel;
    this.canvas = canvas;
    this.sendInput = sendInput;
    this.reportError = reportError;
    this.palette = [
      0x000000, 0x0000aa, 0x00aa00, 0x00aaaa,
      0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa,
      0x555555, 0x5555ff, 0x55ff55, 0x55ffff,
      0xff5555, 0xff55ff, 0xffff55, 0xffffff
    ];
    this.lost = event => {
      event.preventDefault();
      this.cancel(true);
      this.reportError('Graphics WebGL context lost. Press Q to stop the game, then restart it.');
    };
    canvas.addEventListener('webglcontextlost', this.lost);
    this.reset();
  }

  activate(term) {
    this.handler = term.parser.registerOscHandler(777, data => this.command(data));
    this.resetHandler = term.parser.registerEscHandler({ final: 'c' }, () => {
      this.reset();
      return false; // Let xterm perform its normal terminal reset as well.
    });
  }

  reset() {
    this.cancelled = false;
    this.allowOpen = false;
    if (this.gl) {
      this.gl.deleteBuffer(this.buffer);
      this.gl.deleteProgram(this.program);
    }
    this.gl = null;
    this.buffer = null;
    this.program = null;
    this.vertices = null;
    this.shapes = [];
    this.sprites = new Map();
    this.width = 0;
    this.height = 0;
    this.background = 0;
    this.panel.hidden = true;
  }

  cancel(allowOpen = false) {
    this.reset();
    this.cancelled = true;
    this.allowOpen = allowOpen;
  }

  dispose() {
    this.handler?.dispose();
    this.resetHandler?.dispose();
    this.canvas.removeEventListener('webglcontextlost', this.lost);
    this.reset();
  }

  open(width, height) {
    this.reset();
    this.canvas.width = width;
    this.canvas.height = height;
    const gl = this.canvas.getContext('webgl', { alpha: false, antialias: false });
    if (!gl || gl.isContextLost()) throw new Error('WebGL graphics is unavailable');
    this.gl = gl;
    const shaders = [];
    try {
      this.program = gl.createProgram();
      if (!this.program) throw new Error('Cannot allocate graphics program');
      for (const [type, source] of [
        [gl.VERTEX_SHADER, 'attribute vec2 p; attribute vec3 c; varying vec3 colour; void main(){gl_Position=vec4(p,0.,1.);colour=c;}'],
        [gl.FRAGMENT_SHADER, 'precision mediump float; varying vec3 colour; void main(){gl_FragColor=vec4(colour,1.);}']
      ]) {
        const shader = gl.createShader(type);
        if (!shader) throw new Error('Cannot allocate graphics shader');
        shaders.push(shader);
        gl.shaderSource(shader, source);
        gl.compileShader(shader);
        if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS))
          throw new Error(`Graphics shader: ${gl.getShaderInfoLog(shader)}`);
        gl.attachShader(this.program, shader);
      }
      gl.linkProgram(this.program);
      if (!gl.getProgramParameter(this.program, gl.LINK_STATUS))
        throw new Error(`Graphics link: ${gl.getProgramInfoLog(this.program)}`);
      this.buffer = gl.createBuffer();
      if (!this.buffer) throw new Error('Cannot allocate graphics buffer');
      this.vertices = new Float32Array((256 + 32) * 6 * 5);
      this.width = width;
      this.height = height;
      this.background = 0;
      this.panel.hidden = false;
      this.present();
      this.panel.scrollIntoView({ block: 'nearest' });
    } finally {
      for (const shader of shaders) gl.deleteShader(shader);
    }
  }

  present() {
    const gl = this.gl;
    let used = 0;
    const vertex = (x, y, colour) => {
      this.vertices[used++] = x * 2 / this.width - 1;
      this.vertices[used++] = 1 - y * 2 / this.height;
      this.vertices[used++] = ((colour >> 16) & 255) / 255;
      this.vertices[used++] = ((colour >> 8) & 255) / 255;
      this.vertices[used++] = (colour & 255) / 255;
    };
    const quad = (x1, y1, x2, y2, x3, y3, x4, y4, colour) => {
      vertex(x1, y1, colour); vertex(x2, y2, colour); vertex(x3, y3, colour);
      vertex(x1, y1, colour); vertex(x3, y3, colour); vertex(x4, y4, colour);
    };
    for (const shape of this.shapes) {
      const [kind, x, y, a, b, ink] = shape;
      const colour = this.palette[ink];
      if (kind === 'rect') {
        quad(x, y, x + a, y, x + a, y + b, x, y + b, colour);
      } else {
        const length = Math.hypot(a - x, b - y);
        if (!length) {
          quad(x, y, x + 1, y, x + 1, y + 1, x, y + 1, colour);
          continue;
        }
        const dx = -(b - y) / length / 2;
        const dy = (a - x) / length / 2;
        quad(x + dx, y + dy, a + dx, b + dy,
             a - dx, b - dy, x - dx, y - dy, colour);
      }
    }
    for (const sprite of this.sprites.values()) {
      if (sprite.visible)
        quad(sprite.x, sprite.y, sprite.x + sprite.w, sprite.y,
             sprite.x + sprite.w, sprite.y + sprite.h,
             sprite.x, sprite.y + sprite.h, this.palette[sprite.ink]);
    }
    const bg = this.palette[this.background];
    gl.viewport(0, 0, this.width, this.height);
    gl.clearColor((bg >> 16) / 255, ((bg >> 8) & 255) / 255, (bg & 255) / 255, 1);
    gl.clear(gl.COLOR_BUFFER_BIT);
    gl.useProgram(this.program);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.buffer);
    gl.bufferData(gl.ARRAY_BUFFER, this.vertices.subarray(0, used), gl.DYNAMIC_DRAW);
    for (const [name, size, offset] of [['p', 2, 0], ['c', 3, 8]]) {
      const location = gl.getAttribLocation(this.program, name);
      gl.enableVertexAttribArray(location);
      gl.vertexAttribPointer(location, size, gl.FLOAT, false, 20, offset);
    }
    gl.drawArrays(gl.TRIANGLES, 0, used / 5);
  }

  command(data) {
    if (!data.startsWith('Z80G;')) return false;
    if (this.cancelled) {
      const op = data.split(';', 3)[2];
      if (!this.allowOpen || (op !== 'open' && op !== 'close')) return true;
    }
    let opening = false;
    try {
      if (data.length > 128) throw new Error('Command exceeds 128 characters');
      const fields = data.split(';');
      opening = fields[2] === 'open';
      if (fields[1] !== '1') throw new Error('Unsupported graphics protocol version');
      const op = fields[2];
      const args = fields.slice(3);
      const numbers = count => {
        if (args.length !== count || args.some(n => !/^(0|[1-9][0-9]{0,3})$/.test(n)))
          throw new Error('Invalid graphics command arguments');
        return args.map(Number);
      };
      const require = (condition, message) => {
        if (!condition) throw new Error(message);
      };
      const point = (x, y) => x < this.width && y < this.height;
      const box = (x, y, w, h) => w > 0 && h > 0 && point(x, y) &&
        x + w <= this.width && y + h <= this.height;
      if (op === 'open') {
        const [w, h] = numbers(2);
        require(w >= 64 && w <= 640 && h >= 64 && h <= 480, 'Invalid canvas dimensions');
        this.open(w, h);
        this.sendInput('Z80G1\r');
      } else if (op === 'close') {
        numbers(0);
        this.reset();
      } else {
        require(this.gl && !this.gl.isContextLost(), 'Open the graphics canvas first');
        if (op === 'clear') {
          const [ink] = numbers(1);
          require(ink < 16, 'Invalid palette index');
          this.background = ink;
          this.shapes = [];
        } else if (op === 'rect' || op === 'line') {
          const [x, y, a, b, ink] = numbers(5);
          require(ink < 16 && (op === 'rect' ? box(x, y, a, b) : point(x, y) && point(a, b)),
                  'Invalid geometry or palette index');
          require(this.shapes.length < 256, 'Scene exceeds 256 shapes');
          this.shapes.push([op, x, y, a, b, ink]);
        } else if (op === 'sprite') {
          const [id, w, h, ink] = numbers(4);
          require(id < 32 && ink < 16 && box(0, 0, w, h), 'Invalid sprite definition');
          this.sprites.set(id, { w, h, ink, x: 0, y: 0, visible: false });
        } else if (op === 'move') {
          const [id, x, y, visible] = numbers(4);
          const sprite = this.sprites.get(id);
          require(sprite && visible <= 1 && box(x, y, sprite.w, sprite.h), 'Invalid sprite move');
          Object.assign(sprite, { x, y, visible: visible === 1 });
        } else if (op === 'present') {
          numbers(0);
          this.present();
        } else {
          throw new Error('Unknown graphics command');
        }
      }
    } catch (error) {
      this.cancel(true);
      this.reportError(`Z80 graphics: ${error.message}. Press Q to stop the application.`);
      if (opening) this.sendInput('Z80G0\r');
    }
    return true;
  }
}
