'use strict';
const $ = id => document.getElementById(id);
const params = new URLSearchParams(location.search);
// Accurate timing is the default; only ?turbo=1 opts into turbo.
$('turbo').checked = params.get('turbo') === '1';
$('wifi').checked = params.get('wifi') !== 'off';
let frame, bootData, pending = false, running = false;
// The page owns the SD image while the emulator is not running; the runtime
// iframe owns it (MEMFS /sd.img) from boot until the stop snapshot returns it.
let sdImage = null, sdLoading = null;
let sdSaving = 0, sdSnapshotPending = false;
let milestones = 0, startedAt;
const held = new Set();
const bindings = {Back: 'Backspace', Confirm: 'Enter', Left: 'ArrowLeft',
    Right: 'ArrowRight', Up: 'ArrowUp', Down: 'ArrowDown', Power: 'KeyP', Home: 'Home'};
const post = (data, transfer = []) => frame?.contentWindow.postMessage(data, location.origin, transfer);
const status = text => { $('status').textContent = text; };
const timingLabel = () => $('turbo').checked ? 'Turbo (not timing-accurate)' : 'Accurate timing';
function resizeScreen() {
    if (!frame) return;
    const dpr = window.devicePixelRatio || 1;
    const width = Number(frame.width), height = Number(frame.height);
    const available = document.querySelector('.device').clientWidth - 18;
    // Keep SDL's viewport at the panel resolution. Resizing it to the layout
    // resamples the framebuffer before the browser ever draws the canvas.
    // Prefer native CSS size, fitting smaller screens in whole physical pixels.
    // If even 1x cannot fit, the device section scrolls instead of losing pixels.
    const pixels = Math.max(1, Math.min(Math.floor(dpr), Math.floor(available * dpr / width)));
    const scale = pixels / dpr;
    $('screen').style.width = `${width * scale}px`;
    $('screen').style.height = `${height * scale}px`;
    frame.style.transform = `scale(${scale})`;
}
new ResizeObserver(resizeScreen).observe(document.querySelector('.device'));
window.addEventListener('resize', resizeScreen);
function watchPixelRatio() {
    // Moving between monitors can change DPR without changing the layout width.
    matchMedia(`(resolution: ${window.devicePixelRatio}dppx)`).addEventListener('change', () => {
        resizeScreen(); watchPixelRatio();
    }, {once: true});
}
watchPixelRatio();
function append(line) {
    $('console').textContent += line + '\n';
    if ($('console').textContent.length > 1000000) {
        $('console').textContent = $('console').textContent.slice(-800000);
    }
    $('console').scrollTop = $('console').scrollHeight;
    if (/Wait complete:\s+(?:8179|8279|X3)_DRF/.test(line) && ++milestones === 3) {
        const seconds = (performance.now() - startedAt) / 1000;
        status(`${/8179_DRF/.test(line) ? 'X4 Pro' : 'X3'} · ${timingLabel()} · Home drawn in ${seconds.toFixed(2)} s · finishing startup…`);
        window.bootTimeSeconds = seconds;
    }
    if (line.includes('[MEM]') && window.bootTimeSeconds) {
        status(`${frame?.title.startsWith('X4 Pro') ? 'X4 Pro' : 'X3'} · ${timingLabel()} · Ready · Home drawn in ${window.bootTimeSeconds.toFixed(2)} s`);
    }
}
function resetting(prefix = '') {
    milestones = 0; startedAt = performance.now(); window.bootTimeSeconds = null;
    status(`${prefix}Resetting…`);
}
function key(code, down) {
    if (!frame || !Object.values(bindings).concat('Escape').includes(code)) return;
    if (down === held.has(code)) return;
    if (down) held.add(code); else held.delete(code);
    post({type: 'key', code, down});
}
function release() { for (const code of held) key(code, false); }
for (const type of ['keydown', 'keyup']) document.addEventListener(type, event => {
    if (!frame || event.target.tagName === 'INPUT' || event.target.closest('.sd') ||
        event.ctrlKey || event.metaKey || event.altKey) return;
    if (Object.values(bindings).concat('Escape').includes(event.code)) {
        event.preventDefault(); key(event.code, type === 'keydown');
    }
});
window.addEventListener('blur', release);
function buttons(machine) {
    $('keys').replaceChildren();
    for (const name of machine === 'x3' ? Object.keys(bindings).filter(name => name !== 'Home') : ['Home', 'Up', 'Down', 'Power']) {
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
function saveBlob(bytes, name) {
    const url = URL.createObjectURL(new Blob([bytes], {type: 'application/octet-stream'}));
    const link = document.createElement('a');
    link.href = url; link.download = name; link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
}
async function prepareFlash(input) {
    const bytes = new Uint8Array(input);
    if (bytes.length === 0x1000000) {
        return {flash: input, machine: bytes[12] === 5 ? 'x3' : 'x4pro', composed: false};
    }
    if (bytes.length < 24 || bytes[0] !== 0xe9) {
        throw new Error('Choose an official CrossPoint release .bin or a 16 MiB flash dump (16777216 bytes).');
    }
    const chip = bytes[12] | bytes[13] << 8;
    if (chip !== 5 && chip !== 9) {
        throw new Error(`Unsupported ESP chip ID ${chip}; expected ESP32-C3 (5) or ESP32-S3 (9).`);
    }
    if (bytes.length > 0x640000) {
        throw new Error('App image exceeds the CrossPoint app0 partition (6553600 bytes).');
    }
    const machine = chip === 5 ? 'x3' : 'x4pro';
    status(`Composing 16 MiB flash image for ${machine === 'x3' ? 'X3' : 'X4 Pro'}…`);
    const [boot, partitions, nvs] = await Promise.all([
        fetchImage(`bootloader-${chip === 5 ? 'esp32c3' : 'esp32s3'}.bin`),
        fetchImage('partitions.bin'),
        chip === 5 ? fetchImage('nvs-x3.bin') : null
    ]);
    if (boot.byteLength < 24 || boot.byteLength > 0x8000 ||
        new Uint8Array(boot)[0] !== 0xe9 || new Uint8Array(boot)[12] !== chip ||
        partitions.byteLength !== 0xc00 || (nvs && nvs.byteLength !== 0x5000)) {
        throw new Error('Invalid bundled boot assets. Run package-web.sh to regenerate the distribution.');
    }
    const flash = new Uint8Array(0x1000000).fill(0xff);
    flash.set(new Uint8Array(boot), 0);
    flash.set(new Uint8Array(partitions), 0x8000);
    if (nvs) flash.set(new Uint8Array(nvs), 0x9000);
    flash.set(bytes, 0x10000);
    return {flash: flash.buffer, machine, composed: true};
}

/* SD card image ownership. */
const defaultSD = () => params.get('sd') || 'blank-sd.img';
function sdSaveStatus(text, error = false) {
    $('sd-save').textContent = text;
    $('sd-save').classList.toggle('error', error);
}
async function saveSD(bytes) {
    const revision = ++sdSaving;
    sdSaveStatus('Saving SD card in this browser…');
    try {
        await sdStore.save(bytes);
        if (revision === sdSaving) sdSaveStatus('SD card saved in this browser.' +
            (running ? ' Device changes save every 10 seconds; Stop saves now.' : ' Safe to refresh.'));
    } catch (error) {
        if (revision === sdSaving) sdSaveStatus(`Could not save SD card: ${error.message} Download the SD image to keep changes.`, true);
    }
}
function autosaveSD() {
    if (!running || sd.busy || sdSnapshotPending || $('stop').disabled) return;
    sdSnapshotPending = true;
    post({type: 'autosave'});
}
setInterval(autosaveSD, 10000);
document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'hidden') autosaveSD();
});
function loadSD(source, restore = false) {
    // source: a File from the picker or a same-origin URL.
    sdImage = null; sd.ready = false;
    $('sd').disabled = true;
    $('download').disabled = true;
    sdStatus('Loading SD image…'); sdClear();
    sdLoading = (async () => {
        try {
            let saved = null, restoreFailed = false;
            if (restore) {
                try { saved = await sdStore.load(); }
                catch (error) {
                    restoreFailed = true;
                    sdSaveStatus(`Could not restore the saved SD card: ${error.message} Using a temporary card.`, true);
                }
            }
            const buffer = saved || new Uint8Array(source instanceof File ? await source.arrayBuffer() : await fetchImage(source));
            if (!buffer.byteLength || buffer.byteLength % 512) {
                throw new Error('SD image must contain a whole number of 512-byte sectors.');
            }
            sdImage = buffer;
            if (saved) sdSaveStatus('Saved SD card restored from this browser.');
            else if (!restoreFailed) await saveSD(sdImage);
            adoptSD();
        } catch (error) {
            sdStatus(error.message, true); append(`SD image: ${error.message}`);
        } finally {
            sdLoading = null;
            $('sd').disabled = pending || !!frame;
        }
    })();
    return sdLoading;
}
function adoptSD() {
    // Called whenever the page (re)gains the image: load, failed boot, stop.
    $('download').disabled = false;
    try {
        const fs = FAT.open(sdImage);
        sd.supported = true;
        sd.filesystem = fs.type;
    } catch (error) {
        sd.supported = false; sd.ready = false; sdClear();
        sdStatus(`${error.message} The emulator can still use this image.`, true);
        return;
    }
    sd.ready = true;
    sdRefresh(sd.cwd);
}

