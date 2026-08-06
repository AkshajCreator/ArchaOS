// src/ai.c — ArchaOS Assistant AI
// Hybrid: fuzzy Levenshtein lookup for known inputs,
//         GRU neural net inference for unknown inputs.
// All large GRU buffers are static to avoid stack overflow.

#include "ai.h"
#include "ai_data.h"
#include "ai_weights.h"
#include "vga.h"
#include "idt.h"
#include "kernel.h"
#include <stdint.h>
#include <stddef.h>

/* ============================================================
 * PORT I/O — RTC
 * ============================================================ */

static inline void ai_outb(uint16_t port, uint8_t val)
{ asm volatile("outb %0,%1"::"a"(val),"Nd"(port)); }

static inline uint8_t ai_inb(uint16_t port)
{ uint8_t r; asm volatile("inb %1,%0":"=a"(r):"Nd"(port)); return r; }

/* ============================================================
 * RTC
 * ============================================================ */

static uint8_t bcd2bin(uint8_t v) { return (v & 0x0F) + ((v >> 4) * 10); }

static void rtc_read(uint8_t *hour, uint8_t *min, uint8_t *sec,
                     uint8_t *day,  uint8_t *month)
{
    while (1) {
        ai_outb(0x70, 0x0A | 0x80);
        if (!(ai_inb(0x71) & 0x80)) break;
    }
    ai_outb(0x70, 0x00 | 0x80); *sec   = ai_inb(0x71);
    ai_outb(0x70, 0x02 | 0x80); *min   = ai_inb(0x71);
    ai_outb(0x70, 0x04 | 0x80); *hour  = ai_inb(0x71);
    ai_outb(0x70, 0x07 | 0x80); *day   = ai_inb(0x71);
    ai_outb(0x70, 0x08 | 0x80); *month = ai_inb(0x71);
    ai_outb(0x70, 0x0B | 0x80);
    uint8_t regB = ai_inb(0x71);
    if (!(regB & 0x04)) {
        *sec   = bcd2bin(*sec);   *min   = bcd2bin(*min);
        *hour  = bcd2bin(*hour);  *day   = bcd2bin(*day);
        *month = bcd2bin(*month);
    }
}

/* ============================================================
 * HELPERS
 * ============================================================ */

static int ai_strlen(const char *s)
{ int n = 0; while (s[n]) n++; return n; }

static void ai_strcpy(char *d, const char *s)
{ while ((*d++ = *s++)); }

static void ai_tolower(char *s)
{ for (int i = 0; s[i]; i++) if (s[i] >= 'A' && s[i] <= 'Z') s[i] += 32; }

static void ai_itoa2(char *buf, uint8_t v)
{ buf[0] = '0' + v / 10; buf[1] = '0' + v % 10; buf[2] = '\0'; }

/* ============================================================
 * LEVENSHTEIN DISTANCE
 * ============================================================ */

#define LEV_MAX 64

static int levenshtein(const char *a, const char *b)
{
    int la = ai_strlen(a), lb = ai_strlen(b);
    if (la > LEV_MAX) la = LEV_MAX;
    if (lb > LEV_MAX) lb = LEV_MAX;
    if (la < lb) {
        const char *t = a; a = b; b = t;
        int ti = la; la = lb; lb = ti;
    }
    static int prev[LEV_MAX + 1], curr[LEV_MAX + 1];
    for (int j = 0; j <= lb; j++) prev[j] = j;
    for (int i = 1; i <= la; i++) {
        curr[0] = i;
        for (int j = 1; j <= lb; j++) {
            int cost = (a[i-1] == b[j-1]) ? 0 : 1;
            int del  = prev[j]   + 1;
            int ins  = curr[j-1] + 1;
            int sub  = prev[j-1] + cost;
            curr[j]  = del < ins ? (del < sub ? del : sub)
            : (ins < sub ? ins : sub);
        }
        for (int j = 0; j <= lb; j++) prev[j] = curr[j];
    }
    return prev[lb];
}

