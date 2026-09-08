// src/net/nim.c — NVIDIA NIM / OpenAI-compatible AI client with OS Awareness & Tool Execution

#include "nim.h"
#include "net.h"
#include "http.h"
#include "fs.h"
#include "mm.h"
#include "vga.h"
#include "kernel.h"
#include <stdint.h>

/* Default config: talk to the local host proxy at 10.0.2.2:8080 */
nim_config_t nim_cfg = {
    .proxy_ip   = NIM_DEFAULT_PROXY_IP,
    .proxy_port = NIM_DEFAULT_PROXY_PORT,
    .model      = "openai/gpt-oss-20b",
    .temperature = 0.6f,
    .max_tokens = 512,
};

static uint32_t str_len(const char *s) { uint32_t n=0; while(*s++)n++; return n; }
static void str_copy(char *d, const char *s, uint32_t n)
{
    uint32_t i=0;
    while (i < n-1 && s[i]) { d[i]=s[i]; i++; }
    d[i]='\0';
}
static void str_cat(char *d, const char *s, uint32_t max)
{
    uint32_t dlen=str_len(d);
    if (dlen >= max-1) return;
    str_copy(d+dlen, s, max-dlen);
}
static void u32_to_str(uint32_t v, char *out)
{
    if (!v) { out[0]='0'; out[1]='\0'; return; }
    char tmp[12]; int pos=0;
    while (v) { tmp[pos++]=(char)('0'+v%10); v/=10; }
    int i; for(i=0;i<pos;i++) out[i]=tmp[pos-1-i]; out[i]='\0';
}

/* ============================================================
 * JSON string escaping (escapes " and \ only — enough for prompts)
 * ============================================================ */
static void json_escape(const char *src, char *dst, uint32_t max)
{
    uint32_t pos = 0;
    while (*src && pos < max - 2) {
        if (*src == '"' || *src == '\\') dst[pos++] = '\\';
        dst[pos++] = *src++;
    }
    dst[pos] = '\0';
}

/* ============================================================
 * Tier 1 & 2: Build Live OS Telemetry and Knowledge Base Prompt
 * ============================================================ */
static void build_os_telemetry(char *out, uint32_t max_len)
{
    out[0] = '\0';
    str_cat(out, "You are ArchaOS AI, the intelligent assistant embedded in the ArchaOS x86 operating system.\\n", max_len);
    str_cat(out, "OS Architecture: 32-bit x86 Protected Mode, Monolithic Kernel, VFS Filesystem, Mode 13h GUI (App Studio, Snake, Notepad, Painter), C/Python interpreters, E1000 networking.\\n", max_len);
    str_cat(out, "Available Commands: ls, pwd, cd, cat, write <f> <t>, edit <f>, touch, rm, cp, mv, neofetch, calc, theme <dark|matrix|cyberpunk|nord|amber>, gui, ifconfig, ping, curl, ports, reboot.\\n", max_len);
    str_cat(out, "Agent Tool: If the user requests a system action or wants to run something, append [EXEC: <command>] to execute it in the OS.\\n", max_len);
    str_cat(out, "Current OS Telemetry:\\n", max_len);

    char pwd[64];
    fs_pwd(pwd, sizeof(pwd));
    str_cat(out, "- CWD: ", max_len);
    str_cat(out, pwd, max_len);
    str_cat(out, "\\n- Files: ", max_len);
    fs_node_t *cwd = fs_cwd();
    if (cwd && cwd->child_count > 0) {
        for (int i = 0; i < cwd->child_count; i++) {
            if (cwd->children[i]) {
                str_cat(out, cwd->children[i]->name, max_len);
                if (cwd->children[i]->type == FS_DIR) str_cat(out, "/", max_len);
                str_cat(out, " ", max_len);
            }
        }
    } else {
        str_cat(out, "(empty)", max_len);
    }
    str_cat(out, "\\n", max_len);

    mm_stats_t st = mm_stats();
    char mem_str[16];
    u32_to_str((uint32_t)(st.free / 1024), mem_str);
    str_cat(out, "- Free RAM: ", max_len);
    str_cat(out, mem_str, max_len);
    str_cat(out, " KB\\n", max_len);

    if (net_if.up) {
        char ip_str[16];
        ip_to_str(net_if.ip, ip_str);
        str_cat(out, "- IP: ", max_len);
        str_cat(out, ip_str, max_len);
        str_cat(out, "\\n", max_len);
    }

    str_cat(out, "Answer clearly and concisely (2 to 4 sentences).", max_len);
}

/* ============================================================
 * Tier 3: Agentic Tool Execution Handler
 * ============================================================ */
