'use strict';
const $ = id => document.getElementById(id);
const params = new URLSearchParams(location.search);
// Turbo is the default; ?turbo=0 selects the timing-accurate mode.
$('turbo').checked = params.get('turbo') !== '0';
let frame, bootData, savedSD, pending = false;
let milestones = 0, startedAt;
const held = new Set();
const bindings = {Back: 'Backspace', Confirm: 'Enter', Left: 'ArrowLeft',
    Right: 'ArrowRight', Up: 'ArrowUp', Down: 'ArrowDown', Power: 'KeyP'};
const post = (data, transfer = []) => frame?.contentWindow.postMessage(data, location.origin, transfer);
const status = text => { $('status').textContent = text; };
function append(line) {
    $('console').textContent += line + '\n';
    if ($('console').textContent.length > 1000000) {
        $('console').textContent = $('console').textContent.slice(-800000);
    }
    $('console').scrollTop = $('console').scrollHeight;
    if (/Wait complete:\s+(?:8279|X3)_DRF/.test(line) && ++milestones === 3) {
        const seconds = (performance.now() - startedAt) / 1000;
        status(`X3 · Home drawn in ${seconds.toFixed(2)} s · finishing startup…`);
        window.bootTimeSeconds = seconds;
    }
    if (line.includes('[MEM]') && window.bootTimeSeconds) {
        status(`X3 · Ready · Home drawn in ${window.bootTimeSeconds.toFixed(2)} s`);
    }
}
function key(code, down) {
    if (!frame || !Object.values(bindings).concat('Escape').includes(code)) return;
    if (down === held.has(code)) return;
    if (down) held.add(code); else held.delete(code);
    post({type: 'key', code, down});
}
function release() { for (const code of held) key(code, false); }
for (const type of ['keydown', 'keyup']) document.addEventListener(type, event => {
    if (!frame || event.target.tagName === 'INPUT' || event.ctrlKey || event.metaKey || event.altKey) return;
    if (Object.values(bindings).concat('Escape').includes(event.code)) {
        event.preventDefault(); key(event.code, type === 'keydown');
    }
});
window.addEventListener('blur', release);
function buttons(machine) {
    $('keys').replaceChildren();
    for (const name of machine === 'x3' ? Object.keys(bindings) : ['Up', 'Down', 'Power']) {
        const button = document.createElement('button');
        button.textContent = name;
        button.addEventListener('pointerdown', event => {
            event.preventDefault(); button.setPointerCapture(event.pointerId);
            button.classList.add('held'); key(bindings[name], true);
        });
        for (const type of ['pointerup', 'pointercancel', 'lostpointercapture']) {
            button.addEventListener(type, () => {
                button.classList.remove('held'); key(bindings[name], false);
            });
        }
        // Keyboard and assistive-technology activation.
        button.addEventListener('click', event => {
            if (event.detail === 0) {
                key(bindings[name], true);
                setTimeout(() => key(bindings[name], false), 150);
            }
        });
        $('keys').append(button);
    }
}
async function fetchImage(url) {
    const response = await fetch(url);
    if (!response.ok) throw new Error(`Cannot load ${url}: HTTP ${response.status}`);
    return response.arrayBuffer();
}
async function start() {
    if (frame || pending) return;
    pending = true;
    startedAt = performance.now(); milestones = 0; window.bootTimeSeconds = null;
    $('start').disabled = true; $('console').textContent = '';
    for (const id of ['flash', 'sd', 'turbo', 'reset', 'stop', 'download']) $(id).disabled = true;
    try {
        if (!crossOriginIsolated) throw new Error('SharedArrayBuffer needs isolation headers. Serve this folder with serve.py on localhost or HTTPS.');
        status('Loading images…');
        const flash = $('flash').files[0] ? await $('flash').files[0].arrayBuffer() :
            params.has('flash') ? await fetchImage(params.get('flash')) : null;
        if (!flash) throw new Error('Choose a 16 MiB flash image.');
        if (flash.byteLength !== 16 * 1024 * 1024) throw new Error('Flash image must be exactly 16 MiB (16777216 bytes).');
        const machine = new Uint8Array(flash)[12] === 5 ? 'x3' : 'x4pro';
        const [sd, rom] = await Promise.all([
            savedSD || ($('sd').files[0] ? $('sd').files[0].arrayBuffer() : fetchImage(params.get('sd') || 'blank-sd.img')),
            fetchImage(machine === 'x3' ? 'esp32c3-rom.bin' : 'esp32s3_rev0_rom.bin')
        ]);
        if (!sd.byteLength || sd.byteLength % 512) throw new Error('SD image must contain a whole number of 512-byte sectors.');
        bootData = {type: 'boot', machine, flash, sd, rom, turbo: $('turbo').checked};
        buttons(machine);
        $('screen').style.aspectRatio = machine === 'x3' ? '528 / 792' : '480 / 800';
        frame = document.createElement('iframe');
        frame.title = `${machine === 'x3' ? 'X3' : 'X4 Pro'} display`;
        frame.src = 'runtime.html';
        $('screen').replaceChildren(frame);
        status(`Booting ${machine === 'x3' ? 'X3 (ESP32-C3)' : 'X4 Pro (ESP32-S3)'}…`);
    } catch (error) { fail(error.message); }
    finally { pending = false; }
}
function fail(message) {
    status(message); append(`Error: ${message}`);
    if (!frame) {
        for (const id of ['start', 'flash', 'sd', 'turbo']) $(id).disabled = false;
        $('download').disabled = !savedSD;
    }
}
window.addEventListener('message', ({source, origin, data}) => {
    if (!frame || source !== frame.contentWindow || origin !== location.origin) return;
    if (data.type === 'ready') {
        const data = bootData; bootData = null;
        post(data, [data.flash, data.sd, data.rom]);
        savedSD = null;
    }
    if (data.type === 'log') append(data.line);
    if (data.type === 'error') fail(data.message);
    if (data.type === 'exit') status(`Emulator exited (${data.status}).`);
    if (data.type === 'running') {
        for (const id of ['reset', 'stop', 'download']) $(id).disabled = false;
    }
    if (data.type === 'snapshot') {
        savedSD = data.bytes.buffer;
        if (data.action === 'download') {
            const url = URL.createObjectURL(new Blob([data.bytes], {type: 'application/octet-stream'}));
            const link = document.createElement('a');
            link.href = url; link.download = 'sd.img'; link.click();
            setTimeout(() => URL.revokeObjectURL(url), 1000);
            for (const id of ['download', 'reset', 'stop']) $(id).disabled = false;
        } else {
            release(); frame.remove(); frame = null;
            $('screen').textContent = 'Stopped. The SD image is kept until you close this page.';
            $('keys').replaceChildren();
            for (const id of ['reset', 'stop']) $(id).disabled = true;
            for (const id of ['start', 'flash', 'sd', 'turbo', 'download']) $(id).disabled = false;
            status('Stopped. Start resumes from a fresh boot with your current SD image.');
        }
    }
});
$('start').addEventListener('click', start);
$('reset').addEventListener('click', () => {
    release(); milestones = 0; startedAt = performance.now(); window.bootTimeSeconds = null;
    post({type: 'reset'}); status('Resetting…');
});
$('stop').addEventListener('click', () => {
    for (const id of ['reset', 'stop', 'download']) $(id).disabled = true;
    release(); post({type: 'stop'});
});
$('download').addEventListener('click', () => {
    if (frame) {
        for (const id of ['reset', 'stop', 'download']) $(id).disabled = true;
        release(); post({type: 'download'});
    }
    else if (savedSD) {
        const url = URL.createObjectURL(new Blob([savedSD]));
        const link = document.createElement('a'); link.href = url; link.download = 'sd.img'; link.click();
        setTimeout(() => URL.revokeObjectURL(url), 1000);
    }
});
$('sd').addEventListener('change', () => { savedSD = null; });
if (params.has('flash')) start();
