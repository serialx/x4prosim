/*
 * UltraChip UC8279d (or UC8253) e-paper controller on the Xteink X3: 792x528
 * glass on an 800x600 controller, 3-wire SPI where SDA (MOSI) doubles as the
 * read line, DC/RST on GPIO, BUSY_N on GPIO (low = busy).
 *
 * Bytes arrive from the SPI controller (SSI bus) and, for the firmware's
 * controller probe, as bit-banged SCL/SDA edges on GPIO. The VER (0x70) read
 * answers 00 03 66 00 00 (LUT_VER 0x66 = the X3's UC8279d); with "uc8253"
 * nothing answers and SDA floats high, which is how the firmware recognizes
 * the UC8253 run of the X3. FLG (0x71) gives BUSY_N in bit 0.
 *
 * Modeled: PSR (0x00) REG bit, TRES (0x61), the partial window (0x90, 9
 * bytes) with PTIN/PTOUT (0x91/0x92), DTM1 (0x10, OLD) and DTM2 (0x13, NEW)
 * written inside the window (or the TRES frame), the register LUTs 0x20-0x24
 * and DRF (0x12). A refresh runs the LUT per pixel class (OLD, NEW bits ->
 * WW/KW/WK/KK row): every phase of VDH moves the ink toward black and of VDL
 * toward white by (frames - "dead-frames") / "swing-frames" of the way, so a
 * one-frame balance pulse does nothing and a 26-frame phase saturates; REG=0
 * (no waveform in this module's MTP) just shows NEW. BUSY_N is low for the
 * longest of VCOM and the four transition rows, plus refresh-overhead-us.
 * The measured X3 banks have equal row lengths: they do NOT establish which
 * row gates BUSY on silicon. Taking the maximum also handles a longer VCOM.
 * UC8253 groups have 6 bytes (levels, four frame counts, repeat count).
 * UC8279d groups have 7 (group repeat, four rail<<6|frames bytes, two state
 * repeats); each state repeats its pair of phases within the group repeat.
 * Zero repeats skip that group/state, not the rest of the table.
 *
 * PLL (0x30) scales frame-us relative to 0x09 (UC8253) / 0x0f (UC8279d).
 * Rate tables: six-byte LUT family UC8179c C0.6 p24 (UC8253), seven-byte
 * family UC8253c A0.61 p26 (UC8279d). These revision mappings are inferred,
 * not a measured PLL sweep or matching-silicon documentation. frame-us is an
 * effective panel period, not the reciprocal of the nominal PLL frequency.
 * PON/POF use pon-ms/pof-ms. See x4prosim/sdcal/panel.md for calibration and
 * remaining revision uncertainties. Plane bit 1 = white; DSLP sleeps until RST.
 * Shown portrait (528x792) like the device, panel row 0 at the right.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/ssi/ssi.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "ui/console.h"
#include "ui/pixel_ops.h"
#include "trace.h"

#define TYPE_UC8279 "uc8279"
OBJECT_DECLARE_SIMPLE_TYPE(Uc8279State, UC8279)

#define W 792                   /* glass */
#define H 528
#define RAM_W 800               /* controller RAM */
#define RAM_H 600
#define RAM_WB (RAM_W / 8)
#define LUT_ROWS 5
#define LUT_LEN 64
#define PSR_REG 0x20
#define SHADES 256

enum { PLANE_OLD, PLANE_NEW };

struct Uc8279State {
    SSIPeripheral parent_obj;
    QemuConsole *con;
    qemu_irq busy_n;
    qemu_irq sda_out;
    QEMUTimer busy_timer;
    bool uc8253;
    bool portrait;
    uint32_t busy_ms;       /* fixed DRF BUSY time; 0 = the LUT's frames */
    uint32_t frame_us;
    uint32_t refresh_overhead_us;
    uint32_t pon_ms;
    uint32_t pof_ms;
    uint8_t swing;          /* frames of drive for a full black <-> white swing */
    uint8_t dead;           /* frames of a phase that don't move the ink */

