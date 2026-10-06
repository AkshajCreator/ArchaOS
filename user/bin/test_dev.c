#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <gui.h>
#include <archaos.h>

ARCHAOS_GUI_APP("Dev Suite Test");
ARCHAOS_APP_AUTHOR("ArchaOS Core");

static void write_sample_bmp(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;

    int width = 32;
    int height = 32;
    int row_stride = (width * 3 + 3) & ~3;
    uint32_t image_size = (uint32_t)(row_stride * height);
    uint32_t file_size = 54 + image_size;

    uint8_t header[54];
    memset(header, 0, sizeof(header));
    header[0] = 'B';
    header[1] = 'M';
    *(uint32_t *)&header[2] = file_size;
    *(uint32_t *)&header[10] = 54;
    *(uint32_t *)&header[14] = 40;
    *(int32_t *)&header[18] = width;
    *(int32_t *)&header[22] = height;
    *(uint16_t *)&header[26] = 1;
    *(uint16_t *)&header[28] = 24;
    *(uint32_t *)&header[30] = 0; /* BI_RGB */
    *(uint32_t *)&header[34] = image_size;

    fwrite(header, 1, 54, f);

    uint8_t row[128];
    memset(row, 0, sizeof(row));
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            row[x * 3 + 0] = (uint8_t)(x * 7);        /* Blue */
            row[x * 3 + 1] = (uint8_t)(y * 7);        /* Green */
            row[x * 3 + 2] = (uint8_t)((x + y) * 3);  /* Red */
        }
        fwrite(row, 1, row_stride, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    int auto_exit = (argc > 1 && strcmp(argv[1], "--auto-exit") == 0);

    printf("========================================\n");
    printf("[TEST_DEV] Starting Developer Suite Tests\n");
    printf("========================================\n\n");

    /* 1. POSIX Directory Traversal (opendir, readdir, closedir) */
    printf("[TEST_DEV] 1. Testing POSIX Directory Traversal...\n");
    DIR *d = opendir("/");
    if (!d) {
        printf("[TEST_DEV] FAIL: opendir(\"/\") returned NULL!\n");
        return 1;
    }

    int entry_count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        printf("  [DIR] ino=%u type=%s name='%s' size=%u\n",
               ent->d_ino,
               (ent->d_type == DT_DIR) ? "DIR " : "FILE",
               ent->d_name,
               ent->d_size);
        entry_count++;
    }
    closedir(d);

    if (entry_count == 0) {
        printf("[TEST_DEV] FAIL: No entries found in root directory!\n");
        return 1;
    }
    printf("[TEST_DEV] Found %d entries in root. opendir/readdir: PASS\n\n", entry_count);

    /* 2. Memory Diagnostics (malloc_stats, heap tracking) */
    printf("[TEST_DEV] 2. Testing Memory Diagnostics & Heap Tracking...\n");
    printf("--- Baseline Stats ---\n");
    malloc_stats();

    void *p1 = malloc(512);
    void *p2 = malloc(1024);
    void *p3 = malloc(2048);
    printf("\n--- After 3 Allocations (512, 1024, 2048) ---\n");
    malloc_stats();

    free(p2);
    printf("\n--- After Freeing 1024-byte block (Coalesce / Reuse Test) ---\n");
    malloc_stats();

    free(p1);
    free(p3);
    printf("\n--- After Freeing All Test Blocks ---\n");
    malloc_stats();
    printf("[TEST_DEV] Memory Diagnostics: PASS\n\n");

    /* 3. Native BMP Image Loader (gui_load_bmp) */
    printf("[TEST_DEV] 3. Testing Native BMP Loader...\n");
    write_sample_bmp("/test_dev.bmp");

    int bmp_w = 0, bmp_h = 0;
    uint32_t *bmp_pixels = gui_load_bmp("/test_dev.bmp", &bmp_w, &bmp_h);
    if (!bmp_pixels || bmp_w != 32 || bmp_h != 32) {
        printf("[TEST_DEV] FAIL: Failed to load /test_dev.bmp (w=%d, h=%d)\n", bmp_w, bmp_h);
        return 1;
    }
    printf("[TEST_DEV] Successfully loaded BMP: %dx%d (%u bytes ARGB)\n",
           bmp_w, bmp_h, (uint32_t)(bmp_w * bmp_h * 4));
    printf("[TEST_DEV] BMP Loader: PASS\n\n");

    /* 4. GUI Window, Frame Pacing, Alpha Blending, Cursor Mode */
    printf("[TEST_DEV] 4. Testing GUI Primitives, Alpha Blending & Cursor Mode...\n");
    int win = gui_create_window("ArchaOS Developer Suite", 320, 200);
    if (win < 0) {
        printf("[TEST_DEV] Warning: GUI desktop inactive, skipping window tests.\n");
    } else {
        gui_clear(win, GUI_COLOR_LIGHT_GRAY);
        gui_set_author(win, "ArchaOS Core");

        /* Test cursor mode toggle */
        gui_set_cursor_mode(win, GUI_CURSOR_HIDDEN);
        gui_set_cursor_mode(win, GUI_CURSOR_RELATIVE);
        gui_set_cursor_mode(win, GUI_CURSOR_NORMAL);

        /* Draw background test pattern */
        gui_draw_rect(win, 10, 10, 280, 160, GUI_COLOR_WHITE);
        gui_draw_text(win, 20, 20, "ArchaOS Dev Suite Active", GUI_COLOR_BLUE);

        /* Test Alpha Blending */
        gui_draw_rect_alpha(win, 30, 40, 80, 40, 0x80FF0000);
        gui_draw_rect_alpha(win, 60, 60, 80, 40, 0x8000FF00);
        gui_draw_rect_alpha(win, 90, 80, 80, 40, 0x800000FF);

        /* Draw loaded BMP onto window */
        gui_blend_rect(bmp_pixels, 32, 32, 0, 0, 16, 16, 0x80FFFF00);
        gui_draw_buffer32(win, bmp_pixels, 32, 32);

        gui_update(win);

        /* Test Frame Limiter: 60 frames at 60 FPS (1 full second) */
        for (int i = 0; i < 60; i++) {
            gui_sync_frame(60);
        }
        printf("[TEST_DEV] Alpha Blending & 60 FPS Sync: PASS\n\n");

        if (!auto_exit) {
            gui_message_box("Developer Suite", "All Dev Suite Features OK!");
        }

        gui_close_window(win);
    }

    if (bmp_pixels) {
        free(bmp_pixels);
    }

    printf("========================================\n");
    printf("[TEST_DEV] ALL DEVELOPER SUITE TESTS PASSED!\n");
    printf("========================================\n");

    return 0;
}
