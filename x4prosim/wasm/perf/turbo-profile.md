# X3 boot profile: native and Wasm64

The first optimization target is host synchronization around MMIO and event
notification. In Home-to-settled, the Wasm vCPU spends **50.98% of sampled elapsed
time waiting**, **15.17% in helpers/softmmu/MMIO**, and **7.09% in visible Asyncify
code**. JIT coverage is already **96.884% of guest instruction starts**; lowering
the compilation threshold alone cannot plausibly provide the required speedup.

This report measures the unmodified integration branch at `54fe80175f`, including
the learned Wasm MMIO boundary hint from `a614e3c07d`. It proposes experiments;
**no optimization or QEMU instrumentation is committed**. Tools and reproduction
commands are in [README.md](README.md). Raw paths below are relative to
`build-wasm/evidence/profile/` in the measurement worktree, and are intentionally
not committed.

## Scope and phase boundaries

Host: Apple M5 Max, macOS arm64, Node 26, Emscripten 6.0.12, native clang build;
Wasm default `WASM64_MODE=32` (`MEMORY64=2`), 256 MiB memory, 64 MiB TB cache.
Both run the same CrossPoint 1.6.5 X3 flash and fresh SD copies, headless, with
`-icount shift=0,sleep=off`. The harness enables `WASM_JIT_STATS=1` in both variants.
It alternates native/Wasm three times. No builds from this task overlap timing
runs; the shared host is not reserved, so paired ratios are more useful than
absolute times for comparisons with other workers.

Home is the third `X3_DRF` completion, guest **3250 ms**. Settled is the first
subsequent complete `[MEM]` line, guest **10049 ms** in these inputs, making the
second phase **6.799 guest seconds**. Earlier run context quoted 10056 ms; it is
not what these fresh copies log. All six clean timing runs and all six final
counter runs agree on the first eight wait lines and the settled screenshot:

```text
1115 X3_PON 127; 2050 X3_DRF 935; 2723 X3_DRF 383; 3250 X3_DRF 382
3741 X3_DRF 382; 4224 X3_DRF 382; 4701 X3_DRF 382; 8747 X3_DRF 382
PPM SHA256 b95efa19c1fa99c2abd97fb32ed1a729ecb25a17147198857067ac25523a4c5c
```

To beat guest time, Home needs a **26.8% wall-time reduction** from 4.442 s to
below 3.250 s. Home-to-settled needs **85.2%**, or **6.78x**, from 46.065 s to
below 6.799 s. Even native takes 15.258 s for that phase. Matching native alone
would miss the goal by 2.24x.

## Ranked experiments and attainable-gain estimates

The estimates below multiply sampled shares by clean wall time. They are
**conditional budgets, not measured speedups or guarantees**. Waiting is
sampled elapsed time, not CPU consumption. Categories and causal changes overlap;
do not add the savings columns. In particular, improving MMIO can also remove
BQL waiting and fiber/proxy work.

| Rank / requested lever | Evidence, Home → settled | Plausible experiment and conditional gain |
| --- | --- | --- |
| **1. Host coordination (a/e)** | Wait 50.98%, about **23.5 s**; **75.4 million BQL attempts**, **818,692 clock notifications**, **762,695 main-loop iterations** | Coalesce redundant host notifications and avoid synchronous main-JS proxy round trips while preserving virtual deadlines; reduce unnecessary BQL handoffs. Removing half to three quarters of this wait pool would save **11.7–17.6 s**. Removing every wait still leaves about **22.6 s**, above the target. |
| **2. MMIO access path (a/c/e)** | Helpers/softmmu/MMIO 15.17%, about **7.0 s**; GPSPI **65.65 million register accesses**, including **57.91 million CMD reads** | Specialize proven device accesses or reduce repeated translation/validation/import/locking work without changing each guest-visible access. Cutting half to three quarters of this direct helper cost saves **3.5–5.2 s**, with possible additional overlapping synchronization savings. Device-model bodies alone are only about **1.5%** combined, below **0.7 s**. |
| **3. Fiber/Asyncify crossings (d)** | **15,137,088 unwinds and rewinds**; visible Asyncify 7.09%, about **3.27 s**, plus separate JS glue 5.46% | Reduce crossings at their source, then prototype a compatible coroutine backend. Halving visible Asyncify cost gives about **1.63 s**; full visible elimination bounds that slice at **3.27 s**. Inlined instrumentation is unassigned, and JS glue is not all Asyncify. A larger claim needs an A/B implementation. |
| 4. Dispatch, timers, per-insn accounting (e) | Direct category 4.33%, about **2.0 s**; **14.17 million icount_get**, **747,957 GPSPI timer_mod**, **6,823 systimer timer_mod** | Keep exact virtual time while removing redundant accounting or timer-list operations. A 50% reduction in the whole direct category is about **1.0 s**. Investigate its much larger indirect wakeup cost under rank 1. |
| 5. Backend codegen / inline TLB / chaining (c) | JIT TB module self time 2.97%, about **1.37 s**; **74.05 million C load/store helper entries** versus **37.61 million inline data operations** | Faster generated arithmetic/chaining alone has a small ceiling: doubling all JIT execution saves about **0.68 s**. Inline TLB and import improvements that avoid helper/MMIO work belong to rank 2 and can be larger. Helper entries include TCI RAM hits and actual device accesses; they are not all removable TLB misses. |
| 6. JIT coverage, threshold, batching (b) | **96.884% JIT instruction share**, TCI/libffi self **0.51%** (about **0.24 s**), **939 modules** costing **29.8 ms** synchronous construction | Eliminating the entire visible TCI slice saves only **0.24 s** after Home. Batching module construction has only tens of milliseconds in measured synchronous cost. Earlier compilation may help Home, where TCI is 2.34% (about **0.10 s**), but cannot alone recover its 1.19 s deficit. |