    bool dc;
    bool in_reset;
    bool asleep;
    bool busy;
    uint8_t cmd;
    uint32_t pos;           /* byte index into the data of the current command */
    int64_t plane_start_ns;
    uint8_t psr;
    uint8_t pll;
    uint16_t tres_w, tres_h;
    bool partial;
    uint16_t win[4];        /* xs, xe, ys, ye */
    uint8_t winbuf[9];
    uint8_t lut[LUT_ROWS][LUT_LEN];
    uint8_t lut_n[LUT_ROWS];

    /* bit-banged SPI on GPIO */
    bool sclk;
    bool sda_in;
    uint8_t shift;
    int bits;
    const uint8_t *rd;
    uint32_t rd_len;

    uint8_t ram[2][RAM_H][RAM_WB];
    float *ink;             /* H x W, 0 black .. 1 white */
    bool redraw;
};

static const uint8_t uc8279_ver[] = { 0x00, 0x03, 0x66, 0x00, 0x00 };

static void uc8279_set_busy(Uc8279State *s, uint64_t us)
{
    s->busy = true;
    qemu_set_irq(s->busy_n, 0);
    timer_mod(&s->busy_timer, qemu_clock_get_us(QEMU_CLOCK_VIRTUAL) + us);
}

static void uc8279_busy_done(void *opaque)
{
    Uc8279State *s = opaque;
    s->busy = false;
    qemu_set_irq(s->busy_n, 1);
}

/*
 * Returns the row's frame count and net ink movement, repeats included.
 * rail: 0 GND, 1 VDH, 2 VDL, 3 VDHR (not modeled by the ink approximation).
 */
static int uc8279_lut_walk(Uc8279State *s, int r, float *move)
{
    const uint8_t *row = s->lut[r];
    int n = s->lut_n[r], total = 0;
    int gsize = s->uc8253 ? 6 : 7;

    *move = 0;
    for (int g = 0; g + gsize <= n; g += gsize) {
        const uint8_t *grp = row + g;
        for (int ph = 0; ph < 4; ph++) {
            int rail, f, rep;
            if (s->uc8253) {
                rail = (grp[0] >> (6 - 2 * ph)) & 3;
                f = grp[1 + ph];
                rep = grp[5];
            } else {
                rail = grp[1 + ph] >> 6;
                f = grp[1 + ph] & 0x3f;
                rep = grp[0] * grp[5 + ph / 2];
            }
            total += f * rep;
            if (f > s->dead && (rail == 1 || rail == 2)) {
                *move += (rail == 2 ? 1.0f : -1.0f) *
                         (f - s->dead) / s->swing * rep;
            }
        }
    }
    return total;
}

/* Runs the refresh over the glass; returns its frame count. */
static int uc8279_refresh(Uc8279State *s, int row_frames[LUT_ROWS])
{
    static const int row_of[4] = { 4, 2, 3, 1 };    /* class OLD<<1|NEW: KK, KW, WK, WW */
    float move[LUT_ROWS] = { 0 };
    int frames = 0;
    bool lut = s->psr & PSR_REG;

    for (int r = 0; r < LUT_ROWS; r++) {
        row_frames[r] = lut ? uc8279_lut_walk(s, r, &move[r]) : 0;
        frames = MAX(frames, row_frames[r]);
    }
    /* The driver streams framebuffer row H-1-i into RAM row i. */
    for (int y = 0; y < H; y++) {
        int r = H - 1 - y;
        for (int x = 0; x < W; x++) {
            int o = (s->ram[PLANE_OLD][r][x / 8] >> (7 - x % 8)) & 1;
            int nw = (s->ram[PLANE_NEW][r][x / 8] >> (7 - x % 8)) & 1;
            float *p = &s->ink[y * W + x];
            if (lut) {
                *p = MIN(1.0f, MAX(0.0f, *p + move[row_of[o << 1 | nw]]));
            } else {
                *p = nw;
            }
        }
    }
    s->redraw = true;
    return lut ? frames : 40;
}

static unsigned uc8279_pll_hz(bool uc8253, uint8_t pll)
{
    static const uint8_t six_byte_rates[] = {
        5, 10, 15, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 130, 150, 200
    };
    unsigned frs = pll & 0x1f;

    if (uc8253) {
        return six_byte_rates[pll & 0x0f];
    }
    return frs < 24 ? 5 * (frs + 1) : 130 + 10 * (frs - 24);
}

