/* A fresh iframe owns each Emscripten instance and its pthread workers. */
'use strict';
let input = [];
let snapshot = null;
let monitor = false;
const canvas = document.querySelector('canvas');
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
    if (!line.includes('\x1b') && !line.startsWith('QEMU ')) {
        send('log', {line: line.replace(/^\(qemu\) /, '')});
    }
    if (snapshot && line.includes('VM status: paused')) {
        const action = snapshot;
        snapshot = null;
        const bytes = Module.FS.readFile('/sd.img');
        parent.postMessage({type: 'snapshot', action, bytes}, location.origin, [bytes.buffer]);
        if (action === 'download') command('cont');
    }
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
        command('system_reset');
    } else if (data.type === 'download' || data.type === 'stop') {
        snapshot = data.type;
        command('stop\ninfo status');
    } else if (data.type === 'boot') {
        const {machine, flash, sd, rom, turbo = false} = data;
        // Keep the default pacing; allow deterministic benchmark runs to opt out.
        const sleep = turbo || new URLSearchParams(parent.location.search).get('sleep') === 'off' ? 'off' : 'on';
        window.Module = {
            canvas,
            arguments: ['-L', '/', '-machine', machine,
                '-accel', 'tcg,tb-size=64',
                '-icount', `shift=${machine === 'x3' ? 0 : 2},sleep=${sleep}`,
                ...(turbo ? turboProperties(machine) : []),
                '-drive', 'file=/flash.bin,if=mtd,format=raw,cache.direct=off',
                '-drive', 'file=/sd.img,if=sd,format=raw,cache.direct=off',
                '-chardev', 'stdio,id=cdc,mux=on', '-serial', 'null', '-parallel', 'none',
                '-global', 'driver=misc.esp32s3.usb_serial_jtag,property=chardev,value=cdc',
                '-mon', 'chardev=cdc,mode=readline', '-display', 'sdl,show-cursor=on', '-nic', 'none'],
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
send('ready');
