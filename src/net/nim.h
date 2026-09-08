// src/net/nim.h — NVIDIA NIM / OpenAI-compatible AI client
#ifndef NIM_H
#define NIM_H
#include <stdint.h>

/* Proxy host: 10.0.2.2 (QEMU gateway to host machine) port 8080 */
#define NIM_DEFAULT_PROXY_IP   0x0A000202u  /* 10.0.2.2 in host byte order */
#define NIM_DEFAULT_PROXY_PORT 8080

typedef struct {
    uint32_t    proxy_ip;        /* Resolved proxy IP (host byte order) */
    uint16_t    proxy_port;      /* Proxy port                          */
    const char *model;           /* e.g. "deepseek-ai/deepseek-r1"      */
    float       temperature;     /* 0.0 – 1.0, default 0.6              */
    uint32_t    max_tokens;      /* Max output tokens, default 512       */
} nim_config_t;

/* Global config — can be updated at runtime */
extern nim_config_t nim_cfg;

/* Initialise with defaults */
void nim_init(void);

/* Send a prompt and receive a response.
 * out_response is null-terminated on success.
 * Returns 1 on success, 0 on failure. */
int nim_query(const char *prompt, char *out_response, uint32_t max_len);

/* Multi-turn conversation memory */
#define NIM_MAX_HISTORY 10
#define NIM_MAX_MSG_LEN 512

typedef struct {
    char role[16];   /* "user" or "assistant" */
    char content[NIM_MAX_MSG_LEN];
} nim_message_t;

void           nim_history_add(const char *role, const char *content);
void           nim_history_clear(void);
void           nim_history_load(void);
void           nim_history_save(void);
int            nim_history_count(void);
const nim_message_t *nim_history_get(int idx);

#endif
