#include "js_engine.h"
#include "js_shim.h"
#include "../../serial.h"
#include "../../vga.h"
#include "../http.h"

static duk_context *ctx = 0;
static js_alert_cb_t alert_cb = 0;
static js_navigate_cb_t navigate_cb = 0;

void js_set_alert_callback(js_alert_cb_t cb) {
    alert_cb = cb;
}

void js_set_navigate_callback(js_navigate_cb_t cb) {
    navigate_cb = cb;
}

static uint32_t js_str_len(const char *s) { uint32_t n=0; while(s && *s++) n++; return n; }

/* console.log(...) implementation */
static duk_ret_t native_console_log(duk_context *ctx) {
    duk_idx_t n = duk_get_top(ctx);
    serial_printf(COM1_BASE, "[JS-Console] ");
    for (duk_idx_t i = 0; i < n; i++) {
        const char *s = duk_safe_to_string(ctx, i);
        if (s) {
            serial_printf(COM1_BASE, "%s ", s);
        }
    }
    serial_printf(COM1_BASE, "\n");
    return 0;
}

/* alert(msg) implementation */
static duk_ret_t native_alert(duk_context *ctx) {
    const char *msg = duk_safe_to_string(ctx, 0);
    if (msg) {
        serial_printf(COM1_BASE, "[JS-Alert] %s\n", msg);
        if (alert_cb) alert_cb(msg);
    }
    return 0;
}

/* document.write(html) implementation */
static duk_ret_t native_doc_write(duk_context *ctx) {
    duk_idx_t n = duk_get_top(ctx);
    for (duk_idx_t i = 0; i < n; i++) {
        const char *s = duk_safe_to_string(ctx, i);
        if (s) {
            serial_printf(COM1_BASE, "[JS-DocWrite] %s\n", s);
        }
    }
    return 0;
}

/* window.location setter/getter */
static duk_ret_t native_location_set(duk_context *ctx) {
    const char *url = duk_safe_to_string(ctx, 0);
    if (navigate_cb && url) {
        navigate_cb(url);
    }
    return 0;
}

/* fetch(url) implementation */
static duk_ret_t native_fetch(duk_context *ctx) {
    const char *url = duk_safe_to_string(ctx, 0);
    serial_printf(COM1_BASE, "[JS-Fetch] URL: %s\n", url ? url : "");
    /* Return a mock response object or basic promise */
    duk_push_object(ctx);
    duk_push_string(ctx, "200");
    duk_put_prop_string(ctx, -2, "status");
    duk_push_string(ctx, "OK");
    duk_put_prop_string(ctx, -2, "statusText");
    return 1;
}

static void bind_browser_dom(duk_context *ctx) {
    /* 1. Global console object */
    duk_push_object(ctx);
    duk_push_c_function(ctx, native_console_log, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "log");
    duk_push_c_function(ctx, native_console_log, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "warn");
    duk_push_c_function(ctx, native_console_log, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "error");
    duk_push_c_function(ctx, native_console_log, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "info");
    duk_put_global_string(ctx, "console");

    /* 2. Global alert() */
    duk_push_c_function(ctx, native_alert, 1);
    duk_put_global_string(ctx, "alert");

    /* 3. Global fetch() */
    duk_push_c_function(ctx, native_fetch, 1);
    duk_put_global_string(ctx, "fetch");

    /* 4. Global document object */
    duk_push_object(ctx);
    duk_push_c_function(ctx, native_doc_write, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "write");
    duk_push_c_function(ctx, native_doc_write, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "writeln");
    duk_push_string(ctx, "ArchaOS Web Document");
    duk_put_prop_string(ctx, -2, "title");
    duk_put_global_string(ctx, "document");

    /* 5. Global window object */
    duk_push_object(ctx);
    duk_push_int(ctx, 320);
    duk_put_prop_string(ctx, -2, "innerWidth");
    duk_push_int(ctx, 200);
    duk_put_prop_string(ctx, -2, "innerHeight");
    duk_push_c_function(ctx, native_alert, 1);
    duk_put_prop_string(ctx, -2, "alert");
    duk_push_c_function(ctx, native_location_set, 1);
    duk_put_prop_string(ctx, -2, "navigate");
    duk_put_global_string(ctx, "window");

    /* Helper JS polyfills and standard DOM query wrappers */
    const char *dom_bootstrap =
        "window.document = document;\n"
        "document.getElementById = function(id) {\n"
        "  return { id: id, innerText: '', innerHTML: '', value: '', style: {} };\n"
        "};\n"
        "document.querySelector = function(sel) {\n"
        "  return document.getElementById(sel);\n"
        "};\n"
        "document.createElement = function(tag) {\n"
        "  return { tagName: tag, innerText: '', style: {} };\n"
        "};\n";

    duk_eval_string(ctx, dom_bootstrap);
    duk_pop(ctx);
}

void js_engine_init(void) {
    if (ctx) return;
    ctx = duk_create_heap_default();
    if (!ctx) {
        serial_printf(COM1_BASE, "[JS] Failed to initialize Duktape heap!\n");
        return;
    }
    serial_printf(COM1_BASE, "[JS] Engine initialized successfully!\n");
    bind_browser_dom(ctx);
}

void js_engine_reset(void) {
    if (ctx) {
        duk_destroy_heap(ctx);
        ctx = 0;
    }
    js_engine_init();
}

int js_engine_eval(const char *code) {
    if (!ctx) js_engine_init();
    if (!ctx || !code) return -1;

    serial_printf(COM1_BASE, "[JS] Executing script (%u bytes)...\n", js_str_len(code));
    if (duk_peval_string(ctx, code) != 0) {
        const char *err = duk_safe_to_string(ctx, -1);
        serial_printf(COM1_BASE, "[JS-Error] %s\n", err ? err : "Unknown JS error");
        duk_pop(ctx);
        return -1;
    } else {
        if (!duk_is_undefined(ctx, -1)) {
            const char *res = duk_safe_to_string(ctx, -1);
            if (res && res[0]) {
                serial_printf(COM1_BASE, "[JS-Result] %s\n", res);
            }
        }
        duk_pop(ctx);
    }
    return 0;
}

int js_engine_exec_event(const char *code) {
    return js_engine_eval(code);
}

int js_engine_is_active(void) {
    return (ctx != 0);
}