static uint64_t uc8279_refresh_us(Uc8279State *s, unsigned frames)
{
    unsigned ref_hz = uc8279_pll_hz(s->uc8253, s->uc8253 ? 0x09 : 0x0f);
    uint64_t period = DIV_ROUND_UP((uint64_t)s->frame_us * ref_hz,
                                  uc8279_pll_hz(s->uc8253, s->pll));
    uint64_t us;

    if (s->busy_ms) {
        return (uint64_t)s->busy_ms * 1000;
    }
    us = (uint64_t)frames * MIN(period, UINT32_MAX) + s->refresh_overhead_us;
    return MIN(MAX(us, 1), (uint64_t)UINT32_MAX * 1000);
}

static void uc8279_set_window(Uc8279State *s, int xs, int xe, int ys, int ye)
{
    s->win[0] = MIN(xs, RAM_W - 1);
    s->win[1] = MIN(xe, RAM_W - 1);
    s->win[2] = MIN(ys, RAM_H - 1);
    s->win[3] = MIN(ye, RAM_H - 1);
}

static void uc8279_command(Uc8279State *s, uint8_t c)
{
    s->cmd = c;
    s->pos = 0;
    s->rd = NULL;
    s->rd_len = 0;
    if (c == 0x10 || c == 0x13) {
        s->plane_start_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    }

    switch (c) {
    case 0x02:  /* POF */
        uc8279_set_busy(s, (uint64_t)s->pof_ms * 1000);
        break;
    case 0x04:  /* PON */
        uc8279_set_busy(s, (uint64_t)s->pon_ms * 1000);
        break;
    case 0x12: {    /* DRF */
        int row_frames[LUT_ROWS];
        int frames = uc8279_refresh(s, row_frames);
        uint64_t us = uc8279_refresh_us(s, frames);

        trace_uc8279_refresh(row_frames[0], row_frames[1], row_frames[2],
                             row_frames[3], row_frames[4], s->pll,
                             DIV_ROUND_UP(us, 1000));
        uc8279_set_busy(s, us);
        break;
    }
    case 0x20 ... 0x24:
        s->lut_n[c - 0x20] = 0;
        break;
    case 0x91:  /* PTIN */
        s->partial = true;
        break;
    case 0x92:  /* PTOUT */
        s->partial = false;
        break;
    case 0x70:
        if (!s->uc8253) {
            s->rd = uc8279_ver;
            s->rd_len = sizeof(uc8279_ver);
        }
        break;
    }
}

static void uc8279_data(Uc8279State *s, uint8_t v)
{
    switch (s->cmd) {
    case 0x30:
        if (s->pos++ == 0) {
            s->pll = v & (s->uc8253 ? 0x0f : 0x1f);
        }
        break;
    case 0x00:
        if (s->pos++ == 0) {
            s->psr = v;
        }
        break;
    case 0x07:
        if (v == 0xA5) {
            s->asleep = true;
        }
        break;
    case 0x20 ... 0x24: {
        int r = s->cmd - 0x20;
        if (s->lut_n[r] < LUT_LEN) {
            s->lut[r][s->lut_n[r]++] = v;
        }
        break;
    }
    case 0x61:  /* TRES: HRES[15:8] HRES[7:0] VRES[15:8] VRES[7:0] */
        if (s->pos < 4) {
            s->winbuf[s->pos] = v;
        }
        if (++s->pos == 4) {
            s->tres_w = s->winbuf[0] << 8 | s->winbuf[1];
            s->tres_h = s->winbuf[2] << 8 | s->winbuf[3];
        }
        break;
    case 0x90:  /* PTL: HRST HRED VRST VRED (2 bytes each), PT_SCAN */
        if (s->pos < 9) {
            s->winbuf[s->pos] = v;
        }
        if (++s->pos == 8) {
            uc8279_set_window(s, (s->winbuf[0] << 8 | s->winbuf[1]) & ~7,
                              (s->winbuf[2] << 8 | s->winbuf[3]) | 7,
                              s->winbuf[4] << 8 | s->winbuf[5],
                              s->winbuf[6] << 8 | s->winbuf[7]);
        }
        break;
    case 0x10:
    case 0x13: {
        int xs, xe, ys, ye;
        if (s->partial) {
            xs = s->win[0]; xe = s->win[1]; ys = s->win[2]; ye = s->win[3];
        } else {
            xs = 0; xe = MIN(s->tres_w, RAM_W) - 1; ys = 0; ye = MIN(s->tres_h, RAM_H) - 1;
        }
        int wb = xe / 8 - xs / 8 + 1;
        int row = ys + s->pos / wb;
        if (row <= ye) {
            s->ram[s->cmd == 0x10 ? PLANE_OLD : PLANE_NEW][row][xs / 8 + s->pos % wb] = v;
            s->pos++;
            if (s->pos == wb * (ye - ys + 1)) {
                trace_uc8279_plane_write(s->cmd, s->pos,
                    qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - s->plane_start_ns);
            }
        }
        break;
    }
    }
}

