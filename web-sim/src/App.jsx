import { useEffect, useRef, useState } from 'react';

const KEY_CODES = {
  ZERO: 0,
  COMA: 1,
  PI: 2,
  NOT2: 3,
  ENTER: 4,
  ONE: 5,
  TWO: 6,
  THREE: 7,
  PLUS: 8,
  TIMES: 9,
  FOUR: 10,
  FIVE: 11,
  SIX: 12,
  MINUS: 13,
  DIVIDE: 14,
  SEVEN: 15,
  EIGHT: 16,
  NINE: 17,
  OPENING_PARENTHESIS: 18,
  CLOSING_PARENTHESIS: 19,
  COS: 20,
  SIN: 21,
  TAN: 22,
  SQRT: 23,
  POW: 24,
  LN: 25,
  BACK: 26,
  E: 27,
  DOWN: 28,
  RIGHT: 29,
  X: 30,
  TOOLS: 31,
  EQUAL: 32,
  LEFT: 33,
  OK: 34,
  PASS3: 35,
  SECOND: 36,
  PASS4: 37,
  UP: 38,
  END_KEYS: 39,
  FACT: 40,
  COSH: 41,
  SINH: 42,
  TANH: 43,
  ACOSH: 44,
  ASINH: 45,
  ATANH: 46
};

const KEYBOARD_MAP = {
  '0': KEY_CODES.ZERO,
  '1': KEY_CODES.ONE,
  '2': KEY_CODES.TWO,
  '3': KEY_CODES.THREE,
  '4': KEY_CODES.FOUR,
  '5': KEY_CODES.FIVE,
  '6': KEY_CODES.SIX,
  '7': KEY_CODES.SEVEN,
  '8': KEY_CODES.EIGHT,
  '9': KEY_CODES.NINE,
  ',': KEY_CODES.COMA,
  '.': KEY_CODES.COMA,
  '+': KEY_CODES.PLUS,
  '-': KEY_CODES.MINUS,
  '*': KEY_CODES.TIMES,
  '/': KEY_CODES.DIVIDE,
  '^': KEY_CODES.POW,
  '(': KEY_CODES.OPENING_PARENTHESIS,
  ')': KEY_CODES.CLOSING_PARENTHESIS,
  '=': KEY_CODES.EQUAL,
  Enter: KEY_CODES.OK,
  Backspace: KEY_CODES.BACK,
  ArrowUp: KEY_CODES.UP,
  ArrowDown: KEY_CODES.DOWN,
  ArrowLeft: KEY_CODES.LEFT,
  ArrowRight: KEY_CODES.RIGHT,
  x: KEY_CODES.X,
  X: KEY_CODES.X,
  p: KEY_CODES.PI,
  P: KEY_CODES.PI,
  e: KEY_CODES.E,
  E: KEY_CODES.E,
  s: KEY_CODES.SIN,
  S: KEY_CODES.SIN,
  c: KEY_CODES.COS,
  C: KEY_CODES.COS,
  t: KEY_CODES.TAN,
  T: KEY_CODES.TAN,
  l: KEY_CODES.LN,
  L: KEY_CODES.LN,
  r: KEY_CODES.SQRT,
  R: KEY_CODES.SQRT,
  Shift: KEY_CODES.SECOND
};