static void ai_exec_actions(char *text)
{
    while (1) {
        char *start = 0;
        for (int i = 0; text[i]; i++) {
            if (text[i]=='[' && text[i+1]=='E' && text[i+2]=='X' && text[i+3]=='E' && text[i+4]=='C' && text[i+5]==':') {
                start = text + i;
                break;
            }
        }
        if (!start) break;

        char *cmd_start = start + 6;
        while (*cmd_start == ' ') cmd_start++;

        char *end = cmd_start;
        while (*end && *end != ']') end++;

        if (*end == ']') {
            char cmd_buf[128];
            int clen = (int)(end - cmd_start);
            if (clen > 127) clen = 127;
            for (int k = 0; k < clen; k++) cmd_buf[k] = cmd_start[k];
            cmd_buf[clen] = '\0';

            /* Erase the [EXEC: ...] tag from the text */
            char *p = start;
            char *q = end + 1;
            while (*q) *p++ = *q++;
            *p = '\0';

            /* Print action banner and execute */
            vga_print_color("\n[AI Action] Running: ", 0x0B);
            vga_print_color(cmd_buf, 0x0E);
            vga_print("\n");
            kernel_execute_command(cmd_buf);
        } else {
            break;
        }
    }
}

/* ============================================================
 * Extract the "content" field value from a JSON response.
 * ============================================================ */
static int json_extract_content(const char *json, char *out, uint32_t max_len)
{
    const char *p = json;
    while (*p) {
        if (p[0]=='"' && p[1]=='c' && p[2]=='o' && p[3]=='n' && p[4]=='t' && p[5]=='e' && p[6]=='n' && p[7]=='t' && p[8]=='"') {
            p += 9;
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
            if (*p == ':') {
                p++;
                while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
                if (*p == '"') {
                    p++;
                    /* Found start of content string */
                    uint32_t pos = 0;
                    while (*p && pos < max_len - 1) {
                        if (*p == '\\' && *(p+1)) {
                            char esc = *(p+1);
                            if (esc == 'n')       { out[pos++] = '\n'; p += 2; }
                            else if (esc == 't')  { out[pos++] = '\t'; p += 2; }
                            else if (esc == '"')  { out[pos++] = '"';  p += 2; }
                            else if (esc == '\\') { out[pos++] = '\\'; p += 2; }
                            else if (esc == 'u' && p[2]=='0' && p[3]=='0' && p[4]=='3' && p[5]=='c') { out[pos++] = '<'; p += 6; }
                            else if (esc == 'u' && p[2]=='0' && p[3]=='0' && p[4]=='3' && p[5]=='e') { out[pos++] = '>'; p += 6; }
                            else if (esc == 'u' && p[2]=='0' && p[3]=='0' && p[4]=='2' && p[5]=='6') { out[pos++] = '&'; p += 6; }
                            else if (esc == 'u')  { p += 6; }
                            else                  { out[pos++] = esc;  p += 2; }
                        } else if (*p == '"') {
                            break;
                        } else {
                            out[pos++] = *p++;
                        }
                    }
                    out[pos] = '\0';

                    /* If response contains <think>...</think>, skip to the actual response */
                    const char *think_end = 0;
                    for (uint32_t k = 0; k + 7 < pos; k++) {
                        if (out[k]=='<' && out[k+1]=='/' && out[k+2]=='t' && out[k+3]=='h' &&
                            out[k+4]=='i' && out[k+5]=='n' && out[k+6]=='k' && out[k+7]=='>') {
                            think_end = out + k + 8;
                            break;
                        }
                    }
                    if (think_end) {
                        while (*think_end == '\n' || *think_end == '\r' || *think_end == ' ') think_end++;
                        uint32_t cp = 0;
                        while (*think_end) out[cp++] = *think_end++;
                        out[cp] = '\0';
                    }
                    return 1;
                }
            }
        }
        p++;
    }
    return 0;
}

/* ============================================================
 * Multi-Turn Conversation Memory & VFS Persistence
 * ============================================================ */
static nim_message_t chat_history[NIM_MAX_HISTORY];
static int chat_history_count = 0;
#define AI_CHAT_LOG_PATH "/sys/ai_chat.log"

void nim_history_save(void)
{
    static char log_buf[4096];
    log_buf[0] = '\0';
    for (int i = 0; i < chat_history_count; i++) {
        str_cat(log_buf, "[", sizeof(log_buf));
        str_cat(log_buf, chat_history[i].role, sizeof(log_buf));
        str_cat(log_buf, "] ", sizeof(log_buf));
        str_cat(log_buf, chat_history[i].content, sizeof(log_buf));
        str_cat(log_buf, "\n", sizeof(log_buf));
    }
    fs_write(AI_CHAT_LOG_PATH, log_buf, str_len(log_buf));
}

