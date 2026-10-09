/*
 * BlockBackend tests
 *
 * Copyright (c) 2017 Kevin Wolf <kwolf@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "block/block_int.h"
#include "system/block-backend.h"
#include "qapi/error.h"
#include "qemu/main-loop.h"

static BlockDriver plain_media_driver = {
    .format_name = "test-plain-media",
    .supports_backing = true,
    .bdrv_child_perm = bdrv_default_perms,
};

typedef struct TestMediaState {
    bool inserted;
    unsigned polls;
} TestMediaState;

static bool coroutine_fn test_media_is_inserted(BlockDriverState *bs)
{
    TestMediaState *s = bs->opaque;

    g_assert(qemu_in_coroutine());
    s->polls++;
    qemu_co_sleep_ns(QEMU_CLOCK_REALTIME, 1000);
    return s->inserted;
}

static BlockDriver polling_media_driver = {
    .format_name = "test-polling-media",
    .instance_size = sizeof(TestMediaState),
    .bdrv_co_is_inserted = test_media_is_inserted,
};

static void test_inserted_plain(void)
{
    BlockBackend *blk = blk_new(qemu_get_aio_context(), 0, BLK_PERM_ALL);
    BlockDriverState *bs = bdrv_new_open_driver(&plain_media_driver, NULL,
                                               BDRV_O_RDWR, &error_abort);

    g_assert_false(blk_is_inserted_main_loop(blk));
    blk_insert_bs(blk, bs, &error_abort);
    g_assert_true(blk_is_inserted_main_loop(blk));
    blk_remove_bs(blk);
    g_assert_false(blk_is_inserted_main_loop(blk));
    blk_insert_bs(blk, bs, &error_abort);
    g_assert_true(blk_is_inserted_main_loop(blk));
    blk_unref(blk);
    bdrv_unref(bs);
}

static void test_inserted_polling_child(void)
{
    BlockBackend *blk = blk_new(qemu_get_aio_context(), 0, BLK_PERM_ALL);
    BlockDriverState *bs = bdrv_new_open_driver(&plain_media_driver, NULL,
                                               BDRV_O_RDWR, &error_abort);
    BlockDriverState *child = bdrv_new_open_driver(&polling_media_driver, NULL,
                                                  0, &error_abort);
    TestMediaState *s = child->opaque;

    bdrv_graph_wrlock_drained();
    bdrv_set_backing_hd(bs, child, &error_abort);
    bdrv_graph_wrunlock();
    blk_insert_bs(blk, bs, &error_abort);

    g_assert_false(blk_is_inserted_main_loop(blk));
    g_assert_cmpuint(s->polls, ==, 1);
    /* Physical media changes need no virtual media-change notification. */
    s->inserted = true;
    g_assert_true(blk_is_inserted_main_loop(blk));
    g_assert_cmpuint(s->polls, ==, 2);
    s->inserted = false;
    g_assert_false(blk_is_inserted_main_loop(blk));
    g_assert_cmpuint(s->polls, ==, 3);

    bdrv_graph_wrlock_drained();
    bdrv_set_backing_hd(bs, NULL, &error_abort);
    bdrv_graph_wrunlock();
    g_assert_true(blk_is_inserted_main_loop(blk));
    g_assert_cmpuint(s->polls, ==, 3);
    blk_unref(blk);
    bdrv_unref(bs);
    bdrv_unref(child);
}

static void test_drain_aio_error_flush_cb(void *opaque, int ret)
{
    bool *completed = opaque;

    g_assert(ret == -ENOMEDIUM);
    *completed = true;
}

static void test_drain_aio_error(void)
{
    BlockBackend *blk = blk_new(qemu_get_aio_context(),
                                BLK_PERM_ALL, BLK_PERM_ALL);
    BlockAIOCB *acb;
    bool completed = false;

    acb = blk_aio_flush(blk, test_drain_aio_error_flush_cb, &completed);
    g_assert(acb != NULL);
    g_assert(completed == false);

    blk_drain(blk);
    g_assert(completed == true);

    blk_unref(blk);
}

static void test_drain_all_aio_error(void)
{
    BlockBackend *blk = blk_new(qemu_get_aio_context(),
                                BLK_PERM_ALL, BLK_PERM_ALL);
    BlockAIOCB *acb;
    bool completed = false;

    acb = blk_aio_flush(blk, test_drain_aio_error_flush_cb, &completed);
    g_assert(acb != NULL);
    g_assert(completed == false);

    blk_drain_all();
    g_assert(completed == true);

    blk_unref(blk);
}

int main(int argc, char **argv)
{
    bdrv_init();
    qemu_init_main_loop(&error_abort);

    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/block-backend/inserted_plain", test_inserted_plain);
    g_test_add_func("/block-backend/inserted_polling_child",
                    test_inserted_polling_child);
    g_test_add_func("/block-backend/drain_aio_error", test_drain_aio_error);
    g_test_add_func("/block-backend/drain_all_aio_error",
                    test_drain_all_aio_error);

    return g_test_run();
}
