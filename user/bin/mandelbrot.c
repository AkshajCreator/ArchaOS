#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <archaos.h>
#include <gui.h>

ARCHAOS_APP_TITLE("Mandelbrot Explorer");


/*
 * ArchaOS Mandelbrot Explorer
 *
 * A fixed-point Mandelbrot renderer.
 *
 * No floating point is required.
 *
 * Coordinate format:
 *     16.16 fixed point
 *
 * Example:
 *     1.5 = 1.5 * 65536
 */

#define FP_SHIFT 16
#define FP_ONE   65536

#define CANVAS_W 360
#define CANVAS_H 220

#define MAX_ITER 64

typedef struct {
    int x;
    int y;
} Point;

/* ------------------------------------------------------------
 * Fixed-point helpers
 * ------------------------------------------------------------ */

static int fp_mul(int a, int b)
{
    return (int)(((long long)a * (long long)b) >> FP_SHIFT);
}

/*
 * Convert screen coordinate to Mandelbrot coordinate.
 */
static int map_x(int px, int center_x, int zoom)
{
    /*
     * Range at zoom=1:
     *
     * approximately -2.5 .. +1.0
     */
    int normalized;

    normalized =
    ((px - CANVAS_W / 2) * FP_ONE) /
    (CANVAS_W / 3);

    return center_x + normalized / zoom;
}

static int map_y(int py, int center_y, int zoom)
{
    int normalized;

    normalized =
    ((py - CANVAS_H / 2) * FP_ONE) /
    (CANVAS_W / 3);

    return center_y + normalized / zoom;
}

/* ------------------------------------------------------------
 * Mandelbrot calculation
 * ------------------------------------------------------------ */

static int mandelbrot(
    int cx,
    int cy,
    int iterations
)
{
    int x = 0;
    int y = 0;

    int i;

    for (i = 0; i < iterations; i++) {

        int xx = fp_mul(x, x);
        int yy = fp_mul(y, y);

        /*
         * Escape radius = 2.
         *
         * x² + y² > 4
         */
        if (xx + yy > 4 * FP_ONE)
            return i;

        /*
         * z = z² + c
         *
         * (x + yi)²
         *
         * real = x² - y²
         * imag = 2xy
         */
        int new_x =
        xx - yy + cx;

        int new_y =
        fp_mul(2 * FP_ONE, fp_mul(x, y))
        + cy;

        x = new_x;
        y = new_y;
    }

    return iterations;
}

/* ------------------------------------------------------------
 * Palette
 * ------------------------------------------------------------ */

static unsigned char color_for_iteration(
    int iteration,
    int iterations
)
{
    if (iteration >= iterations)
        return GUI_COLOR_BLACK;

    /*
     * Cycle through the available ArchaOS
     * 256-color palette.
     */
    switch (iteration % 15) {

        case 0:
            return GUI_COLOR_BLUE;

        case 1:
            return GUI_COLOR_CYAN;

        case 2:
            return GUI_COLOR_LIGHT_BLUE;

        case 3:
            return GUI_COLOR_GREEN;

        case 4:
            return GUI_COLOR_LIGHT_GREEN;

        case 5:
            return GUI_COLOR_YELLOW;

        case 6:
            return GUI_COLOR_BROWN;

        case 7:
            return GUI_COLOR_LIGHT_RED;

        case 8:
            return GUI_COLOR_RED;

        case 9:
            return GUI_COLOR_MAGENTA;

        case 10:
            return GUI_COLOR_LIGHT_CYAN;

        case 11:
            return GUI_COLOR_WHITE;

        case 12:
            return GUI_COLOR_DARK_GRAY;

        case 13:
            return GUI_COLOR_LIGHT_GRAY;

        default:
            return GUI_COLOR_BLUE;
    }
}

/* ------------------------------------------------------------
 * Renderer
 * ------------------------------------------------------------ */

static void render(
    int win,
    int center_x,
    int center_y,
    int zoom,
    int iterations,
    gui_widget_t *progress,
    gui_widget_t *status
)
{
    int y;

    gui_clear(
        win,
        GUI_COLOR_BLACK
    );

    gui_set_text(
        status,
        "Rendering..."
    );

    gui_set_progress(
        progress,
        0
    );

    for (y = 0; y < CANVAS_H; y++) {

        int x;

        for (x = 0; x < CANVAS_W; x++) {

            int cx =
            map_x(
                x,
                center_x,
                zoom
            );

            int cy =
            map_y(
                y,
                center_y,
                zoom
            );

            int iteration =
            mandelbrot(
                cx,
                cy,
                iterations
            );

            unsigned char color =
            color_for_iteration(
                iteration,
                iterations
            );

            gui_draw_pixel(
                win,
                x + 10,
                y + 10,
                color
            );
        }

        /*
         * Update the progress bar every few rows.
         */
        if ((y % 10) == 0) {

            int percent =
            (y * 100) / CANVAS_H;

            gui_set_progress(
                progress,
                percent
            );

            /*
             * Give the scheduler a chance to run.
             */
            sleep(1);
        }
    }

    gui_set_progress(
        progress,
        100
    );

    gui_set_text(
        status,
        "Render complete"
    );

    gui_update(win);
}