void nim_history_load(void)
{
    fs_node_t *f = fs_resolve(AI_CHAT_LOG_PATH);
    if (!f || !f->data || f->size == 0) return;
    /* Basic parser for persisted log entries if needed */
}

void nim_history_add(const char *role, const char *content)
{
    if (!role || !content || !content[0]) return;
    if (chat_history_count >= NIM_MAX_HISTORY) {
        for (int i = 0; i < NIM_MAX_HISTORY - 1; i++) {
            chat_history[i] = chat_history[i + 1];
        }
        chat_history_count = NIM_MAX_HISTORY - 1;
    }
    str_copy(chat_history[chat_history_count].role, role, sizeof(chat_history[0].role));
    str_copy(chat_history[chat_history_count].content, content, sizeof(chat_history[0].content));
    chat_history_count++;
    nim_history_save();
}

void nim_history_clear(void)
{
    chat_history_count = 0;
    fs_write(AI_CHAT_LOG_PATH, "", 0);
}

int nim_history_count(void)
{
    return chat_history_count;
}

const nim_message_t *nim_history_get(int idx)
{
    if (idx < 0 || idx >= chat_history_count) return 0;
    return &chat_history[idx];
}

/* ============================================================
 * nim_init
 * ============================================================ */
void nim_init(void)
{
    nim_history_load();
}

/* ============================================================
 * nim_query — send prompt to proxy, parse JSON, return content
 * ============================================================ */
int nim_query(const char *prompt, char *out_response, uint32_t max_len)
{
    if (!prompt || !out_response || max_len == 0) return 0;
    if (!net_if.up) {
        str_copy(out_response, "[NET] Network not configured", max_len);
        return 0;
    }

    /* Build JSON body with Tier 1 Knowledge, Telemetry & Multi-turn history */
    static char body[8192];
    static char escaped_prompt[2048];
    static char telemetry[2048];

    build_os_telemetry(telemetry, sizeof(telemetry));
    json_escape(prompt, escaped_prompt, sizeof(escaped_prompt));

    char tok_str[12];
    u32_to_str(nim_cfg.max_tokens, tok_str);

    char temp_str[8];
    temp_str[0] = (nim_cfg.temperature > 0.5f) ? '1' : '0';
    temp_str[1] = '\0';

    body[0] = '\0';
    str_cat(body, "{\"model\":\"", sizeof(body));
    str_cat(body, nim_cfg.model, sizeof(body));
    str_cat(body, "\",\"messages\":[{\"role\":\"system\",\"content\":\"", sizeof(body));
    str_cat(body, telemetry, sizeof(body));
    str_cat(body, "\"}", sizeof(body));

    /* Serialize existing conversation history turns */
    for (int i = 0; i < chat_history_count; i++) {
        static char esc_hist[512];
        json_escape(chat_history[i].content, esc_hist, sizeof(esc_hist));
        str_cat(body, ",{\"role\":\"", sizeof(body));
        str_cat(body, chat_history[i].role, sizeof(body));
        str_cat(body, "\",\"content\":\"", sizeof(body));
        str_cat(body, esc_hist, sizeof(body));
        str_cat(body, "\"}", sizeof(body));
    }

    /* Append current turn */
    str_cat(body, ",{\"role\":\"user\",\"content\":\"", sizeof(body));
    str_cat(body, escaped_prompt, sizeof(body));
    str_cat(body, "\"}],\"temperature\":", sizeof(body));
    str_cat(body, temp_str, sizeof(body));
    str_cat(body, ",\"max_tokens\":", sizeof(body));
    str_cat(body, tok_str, sizeof(body));
    str_cat(body, ",\"stream\":false}", sizeof(body));

    static char response[8192];
    response[0] = '\0';

    int status = http_post(0, nim_cfg.proxy_ip, nim_cfg.proxy_port,
                           "/v1/chat/completions",
                           body, 0,
                           response, sizeof(response));

    if (status == 0) {
        str_copy(out_response, "[NET] Connection failed", max_len);
        return 0;
    }
    if (status != 200) {
        if (response[0] != '\0') {
            str_copy(out_response, response, max_len);
        } else {
            str_copy(out_response, "[AI] Server error", max_len);
        }
        return 0;
    }

    if (!json_extract_content(response, out_response, max_len)) {
        str_copy(out_response, response, max_len);
        return 0;
    }

    /* Record turns in persistent memory */
    nim_history_add("user", prompt);
    nim_history_add("assistant", out_response);

    /* Execute any agent tool actions emitted by AI */
    ai_exec_actions(out_response);

    return 1;
}
