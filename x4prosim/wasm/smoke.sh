#!/usr/bin/env bash
# smoke.sh flash.bin sd.img or --rom-only (CI without firmware).
# Optional TIMEOUT, EVIDENCE_DIR, NODE, WASM64_MODE, NATIVE_QEMU, REFERENCE_PPM.
# RUN_NODE overrides the launcher for comparisons; REQUIRE_JIT=0 permits TCI.
# Images are copied; logs, PPM screenshots and measurements remain in evidence/.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
exec python3 - "$ROOT" "$@" <<'PY'
import json
import os
from pathlib import Path
import re
import selectors
import shutil
import subprocess
import sys
import time

root = Path(sys.argv[1])
rom_only = sys.argv[2:] == ['--rom-only']
if not rom_only and len(sys.argv) != 4:
    sys.exit('usage: smoke.sh flash.bin sd.img | --rom-only')
evidence = Path(os.environ.get('EVIDENCE_DIR', root / 'build-wasm/evidence/smoke')).resolve()
evidence.mkdir(parents=True, exist_ok=True)
if rom_only:
    subprocess.run([sys.executable, str(root / 'x4prosim/mksd.py'),
                    str(evidence / 'sd.img'), '64'], check=True)
else:
    for src, dest in zip(sys.argv[2:], ('flash.bin', 'sd.img')):
        shutil.copyfile(src, evidence / dest)
(evidence / 'blank.bin').write_bytes(bytes(16 * 1024 * 1024))
timeout = float(os.environ.get('TIMEOUT', '600'))
# Native CrossPoint reaches Home by the third completed panel refresh.
# The optional [HOME] thumbnail warning disappears once its cache exists.
milestone = re.compile(os.environ.get('HOME_MILESTONE',
    r'Wait complete:\s+(?:8279|X3)_DRF'))
milestone_count = int(os.environ.get('HOME_MILESTONE_COUNT', '3'))
measurements = {}


