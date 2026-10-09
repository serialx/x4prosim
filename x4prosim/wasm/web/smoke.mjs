/* Node 22+: browser acceptance test over Chrome's built-in DevTools protocol.
 * Start serve.py and headless Chrome --remote-debugging-port=9224 first.
 * node x4prosim/wasm/web/smoke.mjs 'http://127.0.0.1:8000/?flash=flash.bin&sd=sd.img'
 * Required for X3: INPUT_SD (local source card, to compare the download).
 * Optional: CDP_PORT, CHROME_PID, EVIDENCE_DIR, TIMEOUT (seconds),
 * WEB_MACHINE=x4pro, EXPECT_SERVICE_WORKER=1, PICKER_FLASH/PICKER_SD.
 * No npm packages are required. */
import fs from 'node:fs';
import {createHash} from 'node:crypto';
import os from 'node:os';
import path from 'node:path';
import {execFileSync} from 'node:child_process';
async function sha256(file) {
    const hash = createHash('sha256');
    for await (const chunk of fs.createReadStream(file)) hash.update(chunk);
    return hash.digest('hex');
}
const evidence = process.env.EVIDENCE_DIR || 'build-wasm/evidence';
fs.mkdirSync(evidence, {recursive: true});
const pages = await (await fetch(`http://127.0.0.1:${process.env.CDP_PORT || 9224}/json`)).json();
const page = pages.find(page => page.type === 'page');
const ws = new WebSocket(page.webSocketDebuggerUrl);
await new Promise(resolve => ws.addEventListener('open', resolve, {once: true}));
let sequence = 0;
const calls = new Map(), errors = [];
ws.addEventListener('message', event => {
    const message = JSON.parse(event.data);
    if (message.id) {
        const callback = calls.get(message.id); calls.delete(message.id);
        if (message.error) callback.reject(message.error); else callback.resolve(message.result);
    } else if (message.method === 'Runtime.exceptionThrown') errors.push(message.params);
});
const call = (method, params = {}) => new Promise((resolve, reject) => {
    const id = ++sequence; calls.set(id, {resolve, reject});
    ws.send(JSON.stringify({id, method, params}));
});
async function evaluate(expression) {
    const result = await call('Runtime.evaluate', {expression, returnByValue: true, awaitPromise: true});
    if (result.exceptionDetails) throw new Error(JSON.stringify(result.exceptionDetails));
    return result.result.value;
}
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
const deadline = Date.now() + Number(process.env.TIMEOUT || 600) * 1000;
async function waitFor(expression) {
    while (Date.now() < deadline) {
        if (errors.length) throw new Error(JSON.stringify(errors));
        try {
            const value = await evaluate(expression);
            if (value) return value;
        } catch (error) {
            // The isolation service worker reloads the first navigation.
            if (!/Execution context was destroyed|Cannot find context/.test(error.message || '')) throw error;
        }
        await delay(500);
    }
    throw new Error(`Timed out: ${expression}`);
}
async function screenshot(name) {
    const result = await call('Page.captureScreenshot', {format: 'png', captureBeyondViewport: false});
    fs.writeFileSync(path.join(evidence, name), Buffer.from(result.data, 'base64'));
}
const consoleText = 'document.querySelector("#console")?.textContent';
const canvas = 'document.querySelector("iframe").contentDocument.querySelector("canvas")';
const measurements = {host_cpu: os.cpus()[0].model, host_logical_cpus: os.cpus().length,
    host_ram_bytes: os.totalmem(), url: process.argv[2],
    browser: await call('Browser.getVersion')};
