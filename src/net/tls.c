// src/net/tls.c — Native HTTPS / TLS 1.2 Engine for ArchaOS
#include "tls.h"
#include "net.h"
#include "tcp.h"
#include "tls/inc/bearssl.h"
#include "../serial.h"
#include "../pit.h"

/* ============================================================
 * Insecure X.509 validator: Extracts and accepts server public key
 * Allows bare-metal HTTPS without embedding full 200 Root CA bundle.
 * ============================================================ */
typedef struct {
    const br_x509_class *vtable;
    br_x509_decoder_context dc;
    br_x509_pkey pkey;
    int is_first_cert;
} br_x509_insecure_context;

static void insecure_start_chain(const br_x509_class **ctx, const char *server_name) {
    (void)server_name;
    br_x509_insecure_context *xc = (br_x509_insecure_context *)ctx;
    xc->is_first_cert = 1;
}

static void insecure_start_cert(const br_x509_class **ctx, uint32_t length) {
    (void)length;
    br_x509_insecure_context *xc = (br_x509_insecure_context *)ctx;
    if (xc->is_first_cert) {
        br_x509_decoder_init(&xc->dc, 0, 0);
    }
}

static void insecure_append(const br_x509_class **ctx, const unsigned char *buf, size_t len) {
    br_x509_insecure_context *xc = (br_x509_insecure_context *)ctx;
    if (xc->is_first_cert) {
        br_x509_decoder_push(&xc->dc, buf, len);
    }
}

static void insecure_end_cert(const br_x509_class **ctx) {
    br_x509_insecure_context *xc = (br_x509_insecure_context *)ctx;
    if (xc->is_first_cert) {
        br_x509_pkey *pk = br_x509_decoder_get_pkey(&xc->dc);
        if (pk) xc->pkey = *pk;
        xc->is_first_cert = 0;
    }
}

static unsigned insecure_end_chain(const br_x509_class **ctx) {
    (void)ctx;
    return 0; /* 0 = Success */
}

static const br_x509_pkey *insecure_get_pkey(const br_x509_class *const *ctx, unsigned *usages) {
    br_x509_insecure_context *xc = (br_x509_insecure_context *)ctx;
    if (usages) *usages = BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN;
    return &xc->pkey;
}

static const br_x509_class br_x509_insecure_vtable = {
    sizeof(br_x509_insecure_context),
    insecure_start_chain,
    insecure_start_cert,
    insecure_append,
    insecure_end_cert,
    insecure_end_chain,
    insecure_get_pkey
};

/* ============================================================
 * TLS Engine State
 * ============================================================ */
static br_ssl_client_context    tls_client_ctx __attribute__((aligned(32)));
static br_x509_insecure_context tls_x509_ctx   __attribute__((aligned(32)));
static br_sslio_context         tls_io_ctx;
static unsigned char            tls_iobuf[BR_SSL_BUFSIZE_BIDI] __attribute__((aligned(32)));
static int                      tls_tcp_sock = -1;
static int                      tls_active = 0;

static int tls_sock_read(void *ctx, unsigned char *buf, size_t len) {
    int sock = *(int *)ctx;
    /* 5000ms timeout — TLS handshake requires multiple round trips;
     * Google sends Certificate chain in several TCP segments which may
     * need to be retransmitted. 500ms was too tight. */
    uint16_t got = tcp_recv(sock, buf, (uint16_t)len, 5000);
    serial_printf(COM1_BASE, "[TLS] sock_read want=%u got=%u\n",
                  (uint32_t)len, (uint32_t)got);
    if (got == 0) {
        serial_puts(COM1_BASE, "[TLS] sock_read TIMEOUT → BR_ERR_IO\n");
        return -1;
    }
    return (int)got;
}

static int tls_sock_write(void *ctx, const unsigned char *buf, size_t len) {
    int sock = *(int *)ctx;
    (void)buf;
    int sent = tcp_send(sock, buf, (uint16_t)len);
    serial_printf(COM1_BASE, "[TLS] sock_write len=%u sent=%d\n",
                  (uint32_t)len, sent);
    if (!sent) return -1;
    return (int)len;
}

/* ============================================================
 * Public TLS API
 * ============================================================ */
