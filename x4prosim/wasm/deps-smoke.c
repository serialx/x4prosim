/* Link and run with the wasm sysroot's static GLib and pixman libraries. */
#include <glib.h>
#include <pixman.h>
#include <stdio.h>

static gboolean quit_loop(gpointer data)
{
    g_main_loop_quit(data);
    return G_SOURCE_REMOVE;
}

int main(void)
{
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    GHashTable *table = g_hash_table_new(g_str_hash, g_str_equal);
    pixman_image_t *image = pixman_image_create_bits(PIXMAN_a8r8g8b8,
                                                   8, 8, NULL, 0);

    g_hash_table_insert(table, "status", "ok");
    if (!image || !g_hash_table_lookup(table, "status")) {
        return 1;
    }
    g_idle_add(quit_loop, loop);
    g_main_loop_run(loop);
    puts(g_hash_table_lookup(table, "status"));
    pixman_image_unref(image);
    g_hash_table_destroy(table);
    g_main_loop_unref(loop);
    return 0;
}
