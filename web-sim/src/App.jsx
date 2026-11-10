import { useEffect, useRef, useState } from 'react';

// Doit rester identique à `enum touches` dans firmware/headers/ui.h.
const KEY_CODES = {
  COMA: 0,
  ZERO: 1,
  OPENING_PARENTHESIS: 2,
  CLOSING_PARENTHESIS: 3,
  ENTER: 4,
  NOT2: 5,
  NOT3: 6,
  ONE: 7,
  TWO: 8,
  THREE: 9,
  PLUS: 10,
  ANS: 11,
  NOT4: 12,
  NOT5: 13,
  FOUR: 14,
  FIVE: 15,
  SIX: 16,
  MINUS: 17,
  PI: 18,
  NOT6: 19,
  NOT7: 20,
  SEVEN: 21,
  EIGHT: 22,
  NINE: 23,
  TIMES: 24,
  POW: 25,
  NNNN: 26,
  NN: 27,
  COS: 28,
  SIN: 29,
  TAN: 30,
  DIVIDE: 31,
  SQRT: 32,
  NNN: 33,
  NNNNN: 34,
  X: 35,
  TOOLS: 36,
  HOME: 37,
  CONST: 38,
  BACK: 39,
  NOTTTT: 40,
  NOTTT: 41,
  E: 42,
  LOG: 43,
  LN: 44,
  DOWN: 45,
  LEFT: 46,
  OK: 47,
  RIGHT: 48,
  PHY: 49,
  NOTN: 50,
  EQUAL: 51,
  SECOND: 52,
  ALPHA: 53,
  PASS4: 54,
  UP: 55,
  END_KEYS: 56,
  FACT: 57,
  COSH: 58,
  SINH: 59,
  TANH: 60,
  ACOSH: 61,
  ASINH: 62,
  ATANH: 63,
  IMAGINARY: 64,
  CONJ: 65,
  ARG: 66,
  MOD: 67,
  ACOS: 68,
  ASIN: 69,
  ATAN: 70,
  MATRIX_2X2: 71,
  MATRIX_3X3: 72,
  MATRIX_4X4: 73,
  MATRIX_2X3: 74,
  MATRIX_3X2: 75,
  MATRIX_CUSTOM: 76,
  FUNC_DET: 77,
  FUNC_INV: 78,
  FUNC_TRANS: 79,
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
    { label: '÷',   code: KEY_CODES.DIVIDE, second: 'O' },  // var O
    { label: '√',   code: KEY_CODES.SQRT,   second: 'D' },  // var D
  ],
  [
    { label: '7', code: KEY_CODES.SEVEN, second: 'F' },
    { label: '8', code: KEY_CODES.EIGHT, second: 'G' },
    { label: '9', code: KEY_CODES.NINE,  second: 'H' },
    { label: '×', code: KEY_CODES.TIMES, second: 'T' },
    { label: '^', code: KEY_CODES.POW,   second: 'E' },  // var E
  ],
  [
    { label: '4',  code: KEY_CODES.FOUR,  second: 'K' },
    { label: '5',  code: KEY_CODES.FIVE,  second: 'L' },
    { label: '6',  code: KEY_CODES.SIX,   second: 'M' },
    { label: '−',  code: KEY_CODES.MINUS, second: 'N' },
    { label: 'π',  code: KEY_CODES.PI,    second: 'W' },  // var W
  ],
  [
    { label: '1',   code: KEY_CODES.ONE,  second: 'P' },
    { label: '2',   code: KEY_CODES.TWO,  second: 'Q' },
    { label: '3',   code: KEY_CODES.THREE,second: 'R' },
    { label: '+',   code: KEY_CODES.PLUS, second: 'S' },
    { label: 'Ans', code: KEY_CODES.ANS },                 // pas de 2nd
  ],
  [
    { label: '.',  code: KEY_CODES.COMA,                second: 'V' },
    { label: '0',  code: KEY_CODES.ZERO,                second: 'U' },
    { label: '(',  code: KEY_CODES.OPENING_PARENTHESIS, second: 'I' },
    { label: ')',  code: KEY_CODES.CLOSING_PARENTHESIS, second: 'J' },
    { label: 'OK', code: KEY_CODES.OK, variant: 'ok' },    // pas de 2nd
  ]
];

const FB_W = 320;
const FB_H = 240;