int tls_connect(uint32_t remote_ip, uint16_t remote_port, const char *server_name) {
    serial_printf(COM1_BASE, "[TLS] Connecting to %x:%u (SNI: %s)...\n",
                  remote_ip, remote_port, server_name ? server_name : "none");

    tls_tcp_sock = tcp_connect(remote_ip, remote_port);
    if (tls_tcp_sock < 0) {
        serial_puts(COM1_BASE, "[TLS] TCP connection failed\n");
        return -1;
    }

    /* Initialize BearSSL client */
    br_ssl_client_zero(&tls_client_ctx);
    br_ssl_engine_set_versions(&tls_client_ctx.eng, BR_TLS10, BR_TLS12);

    /* Setup X.509 engine */
    tls_x509_ctx.vtable = &br_x509_insecure_vtable;
    tls_x509_ctx.is_first_cert = 1;

    /* Comprehensive TLS 1.2 Cipher Suites */
    static const uint16_t suites[] = {
        BR_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
        BR_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
        BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
        BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
        BR_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
        BR_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
        BR_TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256,
        BR_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256,
        BR_TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA384,
        BR_TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA384,
        BR_TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA,
        BR_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA,
        BR_TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA,
        BR_TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA,
        BR_TLS_RSA_WITH_AES_128_GCM_SHA256,
        BR_TLS_RSA_WITH_AES_256_GCM_SHA384,
        BR_TLS_RSA_WITH_AES_128_CBC_SHA256,
        BR_TLS_RSA_WITH_AES_256_CBC_SHA256,
        BR_TLS_RSA_WITH_AES_128_CBC_SHA,
        BR_TLS_RSA_WITH_AES_256_CBC_SHA
    };
    br_ssl_engine_set_suites(&tls_client_ctx.eng, suites, sizeof(suites)/sizeof(suites[0]));

    br_ssl_client_set_default_rsapub(&tls_client_ctx);
    br_ssl_engine_set_default_rsavrfy(&tls_client_ctx.eng);
    br_ssl_engine_set_default_ecdsa(&tls_client_ctx.eng);
    br_ssl_engine_set_default_ec(&tls_client_ctx.eng);

    /* Hashes */
    static const br_hash_class *hashes[] = {
        &br_md5_vtable,
        &br_sha1_vtable,
        &br_sha224_vtable,
        &br_sha256_vtable,
        &br_sha384_vtable,
        &br_sha512_vtable
    };
    for (int id = br_md5_ID; id <= br_sha512_ID; id++) {
        br_ssl_engine_set_hash(&tls_client_ctx.eng, id, hashes[id - 1]);
    }

    br_ssl_engine_set_x509(&tls_client_ctx.eng, (const br_x509_class **)&tls_x509_ctx.vtable);
    br_ssl_engine_set_prf10(&tls_client_ctx.eng, &br_tls10_prf);
    br_ssl_engine_set_prf_sha256(&tls_client_ctx.eng, &br_tls12_sha256_prf);
    br_ssl_engine_set_prf_sha384(&tls_client_ctx.eng, &br_tls12_sha384_prf);

    /* Pure 32-bit constant-time software engines (safe for bare-metal without SSE alignment constraints) */
    br_ssl_engine_set_gcm(&tls_client_ctx.eng,
        &br_sslrec_in_gcm_vtable,
        &br_sslrec_out_gcm_vtable);
    br_ssl_engine_set_aes_ctr(&tls_client_ctx.eng, &br_aes_ct_ctr_vtable);
    br_ssl_engine_set_ghash(&tls_client_ctx.eng, &br_ghash_ctmul);

    br_ssl_engine_set_cbc(&tls_client_ctx.eng,
        &br_sslrec_in_cbc_vtable,
        &br_sslrec_out_cbc_vtable);
    br_ssl_engine_set_aes_cbc(&tls_client_ctx.eng,
        &br_aes_ct_cbcenc_vtable,
        &br_aes_ct_cbcdec_vtable);

    br_ssl_engine_set_chapol(&tls_client_ctx.eng,
        &br_sslrec_in_chapol_vtable,
        &br_sslrec_out_chapol_vtable);
    br_ssl_engine_set_chacha20(&tls_client_ctx.eng, &br_chacha20_ct_run);
    br_ssl_engine_set_poly1305(&tls_client_ctx.eng, &br_poly1305_ctmul_run);

    br_ssl_engine_set_buffer(&tls_client_ctx.eng, tls_iobuf, sizeof(tls_iobuf), 1);
    br_ssl_client_reset(&tls_client_ctx, server_name, 0);

    br_sslio_init(&tls_io_ctx, &tls_client_ctx.eng, tls_sock_read, &tls_tcp_sock, tls_sock_write, &tls_tcp_sock);

    tls_active = 1;
    serial_puts(COM1_BASE, "[TLS] Handshake context ready\n");
    return 0; /* Socket 0 for TLS */
}

int tls_send(int sock, const void *buf, uint16_t len) {
    (void)sock;
    if (!tls_active) { serial_puts(COM1_BASE, "[TLS] tls_send: not active\n"); return 0; }
    serial_printf(COM1_BASE, "[TLS] tls_send: starting write+handshake len=%u\n", (uint32_t)len);
    int ret = br_sslio_write_all(&tls_io_ctx, buf, len);
    if (ret < 0) {
        int err = br_ssl_engine_last_error(&tls_client_ctx.eng);
        serial_printf(COM1_BASE, "[TLS] Write/handshake failed (err=%d)\n", err);
        tls_active = 0;
        return 0;
    }
    serial_puts(COM1_BASE, "[TLS] tls_send: write done, flushing\n");
    int fret = br_sslio_flush(&tls_io_ctx);
    serial_printf(COM1_BASE, "[TLS] tls_send: flush ret=%d\n", fret);
    return 1;
}

uint16_t tls_recv(int sock, void *buf, uint16_t max_len, uint32_t timeout_ms) {
    (void)sock;
    (void)timeout_ms;
    if (!tls_active) { serial_puts(COM1_BASE, "[TLS] tls_recv: not active\n"); return 0; }
    serial_printf(COM1_BASE, "[TLS] tls_recv: reading max=%u\n", (uint32_t)max_len);
    int r = br_sslio_read(&tls_io_ctx, buf, max_len);
    serial_printf(COM1_BASE, "[TLS] tls_recv: got r=%d\n", r);
    if (r <= 0) {
        int err = br_ssl_engine_last_error(&tls_client_ctx.eng);
        serial_printf(COM1_BASE, "[TLS] tls_recv failed (err=%d)\n", err);
        tls_active = 0;
        return 0;
    }
    return (uint16_t)r;
}

void tls_close(int sock) {
    (void)sock;
    serial_puts(COM1_BASE, "[TLS] tls_close called\n");
    tls_active = 0;
    if (tls_tcp_sock >= 0) {
        tcp_close(tls_tcp_sock);
        tls_tcp_sock = -1;
    }
}
