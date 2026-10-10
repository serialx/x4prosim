/* A fresh iframe owns each Emscripten instance and its pthread workers. */
'use strict';
let input = [];
let monitor = false;
// Actions that need the VM stopped (snapshots, SD card operations), FIFO.
// Each one runs on the 'VM status: paused' line of its own 'info status'.
const pauses = [];
let paused = null;
const canvas = document.querySelector('canvas');
// The iframe and workers are discarded on navigation. SDL's unload callback
// cannot finish a guest shutdown here, and its wasm64 string return currently
// trips Emscripten's BigInt-to-string conversion. Saves happen before unload.
window.addEventListener('beforeunload', event => event.stopImmediatePropagation(), {capture: true});
const send = (type, extra = {}) => parent.postMessage({type, ...extra}, location.origin);
const keyCodes = {ArrowUp: 38, ArrowDown: 40, ArrowLeft: 37, ArrowRight: 39,
    Enter: 13, Backspace: 8, Escape: 27, KeyP: 80};
// Keep these overrides in sync with x4prosim/turbo-args.sh.
function turboProperties(machine) {
    const properties = [
        ...['read-access-us', 'read-seq-access-us', 'read-repeat-us', 'read-next-us',
            'write-busy-us', 'write-random-busy-us', 'write-repeat-busy-us',
            'write-block-busy-us', 'write-stop-busy-us', 'write-stop-decrement-us',
            'write-stop-random-extra-us'].map(name => `ssi-sd.${name}=0`),
        ...['transaction-overhead-us', 'transaction-overhead-ns', 'buffer-overhead-ns']
            .map(name => `driver=ssi.esp32s3.gpspi,property=${name},value=0`),
        'driver=ssi.esp32s3.gpspi,property=zero-wire-time,value=on',
        ...(machine === 'x3' ?
            [...['frame-us', 'refresh-overhead-us'].map(name => `uc8279.${name}=0`),
                ...['pon-ms', 'pof-ms', 'busy-ms'].map(name => `uc8279.${name}=1`)] :
            // Animation divides by frame-us; PON/POF remain fixed at 2 ms.
            ['uc8179.frame-us=1', 'uc8179.busy-ms=1'])
    ];
    return properties.flatMap(property => ['-global', property]);
}
function output(line) {
    // HMP shares the CDC chardev; keep terminal editing escapes out of the log.
    // 'VM status' answers the page's own pause requests; keep them out too.
    if (!line.includes('\x1b') && !line.startsWith('QEMU ') && !line.startsWith('VM status:')) {
        send('log', {line: line.replace(/^\(qemu\) /, '')});
    }
    if (paused && line.includes('VM status: paused')) {
        const action = paused;
        const resume = action();
        if (resume === 'reset') {
            window.Module?.browserNetwork?.reset();
            command('system_reset\ncont');
        } else if (resume === 'cont') command('cont');
        nextPause();
    }
}
function whilePaused(action) {
    pauses.push(action);
    if (!paused) nextPause();
}
function nextPause() {
    paused = pauses.shift() || null;
    if (paused) command('stop\ninfo status');
}
function snapshot(action) {
    whilePaused(() => {
        if (action === 'stop') Module.browserNetwork?.close();
        const bytes = Module.FS.readFile('/sd.img');
        parent.postMessage({type: 'snapshot', action, bytes}, location.origin, [bytes.buffer]);
        return action === 'stop' ? null : 'cont';
    });
}
// The card image QEMU reads and writes, in place (writeFile(canOwn) adopted it).
function sdBytes() {
    const node = Module.FS.lookupPath('/sd.img').node;
    return node.contents.subarray(0, node.usedBytes);
}
function sdOperation({id, op, args}) {
    whilePaused(() => {
        const reply = {type: 'sd-result', id, ok: true, reset: false};
        const transfer = [];
        try {
            const image = sdBytes();
            reply.result = sdExecute(image, op, args);
            if (reply.result.bytes) {
                // Transfer only a buffer that holds nothing but the file.
                const {bytes} = reply.result;
                if (bytes.buffer === image.buffer || bytes.byteOffset || bytes.byteLength !== bytes.buffer.byteLength) {
                    reply.result.bytes = bytes.slice();
                }
                transfer.push(reply.result.bytes.buffer);
            }
            reply.reset = sdMutates(op);
        } catch (error) {
            reply.ok = false;
            reply.error = {code: error.code || 'EIO', message: error.message};
            // Anything that may have touched a sector needs a remount.
            reply.reset = sdMutates(op) && !sdPrecheckErrors.includes(error.code);
        }
        if (reply.reset) {
            // Save the edited card before the reset lets firmware write again.
            reply.image = Module.FS.readFile('/sd.img');
            transfer.push(reply.image.buffer);
        }
        parent.postMessage(reply, location.origin, transfer);
        return reply.reset ? 'reset' : 'cont';
    });
}
function command(text) {
    if (!monitor) { input.push(1, 99); monitor = true; }
    input.push(...new TextEncoder().encode(text + '\n'));
    Module.FS.getStream(0).node.notifyListeners?.(1);
}
window.addEventListener('message', async ({source, origin, data}) => {
    if (source !== parent || origin !== location.origin) return;
    if (data.type === 'key') {
        document.dispatchEvent(new KeyboardEvent(data.down ? 'keydown' : 'keyup', {
            key: data.code === 'KeyP' ? 'p' : data.code, code: data.code,
            keyCode: keyCodes[data.code], which: keyCodes[data.code], bubbles: true
        }));
    } else if (data.type === 'reset') {
        window.Module?.browserNetwork?.reset();
        command('system_reset');
    } else if (data.type === 'download' || data.type === 'stop' || data.type === 'autosave') {
        snapshot(data.type);
    } else if (data.type === 'sd') {
        sdOperation(data);
    } else if (data.type === 'boot') {
        const {machine, flash, sd, rom, wifi = true, turbo = false} = data;
        // Keep the default pacing; allow deterministic benchmark runs to opt out.
        const sleep = turbo || new URLSearchParams(parent.location.search).get('sleep') === 'off' ? 'off' : 'on';
        let browserNetwork = null;
        try {
            if (wifi) browserNetwork = await (await import('./browser-network.mjs')).createBrowserNetwork({log: output});
        } catch (error) {
            // Return the card so a failed Wi-Fi startup can be retried without
            // losing a card preserved by Stop in the previous session.
            parent.postMessage({type: 'boot-error', message: String(error), sd}, location.origin, [sd]);
            return;
        }
        window.Module = {
            browserNetwork,
            canvas,
            arguments: ['-L', '/', '-machine', machine,
                '-accel', 'tcg,tb-size=64',
                '-icount', `shift=${machine === 'x3' ? 0 : 2},sleep=${sleep}`,
                ...(turbo ? turboProperties(machine) : []),
                '-drive', 'file=/flash.bin,if=mtd,format=raw,cache.direct=off',
                '-drive', 'file=/sd.img,if=sd,format=raw,cache.direct=off',
                '-chardev', 'stdio,id=cdc,mux=on', '-serial', 'null', '-parallel', 'none',
                '-global', 'driver=misc.esp32s3.usb_serial_jtag,property=chardev,value=cdc',
                '-mon', 'chardev=cdc,mode=readline', '-display', 'sdl,show-cursor=on',
                '-nic', wifi ? 'browser,model=esp32_wifi' : 'none'],
            print: output, printErr: output,
            onAbort: reason => send('error', {message: String(reason)}),
            onExit: status => send('exit', {status}),
            preRun: [() => {
                Module.FS.writeFile('/flash.bin', new Uint8Array(flash), {canOwn: true});
                Module.FS.writeFile('/sd.img', new Uint8Array(sd), {canOwn: true});
                Module.FS.writeFile(machine === 'x3' ? '/esp32c3-rom.bin' : '/esp32s3_rev0_rom.bin', new Uint8Array(rom));
            }],
            onRuntimeInitialized: () => {
                Module.TTY.default_tty_ops.get_char = () => input.length ? input.shift() : undefined;
                const stdin = Module.FS.getStream(0);
                stdin.stream_ops = {...stdin.stream_ops, poll: () => input.length ? 1 : 0};
                send('running');
                canvas.focus({preventScroll: true});
            }
        };
        const script = document.createElement('script');
        script.src = `qemu-system-${machine === 'x3' ? 'riscv32' : 'xtensa'}.js`;
        script.onerror = () => send('error', {message: 'Could not load the emulator. Run package-web.sh first.'});
        document.body.append(script);
    }
});
window.addEventListener('error', event => send('error', {message: event.message}));
window.addEventListener('pagehide', () => window.Module?.browserNetwork?.close());
send('ready');

window.addEventListener('unhandledrejection', event => send('error', {message: String(event.reason)}));
