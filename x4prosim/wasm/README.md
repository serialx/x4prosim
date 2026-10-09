# WebAssembly scripts

Build and run instructions, prerequisites, Pages setup, limits and measurements
are in [X4PROSIM.md](../../X4PROSIM.md#webassembly-build). For the browser controls
and dependency-free Chrome test, see [web/README.txt](web/README.txt).

From the repository root, with Emscripten 6.0.12 activated:

```sh
x4prosim/wasm/build-deps.sh
x4prosim/wasm/build.sh --web
x4prosim/wasm/run-node.sh flash.bin sd.img
python3 x4prosim/wasm/serve.py build-wasm/web-dist
```

`smoke.sh flash.bin sd.img` verifies X3 Home and the X4 Pro ROM/panel;
`smoke.sh --rom-only` needs no firmware. Both copy test images into the evidence
directory. `WASM_SYSROOT=/absolute/path/to/existing/sysroot` lets both build
scripts reuse dependencies compiled with the same emsdk version.