For Home, synchronization is 35.68% (about **1.58 s**) and MMIO helpers are
29.25% (about **1.30 s**). Joint reductions there can plausibly cross 3.25 s.
For the thumbnail phase, no single measured lever reaches the target. A credible
route must remove repeated host coordination and much of the associated MMIO
and coroutine overhead together, then reprofile. Preserve all eight virtual
wait timestamps and the exact PPM at every step. Reducing GPSPI wire time,
skipping firmware polling, changing firmware, or relaxing virtual-time checks
would change the workload rather than establish faster simulation.

## Where the waiting occurs

One representative Node vCPU capture (`node-cpu/wasm-1`, 46.955 s sampled in the
second phase) attributes about **10.52 s** to BQL reacquisition below MMIO loads,
**1.07 s** below MMIO stores, and **10.35 s** to synchronous `fd_write` proxy
waiting under `qemu_clock_notify` / `rr_cpu_thread_fn`. These are distinct wait
stacks inside the overall wait category; per-run medians are in the category
table. Raw chains are retained in `node-analysis.json` under `wait_stacks_ms`.

```text
_do_futex_wait ← pthread_mutex_lock ← bql_lock_impl ← do_ld_mmio_beN
_do_futex_wait ← pthread_cond_timedwait ← emscripten_proxy_sync_with_ctx
              ← _fd_write ← qemu_clock_notify ← rr_cpu_thread_fn
```

The main-loop pthread is mostly blocked too: the first capture has about
24.08 s waiting for a proxied `poll`, 10.32 s reacquiring BQL, and 9.73 s waiting
for proxied `fd_read` in AIO dispatch. The JS main thread is about 70% idle;
`emscripten_futex_wake` alone is about 8.95% of its sampled elapsed time. These
support a synchronization/proxy hypothesis, not a saturated JS event-loop
hypothesis. See `node-mainloop-analysis.json` and `node-js-main-analysis.json`.
**Do not sum thread elapsed samples into process wall time.** Native main-loop
captures are retained as `native-{home,settled}-mainloop-analysis.json`.

Native also spends substantial time on MMIO dispatch and exception recovery.
It executes **69.34 million cpu_io_recompile exits** after Home, versus **759**
for Wasm with its existing learned MMIO boundary hint. Native self symbols
include `pthread_jit_write_protect_np` (7.87%), `g_tree_find_node` (5.40%),
`tb_tc_cmp` (3.92%), and `cpu_unwind_data_from_tb` (2.42%). Thus native and Wasm
share the same guest work but do not traverse identical host exception paths.

## Guest-side SD and GPSPI workload

The firmware logs a missing `/.crosspoint/epub_2674129756/thumb_226.bmp` at guest
2776 ms, refreshes Home at 3250 ms, performs four more display waits ending at
8747 ms, and reaches the memory log at 10049 ms. This is consistent with the
known thumbnail-generation workload; the serial log does not name every
subsequent file operation. Host counters establish its I/O cost without
requiring a firmware change.

After Home it reads **3,675 sectors / 1,881,600 bytes**, all 512-byte reads.
**2,325 reads (63.3%)** immediately follow the preceding sector; there are no
immediate same-address rereads, but the captured prefix revisits earlier
addresses repeatedly. For example, byte offsets 3,162,112 and 3,166,208 recur
between reads around 41,459,712, followed by sequential runs starting at
1,090,048. This resembles FAT metadata and file-data interleaving; mapping each
address to a particular file has not been independently decoded. The first 80
reads per phase are in `counters.json` → `sd_prefix`, and complete aggregate
counts are in each `counters-final/*/x3.log` snapshot.

There are **747,957 GPSPI transactions** and **4,588,541 full-duplex byte clocks**:
an amortized **203.53 transactions and 1,248.58 SPI bytes per SD sector**. These
ratios include protocol polling and all SSI traffic in the interval; they do
not claim every sector uses an identical transaction sequence. The model's
inclusive callback duration is **15.51 µs/transaction in Wasm**, versus
**1.29 µs native**, in instrumented builds. Its Wasm total is **11.60 s**, but
includes nested SD/block I/O, coroutine suspension and host scheduling. The
exclusive GPSPI function samples are only 0.25% of vCPU elapsed time. Do not
interpret the inclusive 11.60 s as C device-model arithmetic that can simply be
removed.

## Asyncify and JSPI feasibility

The generated-JS wrappers count real `_asyncify_start_unwind` and
`_asyncify_start_rewind` calls, using synchronous phase snapshots. They do not
infer counts from module totals or stack samples. The three-run count is stable:
396,808 unwinds before Home and 15,137,088 afterward. A few outstanding startup
unwinds explain the Home unwind/rewind difference. The wrappers do not identify
the reason for every suspension, and their timings are excluded from the clean
benchmark.

