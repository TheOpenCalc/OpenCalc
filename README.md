This repo contains everything about OpenCalc, you can see and understand how everything is built. It should be explained in the differents README (depending on when you read this). To get more info about how you can contribute and join us you can check our website ( <a href ="opencalc.fr">opencalc.fr</a> )

## Web Simulator

Try the calculator in your browser! The firmware can be compiled to WebAssembly:

```bash
# Install Emscripten first (see WASM_BUILD.md)
source ~/emsdk/emsdk_env.sh
./firmware/wasm/build_wasm.sh

# Run the web simulator
cd web-sim
npm install
npm run dev
```

For detailed build instructions, see:
- [`WASM_BUILD.md`](WASM_BUILD.md) - How to compile the firmware to WASM
- [`web-sim/README.md`](web-sim/README.md) - Web simulator documentation

## Build Your Own

You are welcome to build and/or modify your OpenCalc (it's open source but you probably already know it if you read this) , you can choose to build everything from scratch or buy some pieces from us depending on your time and knowledge. The calculatore is made frome
<ol><li>A case which can be 3D printed </li>
<li>A PCB wich can be manufactured or homemade (in theory, we would love to hear from you if you did it)</li>
<li>A battery (we very highly advise NOT TO build it yourself from bare lithium)  </li>
<li>A raspberry pico 2 (or pi pico depending on performance you wish to have) which you can buy or build yourself</li>
<li>A screen you can buy from our website</li></ol>

If you custom your OpenCalc you can send us a message in order to share it with us, you could help build a better calculator without even knowing it.
