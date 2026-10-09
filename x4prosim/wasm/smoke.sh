#!/usr/bin/env bash
# CI: smoke.sh flash.bin sd.img; optional TIMEOUT, EVIDENCE_DIR, NODE.
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
if len(sys.argv) != 4:
    sys.exit('usage: smoke.sh flash.bin sd.img')
evidence = Path(os.environ.get('EVIDENCE_DIR', root / 'build-wasm/evidence/smoke')).resolve()
evidence.mkdir(parents=True, exist_ok=True)
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
    command = [str(root / 'x4prosim/wasm/run-node.sh'), str(flash), str(evidence / 'sd.img')]
    start = time.monotonic()
    p = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, env={**os.environ, 'X4MACHINE': machine})
    sel = selectors.DefaultSelector()
    sel.register(p.stdout, selectors.EVENT_READ)
    output = bytearray()
    boot = None
    ready = None
    requested = False
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
                        print(f'{machine}: milestone at {ready:.3f} s', flush=True)
                # Allow the panel update / main loop to settle, then capture it.
                settled = ('[MEM]' in text if machine == 'x3' else
                           ready is not None and now - start > ready + 2)
                if ready is not None and not requested and settled:
                    p.stdin.write(f'\x01cscreendump "/host{shot}" panel\n'.encode())
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
                    screenshot_time = time.monotonic() - start
                    # Like drive.py, stop the emulator after capturing evidence.
                    # The guest remains running through the screenshot checkpoint.
                    if p.poll() is not None:
                        raise RuntimeError(f'Node exited {p.returncode}')
                    p.terminate()
                    p.wait(10)
                    measurements[machine] = dict(startup_to_rom_s=boot,
                        milestone_s=ready, screenshot_s=screenshot_time,
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
    run('x3', evidence / 'flash.bin')
    run('x4pro', evidence / 'blank.bin')
except Exception as exc:
    sys.exit(str(exc))
(evidence / 'measurements.json').write_text(json.dumps(measurements, indent=2) + '\n')
print('PASS: X3 Home refresh and X4 Pro blank-flash ROM; both panel screendumps captured')
PY