QEMU's current `util/coroutine-wasm.c` uses `emscripten_fiber_swap`, with separate
C and Asyncify stacks. Emscripten explicitly implements this fiber API through
[Asyncify](https://emscripten.org/docs/api_reference/fiber.h.html). Consequently,
replacing `-sASYNCIFY` with `-sJSPI` is not a valid drop-in experiment. A JSPI
prototype needs a compatible coroutine implementation and an audit of generated
TB modules, TCI/libffi crossings, and the imports that can suspend. JSPI wraps
suspending imports and promise-producing exports; suspension cannot cross
arbitrary intervening JavaScript frames. See the
[JSPI API](https://webassembly.github.io/js-promise-integration/js-api/) and
[Emscripten asynchronous-code guide](https://emscripten.org/docs/porting/asyncify.html).
This report does not claim a working JSPI backend or measured JSPI speedup.

## Measurement caveats and evidence

Each reported timing median uses three clean runs. Node CPU profiles use three
additional complete runs with 1 ms sampling; the analyzer crops each thread at
monotonic Home/MEM marks. Native uses three Home-only processes and three
separate complete processes sampled only after Home with macOS `sample` at a
nominal 1 ms interval. The sampler is interrupted at the milestone. Profile
timings are excluded: profiling raises Home to roughly 2.91 s native / 4.59 s
Wasm and the second phase to 20.83 s / 47.04 s. An initial native capture whose
cold symbol initialization crossed phases was rejected (`native-cpu/`); the
reported replacements are `native-home/` and `native-settled/`.

Node categories represent weighted sampled elapsed time, including futex
blocking. Native categories represent self samples; unresolved generated native
code is separate. These are not exact OS CPU-time measurements. Asyncify code
inlined into other functions and unrecognized runtime symbols remain in their
observed categories. Device symbols are mapped to source files; the top-30
appendix includes the substantial uncategorized native cost rather than hiding
it. Wasm generated TB entries are grouped because module-local symbols do not
provide a stable guest-PC name.

Counters use three further interleaved runs. Temporary C/TCG instrumentation
increments guest instruction starts at runtime, including chained TBs, and
switches the destination counter between TCI and JIT execution. Faulting/retried
instructions are counted again; subtracting `cpu_io_recompile` yields identical
native/Wasm totals (**80,703,407 before Home; 372,247,985 afterward**). These
are instruction counts, not TB/module counts, but not an architectural retired
instruction PMU. Loads/stores count executed `qemu_ld/st` operations and C helper
entries; their difference estimates inline accesses. MMIO routes through
unnamed subpage regions as well as the named device, so unnamed routing counts
remain in raw data and are excluded from the device table.

Phase snapshots occur in the USB serial model at complete firmware lines.
Main-loop and host synchronization counts can vary with scheduling and are
reported as medians; they are not exact deterministic guest counters. Timer
instances sharing callback names are aggregated in the table. The headless
workload does not run SDL polling; panel updates still occur through the
emulated display device. Browser rendering/Chrome performance is outside this
measurement, and no claim about Chrome gains is made.

Evidence inventory (under `build-wasm/evidence/profile/`):

- `timing/{results.json,native-*,wasm-*}`: clean interleaved times, full argv,
  logs, copied images and PPMs; `timing.log` contains the summary.
- `node-cpu/`, `native-home/`, `native-settled/`: raw CPU profiles/samples,
  module events and milestone metadata; `*-analysis.json` contains categories,
  full self-symbol lists, top 30 and selected wait stacks.
- `counters-final/`, `counters.json`: final refined counters and Asyncify
  snapshots. Earlier `counters/` data are superseded and not used in tables.
- `instrument.py`, `refine-memory-counters.py`, `instrument-js.py`,
  `instrumentation.patch`, `turbo-profile.{h,c}`: exact temporary instrumentation;
  `instrument-originals/` and `restore.py`: saved sources and restoration.
- `tables.md`: regenerated by `make-report.py`; `restored-*-build.log` and
  `restored-native-configure.log`: clean-source build validation.

The measured tables follow. “Settled” column headings mean **Home → settled**,
except where a column explicitly says “Boot → settled”.

## Wall time, sleep disabled

| Runtime | Boot → Home s | Home → settled s | Boot → settled s |
| --- | ---: | ---: | ---: |
| native | 2.272 | 15.258 | 17.537 |
| wasm | 4.442 | 46.065 | 50.515 |

| Run order | Runtime | Home s | Home → settled s | Total s |
| ---: | --- | ---: | ---: | ---: |
| 1 | native | 2.235 | 15.178 | 17.413 |
| 2 | wasm | 4.442 | 46.214 | 50.655 |
| 3 | native | 2.279 | 15.258 | 17.537 |
| 4 | wasm | 4.451 | 46.065 | 50.515 |
| 5 | native | 2.272 | 15.566 | 17.838 |
| 6 | wasm | 4.422 | 45.872 | 50.294 |

Median paired Wasm/native ratios: home_s **1.953x**, home_to_settled_s **3.019x**, settled_s **2.880x**.
Raw: `timing/results.json`; order native, Wasm, repeated three times.

## vCPU sampled self cost by phase

Percentages are medians of three per-run shares; column sums can differ slightly from 100%.
Native uses stack self samples; Node uses time-delta-weighted elapsed samples, including waits.

| Category | Native Home % | Native settled % | Wasm Home % | Wasm settled % |
| --- | ---: | ---: | ---: | ---: |
| Wait | 6.25 | 17.12 | 35.68 | 50.98 |
| Helpers / softmmu / MMIO | 25.97 | 21.32 | 29.25 | 15.17 |
| Other QEMU / runtime | 42.40 | 40.94 | 12.14 | 11.41 |
| Asyncify / fibers (visible) | 0.00 | 0.00 | 3.14 | 7.09 |
| JS glue / V8 | 0.00 | 0.00 | 2.74 | 5.46 |
| Dispatch / main loop / timers / icount | 15.69 | 11.97 | 5.81 | 4.33 |
| JIT TB modules | 0.00 | 0.00 | 4.97 | 2.97 |
| Device models: hw/sd/ssi-sd.c | 0.00 | 0.08 | 0.22 | 0.62 |
| TCI / libffi | 0.00 | 0.00 | 2.34 | 0.51 |
| Device models: hw/ssi/ssi.c | 0.05 | 0.09 | 0.24 | 0.37 |
| Device models: hw/ssi/esp32s3_gpspi.c | 0.38 | 0.36 | 0.23 | 0.25 |
| Translation / lookup | 6.51 | 5.28 | 2.58 | 0.21 |
| Device models: hw/sd/sd.c | 0.00 | 0.07 | 0.08 | 0.20 |
| Device models: hw/timer/esp_systimer.c | 0.11 | 0.16 | 0.02 | 0.07 |
| Module constructors / hooks | 0.00 | 0.00 | 0.40 | 0.05 |
| Device models: hw/timer/esp32c3_systimer.c | 0.00 | 0.02 | 0.00 | 0.02 |
| Device models: hw/misc/esp32s3_usb_jtag.c | 0.00 | 0.00 | 0.00 | 0.01 |
| Device models: hw/timer/esp_timg.c | 0.00 | 0.00 | 0.02 | 0.01 |
| Device models: hw/gpio/esp32s3_gpio.c | 0.00 | 0.00 | 0.00 | 0.00 |
| Device models: hw/display/uc8279.c | 0.11 | 0.02 | 0.03 | 0.00 |
| Device models: hw/misc/esp_sha.c | 0.06 | 0.00 | 0.19 | 0.00 |
| Device models: hw/i2c/esp32s3_i2c.c | 0.00 | 0.00 | 0.00 | 0.00 |
| Device models: hw/ssi/esp32c3_spi.c | 0.00 | 0.00 | 0.00 | 0.00 |
| Device models: hw/misc/bq27220.c | 0.00 | 0.00 | 0.00 | 0.00 |
| Unresolved / native generated code | 2.48 | 2.54 | 0.00 | 0.00 |

Raw: `node-analysis.json`, `native-home-analysis.json`, `native-settled-analysis.json`.

## Counters at exact firmware phase boundaries

Medians of three instrumented runs; each snapshot resets after the Home line.
These builds are excluded from the wall-time and CPU-profile tables.

| Counter | Native Home | Native settled | Wasm Home | Wasm settled |
| --- | ---: | ---: | ---: | ---: |
| Dispatcher TB-chain entries (exits within ±1) | 14,433,651 | 72,225,364 | 274,868 | 2,881,249 |
| cpu_io_recompile exits | 14,160,646 | 69,340,109 | 1,859 | 759 |
| icount_get calls | 570,805 | 14,171,732 | 570,805 | 14,171,732 |
| BQL lock attempts | 14,390,387 | 75,244,901 | 14,395,264 | 75,381,037 |
| qemu_clock_notify calls | 42,646 | 818,692 | 42,646 | 818,692 |
| Main-loop iterations | 33,282 | 757,528 | 33,416 | 762,695 |
| Data load helper entries | 24,446,040 | 127,778,430 | 13,411,657 | 66,676,077 |
| Data store helper entries | 3,919,036 | 11,656,915 | 2,714,319 | 7,375,663 |
| Executed qemu_ld operations | 33,449,202 | 154,839,185 | 21,248,562 | 91,327,563 |
| Executed qemu_st operations | 7,133,244 | 26,160,668 | 5,175,097 | 20,332,940 |
| Native instruction starts | 94,864,053 | 441,588,094 | 0 | 0 |
| TCI instruction starts | 0 | 0 | 5,996,688 | 11,597,420 |
| JIT instruction starts | 0 | 0 | 74,708,578 | 360,651,324 |
| GPSPI transactions | 25,175 | 747,957 | 25,175 | 747,957 |
| GPSPI full-duplex byte clocks | 525,233 | 4,588,541 | 525,233 | 4,588,541 |
| SD reads / 512-byte sectors | 178 | 3,675 | 178 | 3,675 |
| SD bytes read | 91,136 | 1,881,600 | 91,136 | 1,881,600 |
| Reads contiguous with preceding read | 47 | 2,325 | 47 | 2,325 |
| Immediate same-address rereads | 0 | 0 | 0 | 0 |
| Console update calls | 1 | 0 | 1 | 0 |
| SDL event polls | 0 | 0 | 0 | 0 |
| Inline data accesses (operations − helper entries) | 12,217,370 | 41,564,508 | 10,297,683 | 37,608,763 |

Helper entry is **not** a hardware TLB-miss count: TCI uses the C helper even for RAM hits.
Instruction starts include fault/retry attempts; they count guest instructions, not TCG ops or modules.
Subtracting io_recompile counts reconciles native and Wasm instruction totals.

| Wasm phase | TCI instruction share | JIT instruction share | Asyncify unwinds | Asyncify rewinds |
| --- | ---: | ---: | ---: | ---: |
| Home | 7.430% | 92.570% | 396,808 | 396,805 |
| Home → settled | 3.116% | 96.884% | 15,137,088 | 15,137,088 |

| Phase | New JIT modules | Synchronous compile ms | Synchronous instantiate ms |
| --- | ---: | ---: | ---: |
| home | 548 | 12.768 | 2.691 |
| settled | 939 | 24.778 | 4.994 |

Constructor costs exclude V8 background optimization. Raw: `counters.json` and `node-cpu/*/modules-*.jsonl`.

## Device registers and timer traffic

Named MMIO regions only; alias recursion and unnamed subpage routing regions are excluded. Values are medians.

| Region | Native Home R/W | Native settled R/W | Wasm Home R/W | Wasm settled R/W |
| --- | ---: | ---: | ---: | ---: |
| ssi.esp32s3.gpspi | 11,966,744 / 237,635 | 60,969,941 / 4,677,452 | 11,966,744 / 237,635 | 60,969,941 / 4,677,452 |
| esp.systimer | 92,312 / 19,569 | 2,959,302 / 594,586 | 92,312 / 19,569 | 2,959,302 / 594,586 |
| misc.esp32c3.intmatrix | 28,816 / 47,966 | 181,325 / 331,355 | 28,816 / 47,966 | 181,325 / 331,355 |
| esp32c3.iomem | 17,045 / 27,647 | 35,285 / 65,168 | 17,045 / 27,647 | 35,285 / 65,168 |
| timer.esp.timg | 10,956 / 24,581 | 27,292 / 61,407 | 10,956 / 24,581 | 27,292 / 61,407 |
| misc.esp32c3.saradc | 3,272 / 2,912 | 37,206 / 33,072 | 3,272 / 2,912 | 37,206 / 33,072 |
| misc.esp32s3.ana | 2,272 / 1,894 | 24,848 / 20,705 | 2,272 / 1,894 | 24,848 / 20,705 |
| esp32s3.i2c | 1,798 / 2,155 | 16,704 / 20,160 | 1,798 / 2,155 | 16,704 / 20,160 |
| esp32s3.gpio | 849 / 5,139 | 749 / 15,930 | 849 / 5,139 | 749 / 15,930 |
| misc.esp32s3.usb_serial_jtag | 4,035 / 4,022 | 7,142 / 7,131 | 4,035 / 4,022 | 7,142 / 7,131 |
| esp32c3.soc.clk | 698 / 1,259 | 767 / 1,472 | 698 / 1,259 | 767 / 1,472 |
| misc.esp32c3.rtc_cntl | 321 / 287 | 11 / 4 | 321 / 287 | 11 / 4 |
| misc.esp32.fe | 1 / 1 | 0 / 0 | 1 / 1 | 0 / 0 |
| esp32c3.cache | 1,307 / 2,776 | 0 / 0 | 1,307 / 2,776 | 0 / 0 |
| nvram.esp.efuse | 162 / 0 | 0 / 0 | 162 / 0 | 0 / 0 |
| misc.esp32s3.regstub | 4 / 4 | 0 / 0 | 4 / 4 | 0 / 0 |
| ssi.esp32c3.spi | 7,910 / 3,252 | 0 / 0 | 7,910 / 3,252 | 0 / 0 |
| misc.esp.sha | 87,650 / 1,577,376 | 0 / 0 | 87,650 / 1,577,376 | 0 / 0 |
| esp_soc.uart | 669 / 653 | 0 / 0 | 669 / 653 | 0 / 0 |

| Wasm GPSPI register | Home reads/writes | Settled reads/writes |
| --- | ---: | ---: |
| CMD (0x00) | 11,885,506 / 50,604 | 57,912,960 / 1,501,450 |
| CLOCK (0x0c) | 254 / 256 | 5,542 / 5,539 |
| USER (unmodeled storage) (0x10) | 258 / 259 | 5,536 / 5,536 |
| MS_DLEN (0x1c) | 41,884 / 41,884 | 1,486,386 / 1,486,386 |
| W0 (0x98) | 16,709 / 25,176 | 738,429 / 747,957 |
| W1 (0x9c) | 1,424 / 9,888 | 53,632 / 63,156 |
| W2 (0xa0) | 1,424 / 9,888 | 53,632 / 63,156 |
| W3 (0xa4) | 1,424 / 9,888 | 53,632 / 63,156 |
| W4 (0xa8) | 1,424 / 9,888 | 53,632 / 63,156 |
| W5 (0xac) | 1,424 / 9,888 | 53,632 / 63,156 |
| W6 (0xb0) | 1,424 / 9,888 | 53,632 / 63,156 |
| W7 (0xb4) | 1,424 / 9,888 | 53,632 / 63,156 |
| W8 (0xb8) | 1,424 / 9,888 | 53,632 / 63,156 |
| W9 (0xbc) | 1,424 / 5,664 | 53,632 / 58,404 |
| W10 (0xc0) | 1,424 / 5,664 | 53,632 / 58,404 |
| W11 (0xc4) | 1,424 / 5,649 | 53,632 / 58,384 |
| W12 (0xc8) | 1,424 / 5,649 | 53,632 / 58,384 |
| W13 (0xcc) | 1,424 / 5,649 | 53,632 / 58,384 |
| W14 (0xd0) | 1,424 / 5,649 | 53,632 / 58,384 |
| W15 (0xd4) | 1,424 / 5,649 | 53,632 / 58,384 |

| Timer callback | Native Home mod/del | Native settled mod/del | Wasm Home mod/del | Wasm settled mod/del |
| --- | ---: | ---: | ---: | ---: |
| esp32c3_rtc_sleep_timer_cb | 0 / 2 | 0 / 0 | 0 / 2 | 0 / 0 |
| esp_efuse_timer_cb | 0 / 1 | 0 / 0 | 0 / 1 | 0 / 0 |
| esp_systimer_cb | 2,724 / 4 | 6,823 / 0 | 2,724 / 4 | 6,823 / 0 |
| esp_t0_cb | 0 / 4 | 0 / 0 | 0 / 4 | 0 / 0 |
| esp_wdt_cb | 8,170 / 4 | 20,469 / 0 | 8,170 / 4 | 20,469 / 0 |
| gpspi_transfer | 25,175 / 1 | 747,957 / 0 | 25,175 / 1 | 747,957 / 0 |
| keys_boot_release | 1 / 0 | 0 / 0 | 1 / 0 | 0 / 0 |
| riscv_itrigger_timer_cb | 0 / 4 | 0 / 0 | 0 / 4 | 0 / 0 |
| uart_rx_timeout_timer_cb | 0 / 1 | 0 / 0 | 0 / 1 | 0 / 0 |
| uart_throttle_timer_cb | 0 / 2 | 0 / 0 | 0 / 2 | 0 / 0 |
| uc8279_busy_done | 4 / 0 | 4 / 0 | 4 / 0 | 4 / 0 |
| unknown | 0 / 4 | 0 / 0 | 0 / 4 | 0 / 0 |
| usb_jtag_sof | 3,252 / 0 | 6,823 / 0 | 3,252 / 0 | 6,823 / 0 |

Timers with identical callback names are combined; raw snapshots retain individual timer instances.
`timer_mod_ns` and `timer_mod_anticipate_ns` are calls, not necessarily changes in expiry;
`timer_del` counts explicit public calls, not internal unlinking by every modification.

## GPSPI callback host duration

Instrumented inclusive elapsed time, including nested SD/block I/O and host scheduling.
It is not exclusive model CPU time; clock reads and instrumentation perturb it.

| Runtime / phase | Total callback s | Mean µs/transaction | Transactions / SD sector | SPI byte clocks / SD sector |
| --- | ---: | ---: | ---: | ---: |
| native Home | 0.046 | 1.820 | 141.43 | 2950.75 |
| native settled | 0.962 | 1.287 | 203.53 | 1248.58 |
| wasm Home | 0.359 | 14.256 | 141.43 | 2950.75 |
| wasm settled | 11.603 | 15.512 | 203.53 | 1248.58 |

Ratios divide all SPI traffic in the phase by SD sectors; they include protocol polling and any other SSI traffic.

## Top 30 vCPU self symbols per phase

Ranked by mean per-run self share. Self columns are mean raw samples for native and mean sampled elapsed ms for Node.
They are statistical attribution, not exact OS CPU accounting.

### Native Home

| Symbol | Mean self | Mean self % | Category |
| --- | ---: | ---: | --- |
| `pthread_jit_write_protect_np` | 171.67 | 9.44 | Other QEMU / runtime |
| `g_tree_find_node` | 120.33 | 6.62 | Other QEMU / runtime |
| `__psynch_mutexwait` | 80.00 | 4.40 | Wait |
| `tb_tc_cmp` | 79.67 | 4.38 | Translation / lookup |
| `access_with_adjusted_size` | 60.67 | 3.34 | Helpers / softmmu / MMIO |
| `cpu_exec_setjmp` | 60.00 | 3.30 | Dispatch / main loop / timers / icount |
| `cpu_unwind_data_from_tb` | 58.33 | 3.21 | Dispatch / main loop / timers / icount |
| `get_bql_locked` | 58.33 | 3.21 | Other QEMU / runtime |
| `_tlv_get_addr` | 49.67 | 2.73 | Other QEMU / runtime |
| `???` | 45.33 | 2.49 | Unresolved / native generated code |
| `mmu_lookup1` | 44.33 | 2.44 | Helpers / softmmu / MMIO |
| `memory_region_access_valid` | 42.33 | 2.33 | Helpers / softmmu / MMIO |
| `cpu_exec_loop` | 41.00 | 2.25 | Dispatch / main loop / timers / icount |
| `riscv_get_tb_cpu_state` | 38.67 | 2.13 | Dispatch / main loop / timers / icount |
| `flatview_translate` | 33.67 | 1.85 | Helpers / softmmu / MMIO |
| `memory_region_dispatch_read` | 32.33 | 1.78 | Helpers / softmmu / MMIO |
| `address_space_translate_internal` | 30.67 | 1.69 | Helpers / softmmu / MMIO |
| `pthread_mutex_lock` | 30.33 | 1.67 | Other QEMU / runtime |
| `flatview_read_continue_step` | 29.33 | 1.61 | Helpers / softmmu / MMIO |
| `do_ld4_mmu` | 29.00 | 1.60 | Helpers / softmmu / MMIO |
| `write` | 28.00 | 1.54 | Other QEMU / runtime |
| `qemu_mutex_unlock_impl` | 25.67 | 1.41 | Other QEMU / runtime |
| `do_ld_mmio_beN` | 25.00 | 1.37 | Helpers / softmmu / MMIO |
| `tcg_tb_lookup` | 24.67 | 1.36 | Translation / lookup |
| `mmu_lookup` | 24.33 | 1.34 | Helpers / softmmu / MMIO |
| `pthread_mutex_unlock` | 24.33 | 1.34 | Other QEMU / runtime |
| `flatview_access_valid` | 21.00 | 1.15 | Helpers / softmmu / MMIO |
| `object_dynamic_cast_assert` | 21.00 | 1.15 | Other QEMU / runtime |
| `__psynch_cvwait` | 19.33 | 1.06 | Wait |
| `memory_region_read_with_attrs_accessor` | 18.67 | 1.03 | Helpers / softmmu / MMIO |

### Native Home → settled

| Symbol | Mean self | Mean self % | Category |
| --- | ---: | ---: | --- |
| `__psynch_mutexwait` | 2095.67 | 16.22 | Wait |
| `pthread_jit_write_protect_np` | 1017.33 | 7.87 | Other QEMU / runtime |
| `g_tree_find_node` | 697.67 | 5.40 | Other QEMU / runtime |
| `write` | 512.00 | 3.96 | Other QEMU / runtime |
| `tb_tc_cmp` | 506.00 | 3.92 | Translation / lookup |
| `access_with_adjusted_size` | 344.33 | 2.66 | Helpers / softmmu / MMIO |
| `???` | 332.00 | 2.57 | Unresolved / native generated code |
| `get_bql_locked` | 319.33 | 2.47 | Other QEMU / runtime |
| `cpu_unwind_data_from_tb` | 313.00 | 2.42 | Dispatch / main loop / timers / icount |
| `cpu_exec_setjmp` | 295.00 | 2.28 | Dispatch / main loop / timers / icount |
| `_tlv_get_addr` | 260.67 | 2.02 | Other QEMU / runtime |
| `memory_region_access_valid` | 258.67 | 2.00 | Helpers / softmmu / MMIO |
| `mmu_lookup1` | 243.67 | 1.89 | Helpers / softmmu / MMIO |
| `flatview_translate` | 221.67 | 1.72 | Helpers / softmmu / MMIO |
| `__psynch_mutexdrop` | 217.00 | 1.68 | Other QEMU / runtime |
| `riscv_get_tb_cpu_state` | 214.67 | 1.66 | Dispatch / main loop / timers / icount |
| `pthread_mutex_lock` | 202.33 | 1.57 | Other QEMU / runtime |
| `address_space_translate_internal` | 201.67 | 1.56 | Helpers / softmmu / MMIO |
| `cpu_exec_loop` | 195.67 | 1.52 | Dispatch / main loop / timers / icount |
| `memory_region_dispatch_read` | 195.00 | 1.51 | Helpers / softmmu / MMIO |
| `do_ld4_mmu` | 156.00 | 1.21 | Helpers / softmmu / MMIO |
| `qemu_mutex_unlock_impl` | 149.33 | 1.16 | Other QEMU / runtime |
| `mmu_lookup` | 147.00 | 1.14 | Helpers / softmmu / MMIO |
| `flatview_access_valid` | 144.00 | 1.11 | Helpers / softmmu / MMIO |
| `flatview_read_continue_step` | 140.33 | 1.09 | Helpers / softmmu / MMIO |
| `pthread_mutex_unlock` | 140.33 | 1.09 | Other QEMU / runtime |
| `do_ld_mmio_beN` | 135.67 | 1.05 | Helpers / softmmu / MMIO |
| `object_dynamic_cast_assert` | 124.67 | 0.96 | Other QEMU / runtime |
| `tcg_tb_lookup` | 113.33 | 0.88 | Translation / lookup |
| `memory_region_read_with_attrs_accessor` | 113.00 | 0.87 | Helpers / softmmu / MMIO |

### Wasm Home

| Symbol | Mean self | Mean self % | Category |
| --- | ---: | ---: | --- |
| `_do_futex_wait` | 1534.38 | 33.97 | Wait |
| `TB entry (generated modules)` | 224.41 | 4.97 | JIT TB modules |
| `cpu_tb_exec` | 202.45 | 4.48 | Dispatch / main loop / timers / icount |
| `access_with_adjusted_size` | 135.69 | 3.00 | Helpers / softmmu / MMIO |
| `memory_region_access_valid` | 131.76 | 2.92 | Helpers / softmmu / MMIO |
| `memory_region_dispatch_read` | 119.71 | 2.65 | Helpers / softmmu / MMIO |
| `flatview_read_continue_step` | 115.97 | 2.57 | Helpers / softmmu / MMIO |
| `flatview_translate` | 106.60 | 2.36 | Helpers / softmmu / MMIO |
| `tcg_qemu_tb_exec_tci` | 95.16 | 2.11 | TCI / libffi |
| `(idle)` | 75.68 | 1.68 | Wait |
| `flatview_access_valid` | 75.06 | 1.66 | Helpers / softmmu / MMIO |
| `do_ld4_mmu` | 70.18 | 1.55 | Helpers / softmmu / MMIO |
| `do_ld_mmio_beN` | 62.78 | 1.39 | Helpers / softmmu / MMIO |
| `address_space_translate_internal` | 62.34 | 1.38 | Helpers / softmmu / MMIO |
| `saveRewindArguments` | 59.92 | 1.33 | Asyncify / fibers (visible) |
| `wasm-to-js` | 57.17 | 1.27 | Other QEMU / runtime |
| `do_proxy` | 53.02 | 1.17 | Other QEMU / runtime |
| `mmu_lookup` | 52.96 | 1.17 | Helpers / softmmu / MMIO |
| `mmu_lookup1` | 52.37 | 1.16 | Helpers / softmmu / MMIO |
| `anonymous JS` | 48.61 | 1.08 | JS glue / V8 |
| `bigintToI53Checked` | 47.86 | 1.06 | JS glue / V8 |
| `flatview_read` | 44.41 | 0.98 | Helpers / softmmu / MMIO |
| `memory_region_read_accessor` | 43.12 | 0.95 | Helpers / softmmu / MMIO |
| `tb_gen_code` | 37.51 | 0.83 | Translation / lookup |
| `memory_region_read_with_attrs_accessor` | 37.20 | 0.82 | Helpers / softmmu / MMIO |
| `bql_lock_impl` | 33.58 | 0.74 | Other QEMU / runtime |
| `subpage_read` | 31.68 | 0.70 | Helpers / softmmu / MMIO |
| `qemu_mutex_lock_impl` | 30.45 | 0.67 | Other QEMU / runtime |
| `qemu_mutex_unlock_impl` | 30.06 | 0.67 | Other QEMU / runtime |
| `liveness_pass_1` | 26.20 | 0.58 | Translation / lookup |

### Wasm Home → settled

| Symbol | Mean self | Mean self % | Category |
| --- | ---: | ---: | --- |
| `_do_futex_wait` | 23998.17 | 51.01 | Wait |
| `TB entry (generated modules)` | 1427.71 | 3.03 | JIT TB modules |
| `saveRewindArguments` | 1185.27 | 2.52 | Asyncify / fibers (visible) |
| `anonymous JS` | 1109.04 | 2.36 | JS glue / V8 |
| `cpu_tb_exec` | 1013.71 | 2.15 | Dispatch / main loop / timers / icount |
| `do_proxy` | 850.74 | 1.81 | Other QEMU / runtime |
| `memory_region_dispatch_read` | 748.59 | 1.59 | Helpers / softmmu / MMIO |
| `bigintToI53Checked` | 738.81 | 1.57 | JS glue / V8 |
| `access_with_adjusted_size` | 712.61 | 1.51 | Helpers / softmmu / MMIO |
| `wrapper` | 686.22 | 1.46 | Asyncify / fibers (visible) |
| `memory_region_access_valid` | 678.38 | 1.44 | Helpers / softmmu / MMIO |
| `flatview_read_continue_step` | 675.38 | 1.44 | Helpers / softmmu / MMIO |
| `flatview_translate` | 590.86 | 1.26 | Helpers / softmmu / MMIO |
| `wasm-to-js` | 576.34 | 1.23 | Other QEMU / runtime |
| `do_ld4_mmu` | 499.17 | 1.06 | Helpers / softmmu / MMIO |
| `rr_cpu_thread_fn` | 434.56 | 0.92 | Dispatch / main loop / timers / icount |
| `flatview_access_valid` | 396.10 | 0.84 | Helpers / softmmu / MMIO |
| `do_ld_mmio_beN` | 370.02 | 0.79 | Helpers / softmmu / MMIO |
| `finishContextSwitch` | 365.00 | 0.78 | Asyncify / fibers (visible) |
| `flatview_read` | 364.08 | 0.77 | Helpers / softmmu / MMIO |
| `(program)` | 336.93 | 0.72 | Other QEMU / runtime |
| `doRewind` | 308.05 | 0.66 | Asyncify / fibers (visible) |
| `address_space_translate_internal` | 294.48 | 0.63 | Helpers / softmmu / MMIO |
| `ssi_sd_transfer` | 289.85 | 0.62 | Device models: hw/sd/ssi-sd.c |
| `maybeStopUnwind` | 284.42 | 0.60 | Asyncify / fibers (visible) |
| `mmu_lookup` | 274.52 | 0.58 | Helpers / softmmu / MMIO |
| `trampoline` | 273.96 | 0.58 | Asyncify / fibers (visible) |
| `emscripten_futex_wake` | 268.07 | 0.57 | Other QEMU / runtime |
| `mmu_lookup1` | 257.60 | 0.55 | Helpers / softmmu / MMIO |
| `memory_region_read_with_attrs_accessor` | 243.53 | 0.52 | Helpers / softmmu / MMIO |