static void uc8279_byte(Uc8279State *s, uint8_t v)
{
    if (s->in_reset || s->asleep) {
        return;
    }
    if (!s->dc) {
        uc8279_command(s, v);
    } else {
        uc8279_data(s, v);
    }
}

static uint32_t uc8279_transfer(SSIPeripheral *dev, uint32_t data)
{
    uc8279_byte(UC8279(dev), data);
    return 0xff;
}

static uint8_t uc8279_read_byte(Uc8279State *s)
{
    if (s->cmd == 0x71) {
        return s->busy ? 0x12 : 0x13;
    }
    if (!s->rd) {
        return 0xff;
    }
    return s->pos < s->rd_len ? s->rd[s->pos] : 0xff;
}

static bool uc8279_reading(Uc8279State *s)
{
    return s->dc && !s->in_reset && !s->uc8253 && (s->cmd == 0x71 || s->rd);
}

static void uc8279_present_bit(Uc8279State *s)
{
    if (uc8279_reading(s)) {
        qemu_set_irq(s->sda_out, (uc8279_read_byte(s) >> (7 - s->bits)) & 1);
    } else {
        qemu_set_irq(s->sda_out, 1);
    }
}

/*
 * The controller shifts read bits out on the falling edge, so a host may
 * sample while SCL is low or right after the rising edge.
 */
static void uc8279_set_sclk(void *opaque, int n, int level)
{
    Uc8279State *s = UC8279(opaque);
    bool rise = level && !s->sclk, fall = !level && s->sclk;

    s->sclk = level;
    if (s->parent_obj.cs) {     /* CS is active low */
        return;
    }
    if (uc8279_reading(s)) {
        if (fall) {
            if (++s->bits == 8) {
                s->bits = 0;
                s->pos++;
            }
            uc8279_present_bit(s);
        }
    } else if (rise) {
        s->shift = (s->shift << 1) | s->sda_in;
        if (++s->bits == 8) {
            s->bits = 0;
            uc8279_byte(s, s->shift);
        }
    }
}

static void uc8279_set_sda(void *opaque, int n, int level)
{
    UC8279(opaque)->sda_in = level != 0;
}

static void uc8279_set_dc(void *opaque, int n, int level)
{
    Uc8279State *s = UC8279(opaque);
    s->dc = level != 0;
    s->bits = 0;
    uc8279_present_bit(s);
}

static int uc8279_set_cs(SSIPeripheral *dev, bool level)
{
    Uc8279State *s = UC8279(dev);
    s->bits = 0;
    if (level) {
        qemu_set_irq(s->sda_out, 1);
    }
    return 0;
}

static void uc8279_reset_regs(Uc8279State *s)
{
    s->psr = 0x0F;
    s->pll = s->uc8253 ? 0x09 : 0x0f;
    s->partial = false;
    s->tres_w = RAM_W;
    s->tres_h = RAM_H;
    uc8279_set_window(s, 0, RAM_W - 1, 0, RAM_H - 1);
    memset(s->lut_n, 0, sizeof(s->lut_n));
}

static void uc8279_set_rst(void *opaque, int n, int level)
{
    Uc8279State *s = UC8279(opaque);
    if (!level) {
        s->in_reset = true;
    } else if (s->in_reset) {
        s->in_reset = false;
        s->asleep = false;
        uc8279_reset_regs(s);
        s->cmd = 0;
        s->bits = 0;
        s->rd = NULL;
        uc8279_present_bit(s);
    }
}