/* ============================================================
 * FUZZY LOOKUP
 * ============================================================ */

#define MAX_DIST 4

static int fuzzy_find(const char *input)
{
    int best_idx = -1, best_dist = MAX_DIST + 1;
    for (int i = 0; i < AI_PAIR_COUNT; i++) {
        int d = levenshtein(input, AI_PAIRS[i].in);
        if (d < best_dist) {
            best_dist = d;
            best_idx  = i;
            if (d == 0) break;
        }
    }
    return (best_dist <= MAX_DIST) ? best_idx : -1;
}

/* ============================================================
 * RTC PRINT
 * ============================================================ */

static void print_time(void)
{
    uint8_t h, m, s, day, mon;
    char buf[4];
    rtc_read(&h, &m, &s, &day, &mon);
    vga_print("It is ");
    ai_itoa2(buf, h); vga_print(buf); vga_print(":");
    ai_itoa2(buf, m); vga_print(buf); vga_print(":");
    ai_itoa2(buf, s); vga_print(buf);
    vga_print(" (HH:MM:SS, 24-hour RTC)");
}

static void print_date(void)
{
    uint8_t h, m, s, day, mon;
    char buf[4];
    rtc_read(&h, &m, &s, &day, &mon);
    vga_print("Today is ");
    ai_itoa2(buf, day); vga_print(buf); vga_print("/");
    ai_itoa2(buf, mon); vga_print(buf);
    vga_print(" (DD/MM). Use the date command for the full timestamp.");
}

/* ============================================================
 * x87 FPU MATH
 * ============================================================ */

static float ai_expf(float x)
{
    if (x >  88.0f) return 3.40282347e+38f;
    if (x < -88.0f) return 0.0f;
    float result;
    asm volatile(
        "fldl2e         \n"
        "fmuls %1       \n"   /* fmuls = scalar float multiply */
        "fld  %%st(0)   \n"
        "frndint        \n"
        "fsub %%st,%%st(1)\n"
        "fxch           \n"
        "f2xm1          \n"
        "fld1           \n"
        "faddp          \n"
        "fscale         \n"
        "fstps %0       \n"   /* fstps = store scalar float */
        "fstp %%st(0)   \n"
        : "=m"(result) : "m"(x)
    );
    return result;
}

static inline float ai_sigmoidf(float x)
{ return 1.0f / (1.0f + ai_expf(-x)); }

static inline float ai_tanhf(float x)
{ float e2 = ai_expf(2.0f * x); return (e2 - 1.0f) / (e2 + 1.0f); }

/* ============================================================
 * VOCAB HELPERS
 * ============================================================ */

#define H AI_HIDDEN_SIZE
#define E AI_EMBED_SIZE
#define V AI_VOCAB_SIZE

static int char_to_idx(char c)
{
    for (int i = 0; i < AI_CHAR_MAP_SIZE; i++) {
        if (ai_char_keys[i] == (unsigned char)c)
            return ai_char_vals[i];
    }
    return AI_UNK;
}

static char idx_to_char(int idx)
{
    if (idx < 0 || idx >= AI_VOCAB_SIZE) return 0;
    return (char)ai_i2ch[idx];
}

/* ============================================================
 * MATRIX OPS
 * ============================================================ */

static void matvec_acc(float *dst, const float *W,
                       const float *src, int rows, int cols)
{
    for (int i = 0; i < rows; i++) {
        float sum = 0.0f;
        for (int j = 0; j < cols; j++) sum += W[i * cols + j] * src[j];
        dst[i] += sum;
    }
}

static void vec_bias(float *dst, const float *bias, int n)
{ for (int i = 0; i < n; i++) dst[i] = bias[i]; }

/* ============================================================
 * STATIC GRU BUFFERS — avoids stack overflow
 * H=512: ih/hh = 3*512*4 = 6KB each, hidden = 512*4 = 2KB
 * Total ~20KB in BSS, safe.
 * ============================================================ */

