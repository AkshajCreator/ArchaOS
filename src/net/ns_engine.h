#ifndef NS_ENGINE_H
#define NS_ENGINE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int x;
    int y;
    int w;
    int h;
    char text[64];
    char href[256];
    char src[256];
    char poster[128];
    char name[32];
    char placeholder[32];
    char onclick[128];
    char form_action[128];
    char form_method[16];
    bool is_input;
    bool is_button;
    bool is_link;
    bool is_image;
    bool is_video;
    bool is_audio;
    bool is_playing;
    uint8_t fg_col;
    uint8_t bg_col;
    uint8_t border_col;
} ns_interactive_elem_t;

#define NS_MAX_INTERACTIVE 128

extern ns_interactive_elem_t ns_elements[NS_MAX_INTERACTIVE];
extern int ns_element_count;

void ns_engine_init(void);
void ns_engine_parse_html(const char *html_data, size_t len, const char *base_url);
void ns_engine_render(int vx, int vy, int vw, int vh, int scroll_y, int nav_focus_idx);
int  ns_engine_get_content_height(void);
const char *ns_engine_get_title(void);
uint8_t ns_engine_get_bg_color(void);
uint8_t css_parse_color(const char *s, uint8_t default_col);
int  ns_engine_click_fallback(int page_x, int page_y, const char *base_url, char *out_nav_url, size_t nav_sz);

#ifdef __cplusplus
}
#endif

#endif /* NS_ENGINE_H */
