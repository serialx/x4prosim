# WebAssembly JIT scripts

Build and run instructions, both address modes, CI/Pages setup, measurements
and limits are in [X4PROSIM.md](../../X4PROSIM.md#webassembly-build).
Browser controls and Chrome automation are in [web/README.txt](web/README.txt).

From the repository root, with Emscripten 6.0.12 activated:

```sh
x4prosim/wasm/build-deps.sh
x4prosim/wasm/build.sh --web
x4prosim/wasm/run-node.sh flash.bin sd.img
python3 x4prosim/wasm/serve.py build-wasm-32limit/web-dist
```

The default `WASM64_MODE=32` uses `MEMORY64=2`, lowering 64-bit host pointers
to 32-bit Wasm memory addressing. It needs no browser Memory64 support.
Set `WASM64_MODE=64` for build/run/smoke/package to use native Memory64 in
`build-wasm/` instead. Both modes use the wasm64 JIT, with TCI for cold blocks,
and share `build-wasm-deps/sysroot-wasm64`; the old wasm32 TCI sysroot cannot
be reused. Native Memory64 needs Chrome 133+ or Firefox 134+, not Safari;
the lowered mode is the portable option. Emulator acceptance covers Chrome
155 and Node 26, not Firefox/Safari.

`smoke.sh flash.bin sd.img` verifies X3 Home, JIT activity and X4 Pro ROM/panel;
`smoke.sh --rom-only` needs no firmware. Smoke copies both input images;
`run-node.sh` copies flash but writes the supplied SD directly. Use copies.
Evidence defaults to `build-wasm/evidence/smoke` in either mode.
`package-web.sh` packages existing binaries without rebuilding.

`WASM_SYSROOT=/absolute/path/to/sysroot-wasm64` lets both build scripts reuse
compatible dependencies. `WASM_JIT_STATS=1` enables Node JIT module counts;
`ICOUNT_SLEEP=off` gives deterministic idle timing for comparisons. See the
full guide before changing the fixed 256 MiB heap or 64 MiB translation cache.
