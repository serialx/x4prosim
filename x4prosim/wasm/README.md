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

`smoke.sh flash.bin sd.img` detects X3 or X4 Pro and verifies Home, settled
startup, JIT activity and the panel; X3 also checks the X4 Pro blank-flash ROM.
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
and compares SHA256 against `mkflash.py` output before checking Home and controls. Set
`WEB_MACHINE=x4pro` for X4 Pro firmware and its Library touch check. See [web/README.txt](web/README.txt) for complete commands.

`WASM_SYSROOT=/absolute/path/to/sysroot-wasm64` lets both build scripts reuse
compatible dependencies. `WASM_JIT_STATS=1` enables Node JIT module counts;
`RUN_NODE_SLEEP=off` gives deterministic idle timing for comparisons. See the
full guide before changing the fixed 256 MiB heap or 64 MiB translation cache.

Accurate timing is the default for `run-node.sh` and the browser page (checkbox
unticked). Opt into turbo with `TURBO=1 x4prosim/wasm/run-node.sh flash.bin sd.img`
or `?turbo=1`; `TURBO=0` and `?turbo=0` remain accepted. Turbo keeps `-icount`
with `sleep=off`, minimizes device delays and uses synchronous GPSPI transfers.
It changes guest timing; `smoke.sh` tests accurate mode and
`smoke.sh --turbo flash.bin sd.img` compares against native turbo without
requiring timestamp equality.

The 2026-10-10 stack combines inline Emscripten file I/O with amortized BQL
locking, lock-free GPSPI CMD reads with single-access dispatch, and direct
TB tail chaining in the wasm64 backend. These changes preserve accurate guest
waits and panel bytes; turbo separately reduces device delays and changes guest
timing.

CrossPoint 1.6.5 X3 on an Apple M5 Max, Emscripten 6.0.12, Node 26 and
Chrome 155, using `MEMORY64=2`. Baseline is the build before these three
changes. Node and native figures are independent three-run medians; Chrome
figures are single-run confirmations with GPU/WebGL.
Home is the third completed refresh; post-Home ends at the following `[MEM]`
checkpoint. Chrome Home starts at image loading, while settled starts at page
load, so those columns do not sum to the post-Home interval. Accurate figures
use `sleep=off` for deterministic comparisons; normal launches use `sleep=on`.

| Runtime / mode | Baseline Home / post-Home / settled | Combined stack Home / post-Home / settled | Post-Home reduction |
| --- | ---: | ---: | ---: |
| Node accurate, `sleep=off` | 2.404 / 9.897 / 12.299 s | 1.953 / 7.314 / 9.255 s | 26.10% |
| Chrome accurate, `sleep=off` | 2.829 / 11.484 / 14.835 s | 2.358 / 8.580 / 11.463 s | 25.29% |
| Chrome turbo | 1.368 / 3.993 / 5.886 s | 1.227 / 2.929 / 4.681 s | 26.65% |
| Native accurate, `sleep=off` | 1.776 / 8.794 / 10.573 s | 1.541 / 8.024 / 9.565 s | 8.76% |

With `sleep=off`, accurate Node now settles within about 10.05 s of guest time;
Chrome still takes longer. Both address modes build and pass smoke. The hosted page is
<https://serialx.github.io/x4prosim/>.
Official CrossPoint 1.6.5 X4 Pro now reaches Home in native, Node and Chrome,
including accurate and turbo modes; the browser verifies Down/Up and a pointer
tap into Library. This requires the shared-TB CPU context reload, UC8179 timer
rounding and SDL logical-coordinate fixes in the current source. S3 deep-sleep
wake remains unsupported. This X4 Pro verification used `MEMORY64=2`;
`MEMORY64=1` and Safari/Firefox were not retested with these fixes.

## Wi-Fi and font downloads

The browser can download fonts through the firmware's normal **Manage Fonts**
flow without a relay, native QEMU, or a running backend. Build and serve the
static package:

```sh
x4prosim/wasm/build.sh --web
python3 x4prosim/wasm/serve.py build-wasm-32limit/web-dist
```

Leave **Enable Wi-Fi downloads** checked, start an official CrossPoint 1.6.5
image, and open **Settings > Reader > Manage Fonts**. Join the open
**PICSimLabWifi** network. Installed fonts are saved with the SD card in this
browser. Use **Download SD image** to export a copy.
The package also works on static HTTPS hosting with the existing isolation
service worker. `?wifi=off` disables networking.

Packaging builds a separate wasm32 module using pinned lwIP 2.2.1 and mbedTLS
3.6.7 sources, then downloads the font assets and license notices. Both QEMU
address modes use the same network module. Initial preparation needs internet
access; later packaging reuses the local assets and dependency archives.
Emscripten, CMake, Ninja, OpenSSL, curl, and Python 3 are required to build it.

The default catalog includes Alef, Literata, Noto Sans Extended, and Pretendard.
Only these prepared families appear in the device's download list. To select a
different bundle, prepare it and package again:

```sh
python3 x4prosim/wasm/prepare-wifi-assets.py build-wasm-deps/wifi-assets \
  --family Alef --family Literata --family SourceSerif4
x4prosim/wasm/package-web.sh
```

`--catalog` selects a different font format/release manifest. The default is
`sd-fonts-m1-b4`. For families without a built-in license source, supply
`--license-dir /path/to/notices`, containing `<Family>.txt` notices. Each font
is checked against the catalog's size and CRC32; `index.json` records source
URLs and SHA256 hashes. Files stay in ignored build directories. To use another
prepared directory, set `WIFI_ASSETS=/absolute/path` when packaging.
Packaging verifies the prepared files and publishes only the selected catalog
and its licenses. Unselected fonts can remain in the download cache for reuse.

This adapter implements **HTTP downloads**, not general internet networking.
lwIP terminates Ethernet/TCP locally; DHCP and DNS assign the virtual network,
and NTP uses the browser's clock. mbedTLS terminates guest HTTPS in the page.
JavaScript maps prepared font URLs to static files and streams the response
back to the guest. Sending advances on writes and TCP acknowledgements; the
maintenance timer does not impose a per-block download delay.

Other GET/HEAD requests use browser fetch and remain subject
to CORS, mixed-content rules, and browser header restrictions. Uploads, incoming
servers, arbitrary TCP/UDP protocols, and forwarding guest credentials are not
supported. Missing or blocked downloads return an HTTP error, not an empty
successful file.

The local TLS endpoint uses an untrusted, self-signed certificate. CrossPoint
1.6.5's wolfSSL downloader already disables certificate verification, allowing
this to work without firmware patches. Firmware that verifies certificates or
pins keys will reject it. Real HTTPS fetches still use the browser's certificate
verification; no browser certificate or security setting needs changing.

Node can use the same adapter after `build.sh --web`:

```sh
WIFI_ASSETS="$PWD/build-wasm-deps/wifi-assets" \
  x4prosim/wasm/run-node.sh flash.bin sd.img
```

Node defaults to networking off without `WIFI_ASSETS`. The browser and Node
release sessions and pending downloads when the runtime stops or aborts.

Network checks (no relay or host sockets):

```sh
x4prosim/wasm/build-browser-net.sh
python3 x4prosim/wasm/prepare-wifi-assets.py build-wasm-deps/wifi-assets
node --test x4prosim/wasm/test-browser-network.mjs
python3 -m unittest discover -s x4prosim/wasm -p test_wifi_assets.py
```

Set `BENCH_NETWORK=1` on the Node test command to also measure TCP/TLS throughput
with a 10 ms simulated guest poll interval and verify every downloaded byte.