let peakRSS = 0, peakCPU = 0;
const chromePID = Number(process.env.CHROME_PID);
const sampler = setInterval(() => {
    if (!chromePID) return;
    const rows = execFileSync('ps', ['-axo', 'pid=,ppid=,rss=,%cpu='], {encoding: 'utf8'})
        .trim().split('\n').map(line => line.trim().split(/\s+/).map(Number));
    const pids = new Set([chromePID]);
    for (let pass = 0; pass < 4; pass++) for (const row of rows) if (pids.has(row[1])) pids.add(row[0]);
    const owned = rows.filter(row => pids.has(row[0]));
    peakRSS = Math.max(peakRSS, owned.reduce((sum, row) => sum + row[2], 0));
    peakCPU = Math.max(peakCPU, owned.reduce((sum, row) => sum + row[3], 0));
}, 1000);
try {
    await call('Runtime.enable'); await call('Page.enable');
    const downloads = path.resolve(evidence, 'web-downloads');
    fs.mkdirSync(downloads, {recursive: true});
    fs.rmSync(path.join(downloads, 'sd.img'), {force: true});
    await call('Browser.setDownloadBehavior', {behavior: 'allow', downloadPath: downloads});
    await call('Emulation.setDeviceMetricsOverride', {width: 1400, height: 1380, deviceScaleFactor: 1, mobile: false});
    const start = Date.now();
    await call('Page.addScriptToEvaluateOnNewDocument', {source: String.raw`
        window.addEventListener('message', ({origin, data}) => {
            if (origin === location.origin && data.type === 'log' &&
                /Wait complete:\s+(?:8279|X3)_DRF/.test(data.line)) {
                window.smokeRefreshes = (window.smokeRefreshes || 0) + 1;
                if (window.smokeRefreshes === 3) window.smokeHomeEpoch = Date.now();
            }
        });
    `});
    await call('Page.navigate', {url: process.argv[2]});
    // Do not count exceptions from the previous navigation.
    errors.length = 0;
    if (process.env.PICKER_FLASH) {
        await waitFor('crossOriginIsolated && document.querySelector("#flash")');
        await call('DOM.enable');
        const {root} = await call('DOM.getDocument');
        for (const [selector, file] of [['#flash', process.env.PICKER_FLASH], ['#sd', process.env.PICKER_SD]]) {
            if (!file) continue;
            const {nodeId} = await call('DOM.querySelector', {nodeId: root.nodeId, selector});
            await call('DOM.setFileInputFiles', {nodeId, files: [path.resolve(file)]});
        }
        await evaluate('document.querySelector("#start").click()');
        measurements.file_pickers = true;
    }
    await waitFor('crossOriginIsolated && document.querySelector("iframe")');
    measurements.cross_origin_isolated = await evaluate('crossOriginIsolated');
    measurements.service_worker_controlled = await evaluate('!!navigator.serviceWorker.controller');
    if (process.env.EXPECT_SERVICE_WORKER === '1' && !measurements.service_worker_controlled) {
        throw new Error('Header-less hosting did not acquire a service worker');
    }
    measurements.webgl = await evaluate(`(() => {
        const gl = document.createElement('canvas').getContext('webgl');
        if (!gl) return null;
        const ext = gl.getExtension('WEBGL_debug_renderer_info');
        return {renderer: ext ? gl.getParameter(ext.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER)};
    })()`);
    if (!measurements.webgl) throw new Error('WebGL must be available for this acceptance test');
    if (process.env.WEB_MACHINE === 'x4pro') {
        await waitFor(`${consoleText}?.includes('invalid header: 0x00000000')`);
        await delay(2000);
        measurements.rom_s = (Date.now() - start) / 1000;
        measurements.canvas = await evaluate(`({width:${canvas}.width,height:${canvas}.height})`);
        if (measurements.canvas.width <= 0 ||
            Math.abs(measurements.canvas.width / measurements.canvas.height - 480 / 800) > 0.005) {
            throw new Error('X4 Pro panel is not portrait');
        }
        const point = await evaluate(`(() => {
            const r = document.querySelector('iframe').getBoundingClientRect();
            return {x: r.x + r.width / 2, y: r.y + r.height / 2};
        })()`);
        await call('Input.dispatchMouseEvent', {type: 'mouseMoved', ...point});
        await call('Input.dispatchMouseEvent', {type: 'mousePressed', button: 'left', clickCount: 1, ...point});
        await call('Input.dispatchMouseEvent', {type: 'mouseReleased', button: 'left', clickCount: 1, ...point});
        measurements.pointer_delivered_without_error = true;
        await screenshot('web-x4pro.png');
    } else {
        measurements.home_milestone_s = await waitFor('window.bootTimeSeconds');
        measurements.page_load_to_home_s = ((await waitFor('window.smokeHomeEpoch')) - start) / 1000;
        measurements.canvas = await evaluate(`({width:${canvas}.width,height:${canvas}.height})`);
        if (measurements.canvas.width <= 0 ||
            Math.abs(measurements.canvas.width / measurements.canvas.height - 528 / 792) > 0.005) {
            throw new Error('X3 panel is not portrait');
        }
        console.log(`Home milestone: ${measurements.home_milestone_s.toFixed(3)} s`);
        // As in smoke.sh, wait for thumbnail generation to settle before input.
        await waitFor(`${consoleText}?.includes('[MEM]')`);
        measurements.settled_s = (Date.now() - start) / 1000;
        await screenshot('web-home.png');
        const before = await evaluate(`${canvas}.toDataURL()`);
        await evaluate('document.querySelector("#keys button:nth-child(6)").click()');
        await waitFor(`${canvas}.toDataURL() !== ${JSON.stringify(before)}`);
        await delay(1500);
        await screenshot('web-down.png');
        measurements.down_changed_canvas = true;
        // Exercise native keyboard events too (Up restores the previous selection).
        const afterDown = await evaluate(`${canvas}.toDataURL()`);
        await evaluate(`${canvas}.focus({preventScroll:true})`);
        await call('Input.dispatchKeyEvent', {type: 'keyDown', key: 'ArrowUp', code: 'ArrowUp', windowsVirtualKeyCode: 38});
        await delay(250);
        await call('Input.dispatchKeyEvent', {type: 'keyUp', key: 'ArrowUp', code: 'ArrowUp', windowsVirtualKeyCode: 38});
        await waitFor(`${canvas}.toDataURL() !== ${JSON.stringify(afterDown)}`);
        await delay(1500);
        await screenshot('web-keyboard-up.png');
        measurements.keyboard_up_changed_canvas = true;
        await evaluate('document.querySelector("#download").click()');
        await waitFor('savedSD?.byteLength && !document.querySelector("#download").disabled');
        measurements.downloaded_sd_bytes = await evaluate('savedSD.byteLength');
        while (!fs.existsSync(path.join(downloads, 'sd.img'))) {
            if (Date.now() > deadline) throw new Error('SD download did not complete');
            await delay(500);
        }
        if (fs.statSync(path.join(downloads, 'sd.img')).size !== measurements.downloaded_sd_bytes) {
            throw new Error('Downloaded SD file has the wrong size');
        }
        measurements.downloaded_sd_sha256 = await sha256(path.join(downloads, 'sd.img'));
        if (!process.env.INPUT_SD) throw new Error('Set INPUT_SD to check guest SD writes');
        measurements.input_sd_sha256 = await sha256(process.env.INPUT_SD);
        if (measurements.input_sd_sha256 === measurements.downloaded_sd_sha256) {
            throw new Error('Downloaded SD did not change from the input');
        }
        measurements.sd_writes_preserved = true;
        const bootCount = await evaluate(`(${consoleText}.match(/ESP-ROM:/g)||[]).length`);
        await evaluate('document.querySelector("#reset").click()');
        await waitFor(`(${consoleText}.match(/ESP-ROM:/g)||[]).length > ${bootCount}`);
        measurements.reset_booted_rom = true;
    }
    // Stop snapshots the modified SD image and tears down all pthread workers.
    await evaluate('document.querySelector("#stop").click()');
    await waitFor('!document.querySelector("iframe")');
    measurements.exported_sd_bytes = await evaluate('savedSD.byteLength');
    measurements.stop_removed_runtime = true;
    measurements.sampled_peak_chrome_rss_kib = peakRSS;
    measurements.sampled_peak_chrome_cpu_percent = peakCPU;
    const prefix = process.env.WEB_MACHINE === 'x4pro' ? 'web-x4pro' : 'web';
    fs.writeFileSync(path.join(evidence, `${prefix}.log`), await evaluate(consoleText));
    fs.writeFileSync(path.join(evidence, `${prefix}-measurements.json`), JSON.stringify(measurements, null, 2) + '\n');
    if (errors.length) throw new Error(JSON.stringify(errors));
    console.log(JSON.stringify(measurements, null, 2));
    console.log('PASS: all browser smoke checks passed');
} catch (error) {
    fs.writeFileSync(path.join(evidence, 'web-failure.txt'), String(error.stack || error));
    try {
        fs.writeFileSync(path.join(evidence, 'web-failure.log'), await evaluate(consoleText) || '');
        await screenshot('web-failure.png');
    } catch { /* Keep the original failure if the target was lost. */ }
    throw error;
} finally {
    clearInterval(sampler);
    ws.close();
}