static float gru_ih[3 * H];
static float gru_hh[3 * H];
static float gru_r[H];
static float gru_z[H];
static float gru_n[H];
static float gru_x[E];
static float gru_hidden[H];

/* ============================================================
 * GRU CELL — uses static buffers above
 * ============================================================ */

static void gru_cell(float *h, const float *x_in,
                     const float *w_ih, const float *w_hh,
                     const float *b_ih, const float *b_hh)
{
    vec_bias(gru_ih, b_ih, 3 * H);
    matvec_acc(gru_ih, w_ih, x_in, 3 * H, E);

    vec_bias(gru_hh, b_hh, 3 * H);
    matvec_acc(gru_hh, w_hh, h, 3 * H, H);

    for (int i = 0; i < H; i++) gru_r[i] = ai_sigmoidf(gru_ih[i]       + gru_hh[i]);
    for (int i = 0; i < H; i++) gru_z[i] = ai_sigmoidf(gru_ih[H + i]   + gru_hh[H + i]);
    for (int i = 0; i < H; i++) gru_n[i] = ai_tanhf   (gru_ih[2*H + i] + gru_r[i] * gru_hh[2*H + i]);
    for (int i = 0; i < H; i++) h[i]     = (1.0f - gru_z[i]) * gru_n[i] + gru_z[i] * h[i];
}

/* ============================================================
 * ENCODER
 * ============================================================ */

static void gru_encode(const char *input)
{
    for (int i = 0; i < H; i++) gru_hidden[i] = 0.0f;

    /* SOS token */
    const float *emb = ai_enc_emb_weight + AI_SOS * E;
    for (int j = 0; j < E; j++) gru_x[j] = emb[j];
    gru_cell(gru_hidden, gru_x,
             ai_enc_gru_weight_ih, ai_enc_gru_weight_hh,
             ai_enc_gru_bias_ih,   ai_enc_gru_bias_hh);

    /* Input characters */
    for (int i = 0; input[i] && i < AI_MAX_IN; i++) {
        emb = ai_enc_emb_weight + char_to_idx(input[i]) * E;
        for (int j = 0; j < E; j++) gru_x[j] = emb[j];
        gru_cell(gru_hidden, gru_x,
                 ai_enc_gru_weight_ih, ai_enc_gru_weight_hh,
                 ai_enc_gru_bias_ih,   ai_enc_gru_bias_hh);
    }

    /* EOS token */
    emb = ai_enc_emb_weight + AI_EOS * E;
    for (int j = 0; j < E; j++) gru_x[j] = emb[j];
    gru_cell(gru_hidden, gru_x,
             ai_enc_gru_weight_ih, ai_enc_gru_weight_hh,
             ai_enc_gru_bias_ih,   ai_enc_gru_bias_hh);
}

/* ============================================================
 * DECODER
 * ============================================================ */

static void gru_decode(char *out, int max_out)
{
    int out_idx = 0, token = AI_SOS;

    for (int step = 0; step < max_out - 1; step++) {
        const float *emb = ai_dec_emb_weight + token * E;
        for (int j = 0; j < E; j++) gru_x[j] = emb[j];

        gru_cell(gru_hidden, gru_x,
                 ai_dec_gru_weight_ih, ai_dec_gru_weight_hh,
                 ai_dec_gru_bias_ih,   ai_dec_gru_bias_hh);

        /* Find best token via FC layer */
        int   best     = 0;
        float best_val = ai_dec_fc_bias[0];
        for (int j = 0; j < H; j++)
            best_val += ai_dec_fc_weight[j] * gru_hidden[j];

        for (int vv = 1; vv < V; vv++) {
            float val = ai_dec_fc_bias[vv];
            for (int j = 0; j < H; j++)
                val += ai_dec_fc_weight[vv * H + j] * gru_hidden[j];
            if (val > best_val) { best_val = val; best = vv; }
        }

        if (best == AI_EOS || best == AI_PAD) break;

        char c = idx_to_char(best);
        if (c) out[out_idx++] = c;
        token = best;
    }
    out[out_idx] = '\0';
}