async function start() {
    if (frame || pending) return;
    pending = true;
    startedAt = performance.now(); milestones = 0; window.bootTimeSeconds = null;
    $('start').disabled = true; $('console').textContent = '';
    for (const id of ['flash', 'sd', 'turbo', 'wifi', 'reset', 'stop', 'download']) $(id).disabled = true;
    try {
        if (!crossOriginIsolated) throw new Error('SharedArrayBuffer needs isolation headers. Serve this folder with serve.py on localhost or HTTPS.');
        status('Loading images…');
        const input = $('flash').files[0] ? await $('flash').files[0].arrayBuffer() :
            params.has('flash') ? await fetchImage(params.get('flash')) : null;
        if (!input) throw new Error('Choose an official CrossPoint release .bin or a 16 MiB flash dump.');
        const {flash, machine, composed} = await prepareFlash(input);
        const composition = composed ? 'Composed 16 MiB flash from release app · ' : '';
        if (composed) { status(composition + 'loading SD image…'); append(composition.trim()); }
        // Observers can inspect the exact bytes before ownership passes to the runtime.
        window.dispatchEvent(new CustomEvent('x4prosim-flash-ready', {detail: {flash, machine, composed}}));
        if (sdLoading) await sdLoading.catch(() => {});
        if (!sdImage) await loadSD($('sd').files[0] || defaultSD());
        if (!sdImage) throw new Error('No SD image is loaded.');
        const rom = await fetchImage(machine === 'x3' ? 'esp32c3-rom.bin' : 'esp32s3_rev0_rom.bin');
        // The explorer waits for the runtime to own the card.
        sd.ready = false; sdSetEnabled(false); sdStatus('Starting the emulator…');
        bootData = {type: 'boot', machine, flash, sd: sdImage.buffer, rom, wifi: $('wifi').checked, turbo: $('turbo').checked};
        sdImage = null;
        buttons(machine);
        $('screen').style.aspectRatio = machine === 'x3' ? '528 / 792' : '480 / 800';
        frame = document.createElement('iframe');
        frame.width = machine === 'x3' ? '528' : '480';
        frame.height = machine === 'x3' ? '792' : '800';
        frame.title = `${machine === 'x3' ? 'X3' : 'X4 Pro'} display`;
        frame.src = 'runtime.html';
        $('screen').replaceChildren(frame);
        resizeScreen();
        status(`${composition}Booting ${machine === 'x3' ? 'X3 (ESP32-C3)' : 'X4 Pro (ESP32-S3)'} · ${timingLabel()}…`);
    } catch (error) { fail(error.message); }
    finally { pending = false; }
}
function fail(message) {
    status(message); append(`Error: ${message}`);
    if (!frame) {
        for (const id of ['start', 'flash', 'sd', 'turbo', 'wifi']) $(id).disabled = false;
        if (bootData) {
            // The boot never reached the runtime; take the card back.
            sdImage = new Uint8Array(bootData.sd); bootData = null;
            adoptSD();
        }
        $('download').disabled = !sdImage;
    }
}
window.addEventListener('message', ({source, origin, data}) => {
    if (!frame || source !== frame.contentWindow || origin !== location.origin) return;
    if (data.type === 'ready') {
        const data = bootData; bootData = null;
        post(data, [data.flash, data.sd, data.rom]);
    }
    if (data.type === 'boot-error') {
        sdImage = new Uint8Array(data.sd);
        release(); frame.remove(); frame = null;
        $('keys').replaceChildren();
        $('screen').textContent = 'Could not start Wi-Fi. Retry or disable Wi-Fi downloads.';
        adoptSD();
        fail(data.message);
    }
    if (data.type === 'log') append(data.line);
    if (data.type === 'error') fail(data.message);
    if (data.type === 'exit') status(`Emulator exited (${data.status}).`);
    if (data.type === 'running') {
        running = true;
        for (const id of ['reset', 'stop', 'download']) $(id).disabled = false;
        sd.ready = sd.supported;
        if (sd.ready) sdRefresh(sd.cwd);
        else sdStatus('Unsupported filesystem: explorer disabled.', true);
    }
    if (data.type === 'sd-result') sdResult(data);
    if (data.type === 'snapshot') {
        const saved = saveSD(data.bytes);
        if (data.action === 'autosave') {
            saved.finally(() => { sdSnapshotPending = false; });
            return;
        }
        if (data.action === 'download') {
            saveBlob(data.bytes, 'sd.img');
            for (const id of ['download', 'reset', 'stop']) $(id).disabled = false;
        } else {
            release(); frame.remove(); frame = null; running = false;
            sdAbort('The emulator stopped.');
            $('screen').textContent = 'Stopped. Start again to use the current SD card.';
            $('keys').replaceChildren();
            for (const id of ['reset', 'stop']) $(id).disabled = true;
            for (const id of ['start', 'flash', 'sd', 'turbo', 'wifi']) $(id).disabled = false;
            status('Stopped. Start resumes from a fresh boot with your current SD image.');
            sdImage = data.bytes;
            sdSnapshotPending = false;
            adoptSD();
        }
    }
});
$('start').addEventListener('click', start);
$('reset').addEventListener('click', () => {
    release(); resetting(); post({type: 'reset'});
});
$('stop').addEventListener('click', () => {
    for (const id of ['reset', 'stop', 'download']) $(id).disabled = true;
    release(); post({type: 'stop'});
});
$('download').addEventListener('click', () => {
    if (frame) {
        for (const id of ['reset', 'stop', 'download']) $(id).disabled = true;
        release(); post({type: 'download'});
    } else if (sdImage) saveBlob(sdImage, 'sd.img');
});
$('sd').addEventListener('change', () => { loadSD($('sd').files[0] || defaultSD()); });

