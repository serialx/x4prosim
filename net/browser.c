/*
 * Ethernet endpoint for the browser download adapter.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * JavaScript owns the network stack on the runtime thread. Polling on the QEMU
 * main loop keeps packet delivery under the BQL, out of JS callbacks.
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "net/net.h"
#include "clients.h"
#include <emscripten.h>

#define BROWSER_FRAME_MAX 2048

typedef struct NetBrowserState {
    NetClientState nc;
    QEMUTimer *timer;
    bool connected;
} NetBrowserState;

static ssize_t browser_receive(NetClientState *nc, const uint8_t *buf,
                               size_t size)
{
    if (size <= BROWSER_FRAME_MAX) {
        MAIN_THREAD_EM_ASM({
            Module.browserNetwork.send(HEAPU8.subarray(Number($0),
                                                      Number($0) + Number($1)));
        }, buf, size);
    }
    /* Congestion drops frames just like a physical link; TCP retransmits. */
    return size;
}

static void browser_poll(void *opaque)
{
    NetBrowserState *s = opaque;
    uint8_t buf[BROWSER_FRAME_MAX];
    bool connected = MAIN_THREAD_EM_ASM_INT({
        return Module.browserNetwork.connected;
    });

    if (connected != s->connected) {
        s->connected = connected;
        s->nc.link_down = !connected;
        info_report("Browser Wi-Fi %s", connected ? "connected" :
                    "disconnected; stop and start the emulator to reconnect");
    }
    for (int i = 0; connected && i < 64 && qemu_can_send_packet(&s->nc); i++) {
        int len = MAIN_THREAD_EM_ASM_INT({
            const packet = Module.browserNetwork.take();
            if (!packet) {
                return 0;
            }
            HEAPU8.set(packet, Number($0));
            return packet.length;
        }, buf);
        if (!len) {
            break;
        }
        qemu_send_packet(&s->nc, buf, len);
    }
    timer_mod(s->timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + 10);
}

static void browser_cleanup(NetClientState *nc)
{
    NetBrowserState *s = DO_UPCAST(NetBrowserState, nc, nc);

    timer_free(s->timer);
    MAIN_THREAD_EM_ASM({ Module.browserNetwork.close(); });
}

static NetClientInfo net_browser_info = {
    .type = NET_CLIENT_DRIVER_BROWSER,
    .size = sizeof(NetBrowserState),
    .receive = browser_receive,
    .cleanup = browser_cleanup,
};

int net_init_browser(const Netdev *netdev, const char *name,
                     NetClientState *peer, Error **errp)
{
    NetClientState *nc;
    NetBrowserState *s;
    bool connected = MAIN_THREAD_EM_ASM_INT({
        return !!(Module.browserNetwork && Module.browserNetwork.connected);
    });

    if (!connected) {
        error_setg(errp, "Browser Wi-Fi adapter has not been initialized");
        return -1;
    }
    nc = qemu_new_net_client(&net_browser_info, peer, "browser", name);
    s = DO_UPCAST(NetBrowserState, nc, nc);
    s->connected = false;
    nc->link_down = true;
    s->timer = timer_new_ms(QEMU_CLOCK_REALTIME, browser_poll, s);
    timer_mod(s->timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME));
    qemu_set_info_str(nc, "In-browser HTTP download network");
    return 0;
}