/* ============================================================
 * GRU INFERENCE
 * ============================================================ */

static void gru_infer(const char *input, char *out, int max_out)
{
    gru_encode(input);
    gru_decode(out, max_out);
}

/* ============================================================
 * KEYBOARD
 * ============================================================ */

static const char kb_lo[128] = {
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
    'z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',
};
static const char kb_hi[128] = {
    0,27,'!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
    'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
    'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' ',
};

static uint8_t ai_wait_sc(void)
{
    while (!irq_kbd_fired) asm volatile("hlt");
    uint8_t sc = last_scancode;
    irq_kbd_fired = 0;
    return sc;
}

static char ai_translate(uint8_t sc, int shift, int caps)
{
    if (sc >= 128) return 0;
    char c = shift ? kb_hi[sc] : kb_lo[sc];
    if (caps && !shift) {
        if (c >= 'a' && c <= 'z') c -= 32;
    }
    return c;
}

/* ============================================================
 * PUBLIC API
 * ============================================================ */

void ai_init(void) { /* weights are static const — nothing to do */ }

void ai_chat(void)
{
    #define AI_INPUT_MAX 64
    #define AI_GRU_OUT   96

    char input[AI_INPUT_MAX];
    char low[AI_INPUT_MAX];
    char gru_out[AI_GRU_OUT];
    int  shift = 0, caps = 0, ext = 0;

    vga_print_color("\n ArchaOS Assistant AI\n", 0x0B);
    vga_print_color(" Type 'exit' to return to shell.\n\n", 0x07);

    while (1)
    {
        vga_print_color(" AI> ", 0x0E);

        /* Read input line */
        int len = 0;
        while (1)
        {
            uint8_t sc = ai_wait_sc();

            if (sc == 0xE0) { ext = 1; continue; }

            if (sc & 0x80) {
                uint8_t b = sc & 0x7F;
                if (b == 0x2A || b == 0x36) shift = 0;
                ext = 0;
                continue;
            }

            if (sc == 0x2A || sc == 0x36) { shift = 1; ext = 0; continue; }
            if (sc == 0x3A)               { caps = !caps; ext = 0; continue; }
            if (ext)                       { ext = 0; continue; }

            if (sc == 0x1C) { input[len] = '\0'; vga_print("\n"); break; }

            if (sc == 0x0E) {
                if (len > 0) {
                    len--;
                    vga_print_char('\b');
                    vga_print_char(' ');
                    vga_print_char('\b');
                }
                continue;
            }

            char c = ai_translate(sc, shift, caps);
            if (c && c != '\b' && c != '\t' && len < AI_INPUT_MAX - 1) {
                input[len++] = c;
                vga_print_char(c);
            }
        }

        if (len == 0) continue;

        /* Lowercase copy */
        ai_strcpy(low, input);
        ai_tolower(low);

        /* Exit check */
        if (low[0]=='e' && low[1]=='x' && low[2]=='i' &&
            low[3]=='t' && low[4]=='\0') {
            vga_print_color(" Goodbye! Returning to shell.\n\n", 0x0B);
        break;
            }

            vga_print_color("    ", 0x07);

            /* Step 1: fuzzy lookup */
            int idx = fuzzy_find(low);

            if (idx >= 0) {
                const char *resp = AI_PAIRS[idx].out;
                if      (resp[0] == '\x01') print_time();
                else if (resp[0] == '\x02') print_date();
                else                        vga_print(resp);
            } else {
                /* Step 2: GRU generates response for unknown input */
                gru_infer(low, gru_out, AI_GRU_OUT);
                if (gru_out[0])
                    vga_print(gru_out);
                else
                    vga_print("I am not sure about that. Try asking about ArchaOS!");
            }

            vga_print("\n\n");
    }
}
