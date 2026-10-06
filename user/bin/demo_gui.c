#include <stdio.h>
#include <archaos.h>
#include <gui.h>

ARCHAOS_GUI_APP("Widget Suite");

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    int win = gui_create_window("ArchaOS Widget Suite", 230, 150);
    if (win < 0) {
        printf("Error: Failed to create GUI window!\n");
        return 1;
    }

    static const uint8_t suite_icon[7][7] = {
        {1, 1, 1, 1, 1, 1, 1},
        {1, 0, 0, 0, 0, 0, 1},
        {1, 0, 1, 1, 1, 0, 1},
        {1, 0, 1, 0, 1, 0, 1},
        {1, 0, 1, 1, 1, 0, 1},
        {1, 0, 0, 0, 0, 0, 1},
        {1, 1, 1, 1, 1, 1, 1}
    };
    gui_set_icon(win, suite_icon, GUI_COLOR_YELLOW);
    gui_set_author(win, "ArchaOS Team");

    /* Labels */
    gui_add_label(win, 10, 10, "User:");
    gui_add_label(win, 10, 32, "Progress:");

    /* Text Box (Interactive typing, focus, backspace) */
    gui_widget_t *txt_user = gui_add_textbox(win, 55, 8, 160, 14, "ChatGPT");

    /* Progress Bar */
    gui_widget_t *pbar = gui_add_progressbar(win, 70, 32, 145, 10, 30);

    /* Checkbox */
    gui_widget_t *chk_sound = gui_add_checkbox(win, 10, 52, "Enable Audio Feedback", 1);
    gui_widget_t *chk_turbo = gui_add_checkbox(win, 10, 70, "Turbo Execution Mode", 0);

    /* Interactive Buttons */
    gui_widget_t *btn_add   = gui_add_button(win, 10, 95, 60, 18, "+20% Bar");
    gui_widget_t *btn_clear = gui_add_button(win, 80, 95, 65, 18, "Clear Text");
    gui_widget_t *btn_close = gui_add_button(win, 155, 95, 60, 18, "Exit App");

    /* Status Label */
    gui_widget_t *lbl_status = gui_add_label(win, 10, 122, "Ready. Click widgets or type!");

    int cur_progress = 30;

    while (1) {
        gui_event_t ev;
        if (gui_poll_event(win, &ev)) {
            if (ev.type == GUI_EVENT_CLOSE) {
                break;
            }

            if (ev.type == GUI_EVENT_BUTTON_CLICK) {
                if (ev.widget_id == btn_add->id) {
                    cur_progress = (cur_progress + 20) % 120;
                    if (cur_progress > 100) cur_progress = 100;
                    gui_set_progress(pbar, cur_progress);
                    gui_set_text(lbl_status, "Progress bar updated!");
                } else if (ev.widget_id == btn_clear->id) {
                    gui_set_text(txt_user, "");
                    gui_set_text(lbl_status, "Text box cleared.");
                } else if (ev.widget_id == btn_close->id) {
                    break;
                }
            } else if (ev.type == GUI_EVENT_CHECKBOX_TOGGLE) {
                if (ev.widget_id == chk_sound->id) {
                    gui_set_text(lbl_status, chk_sound->checked ? "Audio: ON" : "Audio: OFF");
                } else if (ev.widget_id == chk_turbo->id) {
                    gui_set_text(lbl_status, chk_turbo->checked ? "Turbo: ON" : "Turbo: OFF");
                }
            } else if (ev.type == GUI_EVENT_TEXT_CHANGE) {
                if (ev.widget_id == txt_user->id) {
                    gui_set_text(lbl_status, "Typing in text box...");
                }
            }
        }
        sleep(20);
    }

    gui_close_window(win);
    return 0;
}
