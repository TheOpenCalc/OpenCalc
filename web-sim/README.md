# OpenCalc Web Simulator (React + WASM)

A web-based simulator for the OpenCalc calculator, running the actual firmware compiled to WebAssembly.

## Prerequisites

- [Node.js](https://nodejs.org/) (v18+)
- [Emscripten SDK](https://emscripten.org/) for compiling firmware to WASM

## Build WASM (firmware)

1. Install Emscripten SDK:
```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
```

2. Activate Emscripten in your shell:
```bash
source ~/emsdk/emsdk_env.sh
```

3. From the repository root, run the build script:
```bash
./firmware/wasm/build_wasm.sh
```

This generates:
- `web-sim/src/wasm/opencalc.js`
- `web-sim/src/wasm/opencalc.wasm`

## Run the Web Simulator

```bash
cd web-sim
npm install
npm run dev
```

Then open the URL shown by Vite (usually http://localhost:5173).

## Project Structure

```
web-sim/
├── src/
│   ├── App.jsx          # Main React component
│   ├── styles.css       # Calculator styling
│   ├── main.jsx         # Entry point
│   └── wasm/            # Compiled WASM files
├── public/
│   └── logo.png         # Splash screen logo
└── package.json
```

## Features

- Splash screen with OpenCalc logo on startup
- Interactive calculator buttons
- Keyboard support
- Real-time firmware rendering via WASM

---

*This web simulator was developed with assistance from [Claude](https://claude.ai) (Anthropic).*
