/* Node 22+: browser acceptance test over Chrome's built-in DevTools protocol.
 * Start serve.py and headless Chrome --remote-debugging-port=9224 first.
 * node x4prosim/wasm/web/smoke.mjs 'http://127.0.0.1:8000/?flash=flash.bin&sd=sd.img'
 * Optional: CDP_PORT, EVIDENCE_DIR, TIMEOUT (seconds), WEB_MACHINE=x4pro.
 * No npm packages are required. */
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {execFileSync} from 'node:child_process';
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
        const value = await evaluate(expression);
        if (value) return value;
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
    host_ram_bytes: os.totalmem(), url: process.argv[2]};
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
    await call('Browser.setDownloadBehavior', {behavior: 'allow', downloadPath: downloads});
    await call('Emulation.setDeviceMetricsOverride', {width: 1400, height: 1380, deviceScaleFactor: 1, mobile: false});
    const start = Date.now();
    await call('Page.navigate', {url: process.argv[2]});
    // Do not count exceptions from the previous navigation.
    errors.length = 0;
    if (process.env.WEB_MACHINE === 'x4pro') {
        await waitFor(`${consoleText}?.includes('invalid header: 0x00000000')`);
        await delay(2000);
        measurements.rom_s = (Date.now() - start) / 1000;
        measurements.canvas = await evaluate(`({width:${canvas}.width,height:${canvas}.height})`);
        await screenshot('web-x4pro.png');
    } else {
        measurements.home_milestone_s = await waitFor('window.bootTimeSeconds');
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
    console.log(JSON.stringify(measurements, null, 2));
} finally {
    clearInterval(sampler);
    ws.close();
}
