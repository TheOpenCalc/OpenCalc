# OpenCalc

An open-source calculator built around the Raspberry Pi Pico / Pico 2.  
For contribution guidelines, hardware kits, and community updates, visit **[opencalc.fr](https://opencalc.fr)**.

---

## Firmware Architecture

The codebase is intentionally modular. There is a clear distinction between **apps** and the **core**:

- **Core** — handles calculations, screen rendering, button input, and all low-level interactions:
  - `ui.cpp` — display and input management
  - `Evaluator.cpp` — expression evaluation engine
  - `stack.cpp` — stack implementation used by the evaluator

- **Apps** — built on top of the core, each implementing a specific feature:
  - `Calc.cpp` — standard calculator
  - `elements.cpp` — periodic table / element lookup
  - `grapher.cpp` — function grapher
  - `menu.cpp` — navigation menu
  - `sequences.cpp` — sequence mode *(not fully implemented yet)*
  - `settings.cpp` — user settings

> **Contributing?** Please respect this separation. It keeps the codebase coherent and avoids duplicated logic. If you are adding a new feature, it should be a new app that calls core functions — not a modification of the core itself.

---

## Web Simulator

>The simulator is available at **[emulateur.opencalc.fr](https://emulateur.opencalc.fr)**

You can try the calculator directly in your browser. The firmware can be compiled to WebAssembly:

```bash
# Install Emscripten first (see WASM_BUILD.md)
source ~/emsdk/emsdk_env.sh
./firmware/wasm/build_wasm.sh

# Run the web simulator
cd web-sim
npm install
npm run dev
```

For detailed instructions, see:
- `WASM_BUILD.md` — how to compile the firmware to WASM
- `web-sim/README.md` — web simulator documentation

---

## Build Your Own

OpenCalc is fully open source and designed to be built or modified. You can build everything from scratch or purchase individual components from us depending on your time and skills.

The calculator is made of 5 parts:

1. **Case** — 3D printable (files available in this repo)
2. **PCB** — can be manufactured professionally or made at home *(if you manage to make it yourself, we'd genuinely love to hear about it)*
3. **Battery** — we **strongly advise against** building this yourself from bare lithium cells; use a standard Li-Po pack
4. **Microcontroller** — Raspberry Pi Pico or Pico 2 *(Pico 2 recommended for better performance)*
5. **Screen** — available on our website or compatible alternatives

If you build a custom OpenCalc, send us a message! Sharing your build helps the whole community improve the hardware and firmware, even in ways you might not expect.

---

## Getting Started (Firmware)

The firmware is built with the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) using CMake:

```bash
mkdir build && cd build
cmake ..
make
```

Flash the resulting `.uf2` file to your Pico by holding BOOTSEL while plugging it in, then dragging the file onto the mounted drive.

---

## Contributing

- Keep apps and core separate
- Avoid rewriting logic that already exists in the core
- Document any new public functions in `Doc.md`
- Open a pull request with a clear description of what your change does and why

More details at **[opencalc.fr](https://opencalc.fr)**.