const MAIN_KEYS = [
  [
    { label: 'X',     code: KEY_CODES.X },
    { label: 'Tools', code: KEY_CODES.TOOLS },
    { label: 'Home',  code: KEY_CODES.HOME },
    { label: 'Const', code: KEY_CODES.CONST },
    { label: '←',    code: KEY_CODES.BACK }
  ],
  [
    { label: 'COS', code: KEY_CODES.COS,    second: 'A' },  // acos → var A
    { label: 'SIN', code: KEY_CODES.SIN,    second: 'B' },  // asin → var B
    { label: 'TAN', code: KEY_CODES.TAN,    second: 'C' },  // atan → var C
    { label: '/',   code: KEY_CODES.DIVIDE, second: 'O' },  // var O
    { label: '√',   code: KEY_CODES.SQRT,   second: 'D' },  // var D
  ],
  [
    { label: '7', code: KEY_CODES.SEVEN, second: 'F' },
    { label: '8', code: KEY_CODES.EIGHT, second: 'G' },
    { label: '9', code: KEY_CODES.NINE,  second: 'H' },
    { label: '*', code: KEY_CODES.TIMES, second: 'T' },
    { label: '^', code: KEY_CODES.POW,   second: 'E' },  // var E
  ],
  [
    { label: '4',  code: KEY_CODES.FOUR,  second: 'K' },
    { label: '5',  code: KEY_CODES.FIVE,  second: 'L' },
    { label: '6',  code: KEY_CODES.SIX,   second: 'M' },
    { label: '-',  code: KEY_CODES.MINUS, second: 'N' },
    { label: 'Pi', code: KEY_CODES.PI,    second: 'W' },  // var W
  ],
  [
    { label: '1',   code: KEY_CODES.ONE,  second: 'P' },
    { label: '2',   code: KEY_CODES.TWO,  second: 'Q' },
    { label: '3',   code: KEY_CODES.THREE,second: 'R' },
    { label: '+',   code: KEY_CODES.PLUS, second: 'S' },
    { label: 'Ans', code: KEY_CODES.E },                   // pas de 2nd
  ],
  [
    { label: '.',  code: KEY_CODES.COMA,                second: 'V' },
    { label: '0',  code: KEY_CODES.ZERO,                second: 'U' },
    { label: '(',  code: KEY_CODES.OPENING_PARENTHESIS, second: 'I' },
    { label: ')',  code: KEY_CODES.CLOSING_PARENTHESIS, second: 'J' },
    { label: 'OK', code: KEY_CODES.OK, variant: 'ok' },    // pas de 2nd
  ]
];

