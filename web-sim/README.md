# OpenCalc Web Simulator (React + WASM)

## Build WASM (firmware)

1. Installez Emscripten (emsdk) et activez-le.
2. Depuis `firmware/wasm`, executez :

```bash
./build_wasm.sh
```

Cela genere `web-sim/src/wasm/opencalc.js` + `web-sim/src/wasm/opencalc.wasm`.

## Lancer le front

```bash
cd web-sim
npm install
npm run dev
```

Puis ouvrez l URL donnee par Vite.