static bool uc8279_update_display(void *opaque)
{
    Uc8279State *s = opaque;
    DisplaySurface *surface = qemu_console_surface(s->con);

    if (!s->redraw) {
        return true;
    }
    s->redraw = false;
    uint32_t *d = (uint32_t *)surface_data(surface);
    int stride = surface_stride(surface) / 4;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            /* panel white is a light gray, black a dark one */
            uint8_t g = 0x10 + (int)(s->ink[y * W + x] * 0xE0 + 0.5f);
            int dx = s->portrait ? H - 1 - y : x, dy = s->portrait ? x : y;
            d[dy * stride + dx] = rgb_to_pixel32(g, g, g);
        }
    }
    qemu_console_update(s->con, 0, 0,
                        surface_width(surface), surface_height(surface));
    return true;
}

static void uc8279_invalidate(void *opaque)
{
    UC8279(opaque)->redraw = true;
}

static const GraphicHwOps uc8279_ops = {
    .invalidate = uc8279_invalidate,
    .gfx_update = uc8279_update_display,
};

static void uc8279_realize(SSIPeripheral *d, Error **errp)
{
    DeviceState *dev = DEVICE(d);
    Uc8279State *s = UC8279(d);

    if (!s->swing) {
        error_setg(errp, "uc8279: swing-frames must be at least 1");
        return;
    }
    memset(s->ram, 0xff, sizeof(s->ram));
    s->ink = g_new(float, H * W);
    for (int i = 0; i < H * W; i++) {
        s->ink[i] = 1;
    }
    uc8279_reset_regs(s);
    s->redraw = true;
    s->con = qemu_graphic_console_create(dev, 0, &uc8279_ops, s);
    qemu_console_resize(s->con, s->portrait ? H : W, s->portrait ? W : H);
    timer_init_us(&s->busy_timer, QEMU_CLOCK_VIRTUAL, uc8279_busy_done, s);
    qdev_init_gpio_in_named(dev, uc8279_set_dc, "dc", 1);
    qdev_init_gpio_in_named(dev, uc8279_set_rst, "rst", 1);
    qdev_init_gpio_in_named(dev, uc8279_set_sclk, "sclk", 1);
    qdev_init_gpio_in_named(dev, uc8279_set_sda, "sda", 1);
    qdev_init_gpio_out_named(dev, &s->busy_n, "busy", 1);
    qdev_init_gpio_out_named(dev, &s->sda_out, "sda-out", 1);
}

static const Property uc8279_properties[] = {
    DEFINE_PROP_BOOL("uc8253", Uc8279State, uc8253, false),
    DEFINE_PROP_BOOL("portrait", Uc8279State, portrait, true),
    DEFINE_PROP_UINT32("busy-ms", Uc8279State, busy_ms, 0),
    DEFINE_PROP_UINT32("frame-us", Uc8279State, frame_us, 20000),
    DEFINE_PROP_UINT32("refresh-overhead-us", Uc8279State,
                       refresh_overhead_us, 0),
    DEFINE_PROP_UINT32("pon-ms", Uc8279State, pon_ms, 2),
    DEFINE_PROP_UINT32("pof-ms", Uc8279State, pof_ms, 2),
    DEFINE_PROP_UINT8("swing-frames", Uc8279State, swing, 6),
    DEFINE_PROP_UINT8("dead-frames", Uc8279State, dead, 1),
};

static void uc8279_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SSIPeripheralClass *k = SSI_PERIPHERAL_CLASS(klass);

    k->realize = uc8279_realize;
    k->transfer = uc8279_transfer;
    k->set_cs = uc8279_set_cs;
    k->cs_polarity = SSI_CS_LOW;
    device_class_set_props(dc, uc8279_properties);
}

static const TypeInfo uc8279_info = {
    .name = TYPE_UC8279,
    .parent = TYPE_SSI_PERIPHERAL,
    .instance_size = sizeof(Uc8279State),
    .class_init = uc8279_class_init,
};

static void uc8279_register_types(void)
{
    type_register_static(&uc8279_info);
}

type_init(uc8279_register_types)