export default function App() {
  const canvasRef = useRef(null);
  const moduleRef = useRef(null);
  const framePtrRef = useRef(0);
  const rafRef = useRef(0);
  const [status, setStatus] = useState('Chargement du module...');
  const [showSplash, setShowSplash] = useState(true);
  const [snd, setSnd] = useState(false);
  const logoRef = useRef(null);

  // Load the logo image
  useEffect(() => {
    const logo = new Image();
    logo.src = '/logo.png';
    logo.onload = () => {
      logoRef.current = logo;
      const canvas = canvasRef.current;
      if (canvas && showSplash) {
        canvas.width = 320;
        canvas.height = 240;
        const ctx = canvas.getContext('2d', { alpha: false });
        ctx.fillStyle = '#121212';
        ctx.fillRect(0, 0, 320, 240);
        const x = (320 - logo.width) / 2;
        const y = (240 - logo.height) / 2;
        ctx.drawImage(logo, x, y);
      }
    };
  }, []);

  useEffect(() => {
    if (showSplash && logoRef.current) {
      const canvas = canvasRef.current;
      if (canvas) {
        canvas.width = 320;
        canvas.height = 240;
        const ctx = canvas.getContext('2d', { alpha: false });
        ctx.fillStyle = '#f0f0e8';
        ctx.fillRect(0, 0, 320, 240);
        const x = (320 - logoRef.current.width) / 2;
        const y = (240 - logoRef.current.height) / 2;
        ctx.drawImage(logoRef.current, x, y);
      }
    }
  }, [showSplash]);

  useEffect(() => {
    if (showSplash) return;

    let active = true;

    const load = async () => {
      const wasmModule = await import('./wasm/opencalc.js');
      const createModule = wasmModule.default;
      const Module = await createModule();

      if (!active) return;
      moduleRef.current = Module;

      const width = Module._opencalc_fb_width();
      const height = Module._opencalc_fb_height();
      const ptr = Module._opencalc_framebuffer();

      framePtrRef.current = ptr;

      const canvas = canvasRef.current;
      canvas.width = width;
      canvas.height = height;

      const ctx = canvas.getContext('2d', { alpha: false });
      ctx.imageSmoothingEnabled = false;

      const imageData = ctx.createImageData(width, height);

      setStatus('Pret');

      const render = () => {
        if (!moduleRef.current) return;

        const fb = Module.HEAPU16.subarray(
          framePtrRef.current >> 1,
          (framePtrRef.current >> 1) + width * height
        );

        const data = imageData.data;
        for (let i = 0; i < fb.length; i++) {
          const color = fb[i];
          const r = (color >> 11) & 0x1f;
          const g = (color >> 5) & 0x3f;
          const b = color & 0x1f;
          const idx = i * 4;
          data[idx] = (r << 3) | (r >> 2);
          data[idx + 1] = (g << 2) | (g >> 4);
          data[idx + 2] = (b << 3) | (b >> 2);
          data[idx + 3] = 255;
        }

        ctx.putImageData(imageData, 0, 0);
        rafRef.current = requestAnimationFrame(render);
      };

      rafRef.current = requestAnimationFrame(render);
    };

    load();

    return () => {
      active = false;
      cancelAnimationFrame(rafRef.current);
    };
  }, [showSplash]);

  useEffect(() => {
    const handleKeyDown = (event) => {
      if (showSplash) {
        setShowSplash(false);
        return;
      }
      const code = KEYBOARD_MAP[event.key];
      if (code === undefined || !moduleRef.current) return;
      event.preventDefault();
      if (code === KEY_CODES.SECOND) {
        setSnd(s => !s);
      }
      moduleRef.current._opencalc_key_down(code);
    };

    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [showSplash]);

  const sendKey = (code) => {
    if (showSplash) {
      setShowSplash(false);
      return;
    }
    if (code === KEY_CODES.SECOND) {
      setSnd(s => !s);
    }
    if (!moduleRef.current) return;
    moduleRef.current._opencalc_key_down(code);
  };

  const renderKey = (key, index, extraClass = '') => (
    <button
      key={`${key.label || 'blank'}-${index}`}
      className={`key ${key.variant ? `key-${key.variant}` : ''} ${extraClass}`.trim()}
      style={{ position: 'relative' }}
      onPointerDown={(event) => {
        event.preventDefault();
        if (key.code !== null) {
          sendKey(key.code);
        }
      }}
      type="button"
    >
      {/* Label 2nd en haut à gauche */}
      {key.second && (
        <span style={{
          position: 'absolute',
          top: '3px',
          left: '4px',
          fontSize: '8px',
          fontWeight: '700',
          lineHeight: 1,
          color: snd ? '#ffe066' : '#a89fc0',
          opacity: snd ? 1 : 0.75,
          transition: 'color 0.15s, opacity 0.15s',
          pointerEvents: 'none',
          letterSpacing: '0.2px',
        }}>
          {key.second}
        </span>
      )}
      {key.label}
    </button>
  );

  return (
    <div className="sim-root">
      <div className="device">
        <div className="screen-shell">
          <canvas ref={canvasRef} className="screen-canvas" />
        </div>

        <div className="control-section">
          <div className="function-area">
            <div className="function-row">
              <button
                className="key key-fn key-small"
                style={{
                  color: snd ? '#ffe066' : undefined,
                  boxShadow: snd
                    ? 'inset 0 -2px 0 rgba(0,0,0,0.15), 0 0 8px rgba(255,220,80,0.4)'
                    : undefined,
                  transition: 'color 0.15s, box-shadow 0.15s',
                }}
                onPointerDown={() => sendKey(KEY_CODES.SECOND)}
                type="button"
              >
                2nd
              </button>
              <button className="key key-fn key-small" type="button">alpha</button>
              <button className="key key-fn key-small" type="button"></button>
            </div>
            <div className="function-row">
              <button className="key key-fn key-small" onPointerDown={() => sendKey(KEY_CODES.X)} type="button">phy</button>
              <button className="key key-fn key-small" onPointerDown={() => sendKey(KEY_CODES.TOOLS)} type="button"></button>
              <button className="key key-fn key-small" onPointerDown={() => sendKey(KEY_CODES.EQUAL)} type="button">=</button>
            </div>
            <div className="function-row">
              <button className="key key-fn key-small" onPointerDown={() => sendKey(KEY_CODES.E)} type="button">exp</button>
              <button className="key key-fn key-small" onPointerDown={() => sendKey(KEY_CODES.BACK)} type="button">log</button>
              <button className="key key-fn key-small" onPointerDown={() => sendKey(KEY_CODES.LN)} type="button">ln</button>
            </div>
          </div>
          <div className="dpad">
            <button className="dpad-key dpad-up" onPointerDown={() => sendKey(KEY_CODES.UP)} type="button" />
            <button className="dpad-key dpad-down" onPointerDown={() => sendKey(KEY_CODES.DOWN)} type="button" />
            <button className="dpad-key dpad-left" onPointerDown={() => sendKey(KEY_CODES.LEFT)} type="button" />
            <button className="dpad-key dpad-right" onPointerDown={() => sendKey(KEY_CODES.RIGHT)} type="button" />
            <div className="dpad-center" />
          </div>
        </div>

        <div className="main-grid">
          {MAIN_KEYS.flat().map((key, i) => renderKey(key, `main-${i}`, ''))}
        </div>

        <div className="status-chip" aria-hidden="true">
          {status}
        </div>
      </div>
    </div>
  );
}