/* SD card explorer. Operations run through sdCall: on the page's own image
 * while the emulator is stopped, otherwise inside the runtime iframe, which
 * pauses the VM, applies them to /sd.img and resets the device after a change
 * so the firmware mounts the modified card afresh. */
const sd = {cwd: '/', entries: [], free: 0, busy: false, ready: false, supported: false,
    filesystem: '', calls: new Map(), sequence: 0};
const sdPanel = document.querySelector('.sd');
function sdStatus(text, error = false) {
    $('sd-info').textContent = text; $('sd-info').classList.toggle('error', error);
}
function sdError(error) {
    const message = error?.message || String(error);
    $('sd-error').textContent = message;
    append(`SD card: ${message}`);
}
function sdClear() {
    $('sd-rows').replaceChildren(); $('sd-path').replaceChildren(); $('sd-error').textContent = '';
    sdSetEnabled(false);
}
function sdSetEnabled(enabled) {
    const on = enabled && sd.ready && !sd.busy;
    for (const button of sdPanel.querySelectorAll('button')) {
        button.disabled = !on || button.hasAttribute('aria-current');
    }
    sdPanel.classList.toggle('busy', sd.busy);
    // A pending operation holds the VM; keep the device actions out of the way.
    if (running) for (const id of ['reset', 'stop', 'download']) $(id).disabled = sd.busy;
}
function formatSize(bytes) {
    if (bytes < 1024) return `${bytes} B`;
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KiB`;
    if (bytes < 1024 * 1024 * 1024) return `${(bytes / 1024 / 1024).toFixed(1)} MiB`;
    return `${(bytes / 1024 / 1024 / 1024).toFixed(2)} GiB`;
}
const joinPath = (dir, name) => dir === '/' ? `/${name}` : `${dir}/${name}`;
async function sdCall(op, args = {}, transfer = []) {
    if (!sd.ready) return Promise.reject(new Error('The SD card is not available right now.'));
    if (frame) {
        if (!running) return Promise.reject(new Error('The emulator is still starting.'));
        const id = ++sd.sequence;
        return new Promise((resolve, reject) => {
            sd.calls.set(id, {resolve, reject});
            post({type: 'sd', id, op, args}, transfer);
        });
    }
    try { return sdExecute(sdImage, op, args); }
    finally {
        // Failed multi-file uploads may still have changed part of the card.
        if (sdMutates(op)) await saveSD(sdImage);
    }
}
async function sdResult(data) {
    const call = sd.calls.get(data.id);
    sd.calls.delete(data.id);
    if (data.reset) resetting('SD card changed · ');
    if (data.image) await saveSD(data.image);
    if (!call) return;
    if (data.ok) call.resolve(data.result);
    else call.reject(Object.assign(new Error(data.error.message), {code: data.error.code}));
}
function sdAbort(reason) {
    for (const call of sd.calls.values()) call.reject(new Error(reason));
    sd.calls.clear();
}
async function sdRun(action) {
    // One explorer operation at a time; errors show in the panel and the log.
    if (sd.busy || !sd.ready) return;
    sd.busy = true; sdSetEnabled(false); $('sd-error').textContent = '';
    try { await action(); }
    catch (error) {
        sdError(error);
        if (sd.ready) await sdRefresh().catch(() => {});
    } finally { sd.busy = false; sdSetEnabled(true); }
}
async function sdRefresh(path = sd.cwd) {
    let result;
    try { result = await sdCall('list', {path}); }
    catch (error) {
        // The folder may have gone away (rename, delete, new image): fall back.
        if (error.code === 'ENOENT' && path !== '/') return sdRefresh('/');
        sdError(error); return;
    }
    sd.cwd = path; sd.entries = result.entries; sd.free = result.free;
    sdStatus(`${result.type} · ${formatSize(result.free)} free of ${formatSize(result.total)}` +
        (frame ? ' · changes reset the device' : ''));
    sdRender();
}
function sdRender() {
    const crumbs = $('sd-path');
    crumbs.replaceChildren();
    const parts = sd.cwd.split('/').filter(Boolean);
    parts.forEach((part, index) => {
        const button = document.createElement('button');
        button.textContent = part;
        button.dataset.path = '/' + parts.slice(0, index + 1).join('/');
        if (index === parts.length - 1) button.setAttribute('aria-current', 'location');
        crumbs.append(sep(), button);
    });
    const root = document.createElement('button');
    root.textContent = 'SD card'; root.dataset.path = '/';
    if (!parts.length) root.setAttribute('aria-current', 'location');
    crumbs.prepend(root);
    function sep() { const span = document.createElement('span'); span.textContent = '/'; return span; }

    const rows = $('sd-rows');
    rows.replaceChildren();
    for (const entry of sd.entries) {
        const row = document.createElement('tr');
        row.dataset.name = entry.name;
        const name = document.createElement('td');
        name.className = 'name';
        if (entry.isDir) {
            const open = document.createElement('button');
            open.className = 'dir'; open.textContent = entry.name; open.dataset.action = 'open';
            name.append(open);
        } else {
            const span = document.createElement('span');
            span.className = 'file'; span.textContent = entry.name;
            name.append(span);
        }
        const size = document.createElement('td');
        size.className = 'size'; size.textContent = entry.isDir ? '' : formatSize(entry.size);
        const date = document.createElement('td');
        date.className = 'date';
        date.textContent = entry.mtime ? entry.mtime.toLocaleString(undefined, {dateStyle: 'medium', timeStyle: 'short'}) : '';
        const actions = document.createElement('td');
        actions.className = 'ops';
        for (const [action, label] of [['download', 'Download'], ['rename', 'Rename'], ['remove', 'Delete']]) {
            if (action === 'download' && entry.isDir) continue;
            const button = document.createElement('button');
            button.textContent = label; button.dataset.action = action;
            button.setAttribute('aria-label', `${label} ${entry.name}`);
            actions.append(button);
        }
        row.append(name, size, date, actions);
        rows.append(row);
    }
    if (!sd.entries.length) {
        const row = document.createElement('tr');
        row.className = 'empty';
        const cell = document.createElement('td');
        cell.colSpan = 4; cell.textContent = 'Empty folder';
        row.append(cell); rows.append(row);
    }
    sdSetEnabled(true);
}
function sdValidName(name) {
    name = name.normalize('NFC');
    if (!FAT.isValidName(name)) {
        throw Object.assign(new Error(`"${name}" is not a valid FAT file name.`), {code: 'EINVAL'});
    }
    return name;
}
async function sdUpload(items) {
    // items: [{path: relative to the open folder, may include folders, file}]
    const dirs = new Set(), files = [];
    for (const {path, file} of items) {
        const parts = path.split('/').filter(Boolean).map(sdValidName);
        for (let depth = 1; depth < parts.length; depth++) {
            dirs.add(joinPath(sd.cwd, parts.slice(0, depth).join('/')));
        }
        files.push({path: joinPath(sd.cwd, parts.join('/')), name: parts.at(-1), file});
    }
    if (!files.length) return;
    const existing = files.filter(({path}) =>
        sd.entries.some(entry => !entry.isDir && joinPath(sd.cwd, entry.name) === path));
    if (existing.length && !confirm(existing.length === 1 ?
        `Replace "${existing[0].name}" on the SD card?` : `Replace ${existing.length} existing files on the SD card?`)) return;
    const payload = [];
    for (const {path, file} of files) payload.push({path, bytes: new Uint8Array(await file.arrayBuffer())});
    const total = payload.reduce((sum, {bytes}) => sum + bytes.length, 0);
    if (total > sd.free) {
        throw Object.assign(new Error(`Not enough space: ${formatSize(total)} to copy, ${formatSize(sd.free)} free.`), {code: 'ENOSPC'});
    }
    sdStatus(`Copying ${payload.length === 1 ? payload[0].path.slice(1) : payload.length + ' files'} (${formatSize(total)})…`);
    await sdCall('write', {dirs: [...dirs], files: payload}, payload.map(({bytes}) => bytes.buffer));
    await sdRefresh();
}
async function collectDropped(transfer) {
    // Folders arrive through the entries API; call it before the first await.
    const entries = [...transfer.items].map(item => item.webkitGetAsEntry?.()).filter(Boolean);
    if (!entries.length) return [...transfer.files].map(file => ({path: file.name, file}));
    const items = [];
    async function walk(entry, prefix) {
        if (entry.isFile) {
            items.push({path: prefix + entry.name, file: await new Promise((resolve, reject) => entry.file(resolve, reject))});
        } else if (entry.isDirectory) {
            const reader = entry.createReader();
            for (;;) {
                const batch = await new Promise((resolve, reject) => reader.readEntries(resolve, reject));
                if (!batch.length) break;
                for (const child of batch) await walk(child, `${prefix}${entry.name}/`);
            }
        }
    }
    for (const entry of entries) await walk(entry, '');
    return items;
}
$('sd-upload').addEventListener('click', () => $('sd-files').click());
$('sd-files').addEventListener('change', () => {
    const items = [...$('sd-files').files].map(file => ({path: file.name, file}));
    $('sd-files').value = '';
    sdRun(() => sdUpload(items));
});
$('sd-mkdir').addEventListener('click', () => sdRun(async () => {
    const name = prompt('New folder name');
    if (!name) return;
    await sdCall('mkdir', {path: joinPath(sd.cwd, sdValidName(name))});
    await sdRefresh();
}));
$('sd-refresh').addEventListener('click', () => sdRun(() => sdRefresh()));
$('sd-path').addEventListener('click', event => {
    const button = event.target.closest('button[data-path]');
    if (button && !button.hasAttribute('aria-current')) sdRun(() => sdRefresh(button.dataset.path));
});
$('sd-rows').addEventListener('click', event => {
    const button = event.target.closest('button[data-action]');
    if (!button) return;
    const name = button.closest('tr').dataset.name;
    const entry = sd.entries.find(entry => entry.name === name);
    const path = joinPath(sd.cwd, name);
    if (!entry) return;
    sdRun(async () => {
        if (button.dataset.action === 'open') {
            await sdRefresh(path);
        } else if (button.dataset.action === 'download') {
            const {bytes} = await sdCall('read', {path});
            saveBlob(bytes, name);
        } else if (button.dataset.action === 'rename') {
            const target = prompt(`Rename "${name}" to`, name);
            if (!target || target === name) return;
            await sdCall('rename', {path, name: sdValidName(target)});
            await sdRefresh();
        } else if (button.dataset.action === 'remove') {
            if (!confirm(entry.isDir ? `Delete the folder "${name}" and everything in it?` : `Delete "${name}"?`)) return;
            await sdCall('remove', {path});
            await sdRefresh();
        }
    });
});
for (const type of ['dragenter', 'dragover']) sdPanel.addEventListener(type, event => {
    if (!sd.ready || sd.busy) return;
    event.preventDefault();
    event.dataTransfer.dropEffect = 'copy';
    sdPanel.classList.add('drop');
});
sdPanel.addEventListener('dragleave', event => {
    if (!sdPanel.contains(event.relatedTarget)) sdPanel.classList.remove('drop');
});
sdPanel.addEventListener('drop', event => {
    event.preventDefault();
    sdPanel.classList.remove('drop');
    if (!sd.ready || sd.busy) return;
    const collected = collectDropped(event.dataTransfer);
    sdRun(async () => sdUpload(await collected));
});

// An explicit URL is an intentional replacement (also useful for repeatable tests).
loadSD(defaultSD(), !params.has('sd'));
if (params.has('flash')) start();