export default function App() {
  const canvasRef = useRef(null);
  const moduleRef = useRef(null);
  const framePtrRef = useRef(0);
  const rafRef = useRef(0);
  const [status, setStatus] = useState('Chargement du module...');
  const [showSplash, setShowSplash] = useState(true);
  const [snd, setSnd] = useState(false);
  const logoRef = useRef(null);
  // Image 320x240 de l'ecran, agrandie ensuite dans le canvas visible.
  const offRef = useRef(null);
  if (!offRef.current && typeof document !== 'undefined') {
    const off = document.createElement('canvas');
    off.width = FB_W;
    off.height = FB_H;
    offRef.current = off;
  }

  // Copie l'image 320x240 dans le canvas visible. Sa resolution interne est
  // un multiple entier de 320x240 >= sa taille affichee : l'agrandissement
  // (pixels nets) est exact et le navigateur ne fait que reduire en lissant,
  // donc aucune ligne de pixels n'est sautee ou doublee.
  const presentTo = (canvas) => {
    if (!canvas) return;
    const dpr = window.devicePixelRatio || 1;
    const shown = canvas.clientWidth || FB_W;
    const n = Math.max(1, Math.ceil((shown * dpr) / FB_W));
    if (canvas.width !== FB_W * n) {
      canvas.width = FB_W * n;
      canvas.height = FB_H * n;
    }
    const ctx = canvas.getContext('2d', { alpha: false });
    ctx.imageSmoothingEnabled = false;
    ctx.drawImage(offRef.current, 0, 0, canvas.width, canvas.height);
  };

  // Ecran de l'emulateur + copie agrandie a cote (canvas #bigScreen de index.html).
  const present = () => {
    presentTo(canvasRef.current);
    presentTo(document.getElementById('bigScreen'));
  };

  const drawSplash = () => {
    const logo = logoRef.current;
    if (!logo) return;
    const ctx = offRef.current.getContext('2d', { alpha: false });
    ctx.fillStyle = '#f0f0e8';
    ctx.fillRect(0, 0, FB_W, FB_H);
    ctx.drawImage(logo, (FB_W - logo.width) / 2, (FB_H - logo.height) / 2);
    present();
  };

  // Redessine quand la taille d'affichage change (fenetre, zoom...).
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || typeof ResizeObserver === 'undefined') return undefined;
    const ro = new ResizeObserver(() => present());
    ro.observe(canvas);
    const big = document.getElementById('bigScreen');
    if (big) ro.observe(big);
    return () => ro.disconnect();
  }, []);

  // Load the logo image
  useEffect(() => {
    const logo = new Image();
    logo.src = '/logo.png';
    logo.onload = () => {
      logoRef.current = logo;
      if (showSplash) drawSplash();
    };
  }, []);

  useEffect(() => {
    if (showSplash) drawSplash();
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

      const offCtx = offRef.current.getContext('2d', { alpha: false });
      const imageData = offCtx.createImageData(width, height);

      setStatus('Pret');

      const render = () => {
        if (!moduleRef.current) return;

        const fb = Module.HEAPU16.subarray(
          framePtrRef.current >> 1,
          (framePtrRef.current >> 1) + width * height
        );

        const data = imageData.data;
        // Luminosite logicielle (0..20), identique au firmware : 25 %..100 %.
        const level = Module._opencalc_brightness ? Module._opencalc_brightness() : 20;
        const dim = (64 + (192 * Math.max(0, Math.min(20, level))) / 20) / 256;
        for (let i = 0; i < fb.length; i++) {
          const color = fb[i];
          const r = (color >> 11) & 0x1f;
          const g = (color >> 5) & 0x3f;
          const b = color & 0x1f;
          const idx = i * 4;
          data[idx] = ((r << 3) | (r >> 2)) * dim;
          data[idx + 1] = ((g << 2) | (g >> 4)) * dim;
          data[idx + 2] = ((b << 3) | (b >> 2)) * dim;
          data[idx + 3] = 255;
        }

        offCtx.putImageData(imageData, 0, 0);
        present();
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
      const key = event.key;
      const printable =
        key.length === 1 &&
        !event.ctrlKey && !event.metaKey && !event.altKey;
      const code = KEYBOARD_MAP[key];
      if (code === undefined && !printable) return;
      if (!moduleRef.current) return;
      event.preventDefault();
      if (code === KEY_CODES.SECOND) {
        setSnd(s => !s);
      }
      if (code !== undefined) {
        moduleRef.current._opencalc_key_down(code);
      }
      if (printable) {
        moduleRef.current._opencalc_text_char(key.charCodeAt(0));
      }
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
      className={`key ${key.variant ? `key-${key.variant}` : ''} ${/^[^\w]$/.test(key.label) ? 'key-sym' : ''} ${extraClass}`.replace(/\s+/g, ' ').trim()}
      style={{ position: 'relative' }}
      onPointerDown={() => {
        if (key.code !== null) {
          sendKey(key.code);
        }
      }}
      type="button"
    >
      {/* Label 2nd en haut à gauche */}
      {key.second && (
        <span className={`key-second${snd ? ' active' : ''}`}>
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
              <button className="key key-fn key-small" onPointerDown={() => sendKey(KEY_CODES.ALPHA)} type="button">alpha</button>
              <button className="key key-fn key-small" onPointerDown={() => sendKey(KEY_CODES.IMAGINARY)} type="button">i</button>
            </div>
            <div className="function-row">
              <button className="key key-fn key-small" onPointerDown={() => sendKey(KEY_CODES.PHY)} type="button">phy</button>
              <button className="key key-fn key-small" onPointerDown={() => sendKey(KEY_CODES.NOTN)} type="button" aria-label="Touche non attribuée"></button>
              <button className="key key-fn key-small" onPointerDown={() => sendKey(KEY_CODES.EQUAL)} type="button">=</button>
            </div>
            <div className="function-row">
              <button className="key key-fn key-small" onPointerDown={() => sendKey(KEY_CODES.E)} type="button">exp</button>
              <button className="key key-fn key-small" onPointerDown={() => sendKey(KEY_CODES.LOG)} type="button">log</button>
              <button className="key key-fn key-small" onPointerDown={() => sendKey(KEY_CODES.LN)} type="button">ln</button>
            </div>
          </div>
          <div className="dpad">
            <button className="dpad-key dpad-up" onPointerDown={() => sendKey(KEY_CODES.UP)} type="button" aria-label="up">▲</button>
            <button className="dpad-key dpad-down" onPointerDown={() => sendKey(KEY_CODES.DOWN)} type="button" aria-label="down">▼</button>
            <button className="dpad-key dpad-left" onPointerDown={() => sendKey(KEY_CODES.LEFT)} type="button" aria-label="left">◀</button>
            <button className="dpad-key dpad-right" onPointerDown={() => sendKey(KEY_CODES.RIGHT)} type="button" aria-label="right">▶</button>
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