/* ------------------------------------------------------------
 * Main
 * ------------------------------------------------------------ */

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /*
     * --------------------------------------------------------
     * Create window
     * --------------------------------------------------------
     */

    int win =
    gui_create_window(
        "ArchaOS Mandelbrot Explorer",
        500,
        330
    );

    if (win < 0) {
        printf("Error: could not create window\n");
        return 1;
    }

    static const uint8_t mandelbrot_icon[7][7] = {
        {0, 0, 1, 1, 0, 0, 0},
        {0, 1, 1, 1, 1, 0, 0},
        {1, 1, 0, 1, 1, 1, 0},
        {1, 0, 0, 0, 1, 1, 1},
        {1, 1, 0, 1, 1, 1, 0},
        {0, 1, 1, 1, 1, 0, 0},
        {0, 0, 1, 1, 0, 0, 0}
    };
    gui_set_icon(win, mandelbrot_icon, GUI_COLOR_LIGHT_CYAN);
    gui_set_author(win, "ChatGPT");

    /*
     * --------------------------------------------------------
     * Controls
     * --------------------------------------------------------
     */

    gui_widget_t *btn_zoom_in =
    gui_add_button(
        win,
        10,
        245,
        105,
        22,
        "Zoom In"
    );

    gui_widget_t *btn_zoom_out =
    gui_add_button(
        win,
        120,
        245,
        105,
        22,
        "Zoom Out"
    );

    gui_widget_t *btn_reset =
    gui_add_button(
        win,
        230,
        245,
        105,
        22,
        "Reset"
    );

    gui_widget_t *btn_more =
    gui_add_button(
        win,
        340,
        245,
        70,
        22,
        "+ Iter"
    );

    gui_widget_t *btn_less =
    gui_add_button(
        win,
        415,
        245,
        70,
        22,
        "- Iter"
    );

    /*
     * --------------------------------------------------------
     * Status widgets
     * --------------------------------------------------------
     */

    gui_widget_t *progress =
    gui_add_progressbar(
        win,
        10,
        275,
        475,
        12,
        0
    );

    gui_widget_t *status =
    gui_add_label(
        win,
        10,
        295,
        "Initializing..."
    );

    /*
     * --------------------------------------------------------
     * Fractal state
     * --------------------------------------------------------
     */

    int center_x = -49152;
    int center_y = 0;

    int zoom = 1;

    int iterations = 32;

    /*
     * Initial render.
     */
    render(
        win,
        center_x,
        center_y,
        zoom,
        iterations,
        progress,
        status
    );

    /*
     * --------------------------------------------------------
     * Event loop
     * --------------------------------------------------------
     */

    while (1) {

        gui_event_t ev;

        if (gui_poll_event(win, &ev)) {

            /*
             * Window X
             */
            if (ev.type == GUI_EVENT_CLOSE) {
                break;
            }

            /*
             * Button handling
             */
            if (ev.type == GUI_EVENT_BUTTON_CLICK) {

                /*
                 * Zoom IN
                 */
                if (ev.widget_id == btn_zoom_in->id) {

                    if (zoom < 32)
                        zoom *= 2;

                    render(
                        win,
                        center_x,
                        center_y,
                        zoom,
                        iterations,
                        progress,
                        status
                    );
                }

                /*
                 * Zoom OUT
                 */
                else if (ev.widget_id ==
                    btn_zoom_out->id) {

                    if (zoom > 1)
                        zoom /= 2;

                    render(
                        win,
                        center_x,
                        center_y,
                        zoom,
                        iterations,
                        progress,
                        status
                    );
                    }

                    /*
                     * RESET
                     */
                    else if (ev.widget_id ==
                        btn_reset->id) {

                        center_x = -49152;
                    center_y = 0;
                    zoom = 1;
                    iterations = 32;

                    render(
                        win,
                        center_x,
                        center_y,
                        zoom,
                        iterations,
                        progress,
                        status
                    );
                        }

                        /*
                         * MORE ITERATIONS
                         */
                        else if (ev.widget_id ==
                            btn_more->id) {

                            if (iterations < MAX_ITER)
                                iterations += 8;

                            render(
                                win,
                                center_x,
                                center_y,
                                zoom,
                                iterations,
                                progress,
                                status
                            );
                            }

                            /*
                             * FEWER ITERATIONS
                             */
                            else if (ev.widget_id ==
                                btn_less->id) {

                                if (iterations > 8)
                                    iterations -= 8;

                                render(
                                    win,
                                    center_x,
                                    center_y,
                                    zoom,
                                    iterations,
                                    progress,
                                    status
                                );
                                }
            }
        }

        /*
         * Don't monopolize the CPU.
         */
        sleep(20);
    }

    /*
     * --------------------------------------------------------
     * Cleanup
     * --------------------------------------------------------
     */

    gui_close_window(win);

    return 0;
}
