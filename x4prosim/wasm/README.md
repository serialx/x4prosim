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
`package-web.sh` packages existing binaries without rebuilding, copies the
C3/S3 bootloaders and licenses, and generates the partition table and C3 NVS
seed with the Python helpers in `x4prosim/`.

The browser accepts an official CrossPoint release app `.bin` or a 16 MiB
flash dump through its picker or `?flash=<url>`. Use
`crosspoint-<ver>-x3-x4.bin` for X3, `crosspoint-<ver>-x4pro.bin` for X4 Pro.
Download apps from the [CrossPoint releases](https://github.com/crosspoint-reader/crosspoint-reader/releases).
App images are composed in browser memory with the bundled boot assets;
no esptool or PlatformIO is needed. Full flash dumps pass through unchanged.
The browser smoke test accepts `PICKER_FLASH=<app.bin>` or
`RELEASE_APP=<local-app.bin>` for a URL input, verifies composition status,
and compares SHA256 against `mkflash.py` output before checking X3 Home and
controls. See [web/README.txt](web/README.txt) for complete commands.

`WASM_SYSROOT=/absolute/path/to/sysroot-wasm64` lets both build scripts reuse
compatible dependencies. `WASM_JIT_STATS=1` enables Node JIT module counts;
`RUN_NODE_SLEEP=off` gives deterministic idle timing for comparisons. See the
full guide before changing the fixed 256 MiB heap or 64 MiB translation cache.

Turbo is the default for `run-node.sh` and the browser page (checkbox ticked).
Select accurate timing with `TURBO=0 x4prosim/wasm/run-node.sh flash.bin sd.img`
or `?turbo=0`. Turbo keeps `-icount` with `sleep=off`, minimizes device delays
and uses synchronous GPSPI transfers. It changes guest timing; `smoke.sh` tests
accurate mode and `smoke.sh --turbo flash.bin sd.img` compares against native
turbo without requiring timestamp equality.

Final M5 Max three-run medians (`MEMORY64=2`, seconds):

| Runtime / mode | Home | Settled |
| --- | ---: | ---: |
| Node accurate, `sleep=off` | 2.394 | 12.464 |
| Chrome accurate, `?sleep=off` | 3.069 | 14.758 |
| Node turbo | 1.097 | 4.789 |
| Chrome turbo | 1.636 | 5.558 |

Accurate mode beats guest time to Home (3.250 s), but misses settled startup
(about 10.05 s); turbo beats both by changing delays. Both address modes build
and pass smoke. The hosted page is <https://serialx.github.io/x4prosim/>.
Safari/Firefox and X4 Pro firmware were not executed; X4 Pro coverage is
ROM/panel only.