def run(machine, flash):
    log_path = evidence / (machine + '.log')
    shot = evidence / (machine + '.ppm')
    shot.unlink(missing_ok=True)
    native = os.environ.get('NATIVE_QEMU')
    require_jit = not native and os.environ.get('REQUIRE_JIT', '1') != '0'
    if native:
        arch = 'riscv32' if machine == 'x3' else 'xtensa'
        command = [str(Path(native) / ('qemu-system-' + arch)),
            '-L', str(root / 'pc-bios'), '-machine', machine,
            '-icount', 'shift=' + ('0' if machine == 'x3' else '2') + ',sleep=on',
            '-drive', f'file={str(flash).replace(",", ",,")},if=mtd,format=raw',
            '-drive', f'file={str(evidence / "sd.img").replace(",", ",,")},if=sd,format=raw',
            '-chardev', 'stdio,id=cdc,mux=on', '-serial', 'null',
            '-global', 'driver=misc.esp32s3.usb_serial_jtag,property=chardev,value=cdc',
            '-mon', 'chardev=cdc,mode=readline', '-display', 'none', '-nic', 'none']
    else:
        command = [os.environ.get('RUN_NODE', str(root / 'x4prosim/wasm/run-node.sh')),
                   str(flash), str(evidence / 'sd.img')]
    start = time.monotonic()
    p = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, env={**os.environ, 'X4MACHINE': machine, 'WASM_JIT_STATS': '1'})
    sel = selectors.DefaultSelector()
    sel.register(p.stdout, selectors.EVENT_READ)
    output = bytearray()
    boot = None
    ready = None
    requested = False
    settled_time = None
    home_compiled = 0
    max_rss = 0
    sample_at = start
    try:
        with log_path.open('wb') as log:
            while time.monotonic() - start < timeout:
                now = time.monotonic()
                if now >= sample_at:
                    rss = subprocess.run(['ps', '-o', 'rss=', '-p', str(p.pid)],
                                         capture_output=True, text=True)
                    if rss.returncode == 0 and rss.stdout.strip():
                        max_rss = max(max_rss, int(rss.stdout.strip()))
                    sample_at = now + 1
                for key, _ in sel.select(.05):
                    chunk = os.read(key.fd, 65536)
                    if chunk:
                        log.write(chunk)
                        log.flush()
                        output.extend(chunk)
                text = output.decode(errors='replace')
                if any(failure in text for failure in ('Guru Meditation', 'RuntimeError:', 'Aborted(')):
                    raise RuntimeError(f'{machine}: guest or runtime crash; see {log_path}')
                if boot is None and 'ESP-ROM:' in text:
                    boot = time.monotonic() - start
                if ready is None:
                    matched = (len(milestone.findall(text)) >= milestone_count
                               if machine == 'x3' else
                               text.count('invalid header: 0x00000000') >= 2)
                    if matched:
                        ready = time.monotonic() - start
                        counts = re.findall(r'WASM_JIT compiled_tbs=(\d+)', text)
                        home_compiled = int(counts[-1]) if counts else 0
                        print(f'{machine}: milestone at {ready:.3f} s', flush=True)
                # Allow the panel update / main loop to settle, then capture it.
                matches = list(milestone.finditer(text))
                settled = (ready is not None and re.search(r'\[MEM\][^\n]*\n', text[matches[milestone_count - 1].end():])
                           if machine == 'x3' else
                           ready is not None and now - start > ready + 2)
                if ready is not None and not requested and settled:
                    settled_time = time.monotonic() - start
                    guest_shot = str(shot) if native else '/host' + str(shot)
                    p.stdin.write(f'\x01cscreendump "{guest_shot}" panel\n'.encode())
                    p.stdin.flush()
                    requested = True
                if shot.exists() and shot.stat().st_size > 1000:
                    header = shot.read_bytes().split(b'\n', 3)
                    if header[0] != b'P6':
                        raise RuntimeError('screendump is not a binary PPM')
                    width, height = map(int, header[1].split())
                    if len(header[3]) != width * height * 3:
                        continue
                    expected = (528, 792) if machine == 'x3' else (480, 800)
                    if (width, height) != expected:
                        raise RuntimeError(f'Unexpected panel size: {width}x{height}')
                    if machine == 'x3' and len(set(header[3])) < 2:
                        raise RuntimeError('X3 panel is blank')
                    counts = re.findall(r'WASM_JIT compiled_tbs=(\d+)', text)
                    compiled = int(counts[-1]) if counts else 0
                    if require_jit and machine == 'x3' and compiled == 0:
                        raise RuntimeError('Home reached without any JIT-compiled TBs')
                    reference = os.environ.get('REFERENCE_PPM')
                    if machine == 'x3' and reference and shot.read_bytes() != Path(reference).read_bytes():
                        raise RuntimeError(f'Panel differs from native reference {reference}')
                    screenshot_time = time.monotonic() - start
                    # Like drive.py, stop the emulator after capturing evidence.
                    # The guest remains running through the screenshot checkpoint.
                    if p.poll() is not None:
                        raise RuntimeError(f'Node exited {p.returncode}')
                    p.terminate()
                    deadline = time.monotonic() + 10
                    while True:
                        waited, status, usage = os.wait4(p.pid, os.WNOHANG)
                        if waited:
                            p.returncode = os.waitstatus_to_exitcode(status)
                            break
                        if time.monotonic() > deadline:
                            p.kill()
                        time.sleep(.01)
                    peak_rss = usage.ru_maxrss / (1024 if sys.platform == 'darwin' else 1)
                    measurements[machine] = dict(startup_to_rom_s=boot,
                        milestone_s=ready, settled_mem_s=settled_time, screenshot_s=screenshot_time,
                        jit_compiled_tbs=compiled, home_jit_compiled_tbs=home_compiled,
                        peak_rss_kib=peak_rss,
                        sampled_peak_rss_kib=max_rss,
                        screenshot=str(shot), log=str(log_path))
                    print(f'{machine}: PASS {shot}', flush=True)
                    return
                if p.poll() is not None:
                    raise RuntimeError(f'Node exited {p.returncode}: {text[-2000:]}')
            raise RuntimeError(f'{machine}: timed out after {timeout} s; see {log_path}')
    finally:
        if p.poll() is None:
            p.kill()
        p.wait()
        sel.close()
        p.stdin.close()
        p.stdout.close()


try:
    if not rom_only:
        run('x3', evidence / 'flash.bin')
    run('x4pro', evidence / 'blank.bin')
except Exception as exc:
    sys.exit(str(exc))
(evidence / 'measurements.json').write_text(json.dumps(measurements, indent=2) + '\n')
print('PASS: X4 Pro blank-flash ROM and panel screendump' if rom_only else
      'PASS: X3 Home refresh and X4 Pro blank-flash ROM; both panel screendumps captured')
PY
