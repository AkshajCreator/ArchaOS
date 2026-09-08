#ifndef ARCHA_JS_ENGINE_H
#define ARCHA_JS_ENGINE_H

#include <stdint.h>
#include "duktape.h"

/* Initialize the JavaScript engine */
void js_engine_init(void);

/* Reset the JavaScript context for a newly loaded page */
void js_engine_reset(void);

/* Evaluate a string of JavaScript code */
int js_engine_eval(const char *code);

/* Execute an inline event handler (e.g. onclick="...") */
int js_engine_exec_event(const char *code);

/* Check if JavaScript engine is currently active */
int js_engine_is_active(void);

/* Callback from JS alert() */
typedef void (*js_alert_cb_t)(const char *msg);
void js_set_alert_callback(js_alert_cb_t cb);

/* Callback from JS window.location = ... */
typedef void (*js_navigate_cb_t)(const char *url);
void js_set_navigate_callback(js_navigate_cb_t cb);

#endif /* ARCHA_JS_ENGINE_H */
