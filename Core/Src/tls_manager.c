#include <tls_manager.h>

#include "mbedtls/debug.h"
#include "mbedtls/error.h"

#include "socket.h"
#include "w5500.h"
#include "stm32f4xx_hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_IP_ADDRESS_BUF_LENGTH 4U
#define TLS_DEBUG_LEVEL 5

extern SPI_HandleTypeDef hspi1;

/* RX prefetch buffer.
 *
 * Workaround for a W5500 module fault: socket-buffer SRAM readout becomes
 * persistently corrupted (high bits forced to 1, unstable re-reads) once
 * received data has been left sitting in the chip across a long host stall
 * (e.g. the RSA verify + debug printing during a TLS handshake). Register
 * reads stay correct and reads performed immediately after arrival are
 * always clean, so we drain every available byte into MCU RAM the moment
 * we touch the socket and serve mbedTLS from this buffer instead. */
#define TLS_PF_BUF_SIZE 3072U
static uint8_t  pf_buf[TLS_PF_BUF_SIZE];
static uint16_t pf_off = 0U;
static uint16_t pf_len = 0U;

/* Non-blocking: pull everything the chip currently holds into pf_buf. */
static void tls_prefetch_drain(int sn)
{
    uint16_t rsr;
    int32_t r;

    if (pf_len == 0U) {
        pf_off = 0U;
    }

    while ((rsr = getSn_RX_RSR((uint8_t)sn)) > 0U) {
        uint16_t end = (uint16_t)(pf_off + pf_len);
        uint16_t space = (uint16_t)(TLS_PF_BUF_SIZE - end);

        if (space == 0U) {
            break;
        }
        if (rsr > space) {
            rsr = space;
        }

        r = recv((uint8_t)sn, &pf_buf[end], rsr);
        if (r <= 0) {
            break;
        }
        pf_len += (uint16_t)r;
    }
}

static int tls_entropy_cb(void* data, unsigned char* output, size_t len)
{
    size_t i;
    (void)data;

    for (i = 0; i < len; ++i) {
        output[i] = (unsigned char)(rand() & 0xFF);
    }

    return 0;
}

static int tls_w5500_recv(void* ctx, unsigned char* buf, size_t len)
{
    int32_t ret;
    int sn;
    size_t n;

    if ((ctx == NULL) || (buf == NULL) || (len == 0U)) {
        return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    }

    sn = *((int*)ctx);

    /* Vacuum whatever the chip currently holds into MCU RAM. */
    tls_prefetch_drain(sn);

    if (pf_len == 0U) {
        /* Nothing buffered: block until at least one byte arrives, reading
         * directly into the prefetch buffer, then drain trailing segments
         * (e.g. ServerHelloDone right behind ServerKeyExchange). */
        ret = recv(sn, pf_buf, (uint16_t)sizeof(pf_buf));

        if (ret <= 0) {
            if (ret == SOCK_BUSY) {
                return MBEDTLS_ERR_SSL_WANT_READ;
            }
            if (ret == SOCKERR_TIMEOUT) {
                return MBEDTLS_ERR_SSL_TIMEOUT;
            }
            if (ret == SOCKERR_SOCKSTATUS || ret == SOCKERR_SOCKCLOSED) {
                return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
            }
            return (int)ret;
        }

        pf_off = 0U;
        pf_len = (uint16_t)ret;
        tls_prefetch_drain(sn);
    }

    n = (len < (size_t)pf_len) ? len : (size_t)pf_len;
    memcpy(buf, &pf_buf[pf_off], n);
    pf_off += (uint16_t)n;
    pf_len -= (uint16_t)n;
    if (pf_len == 0U) {
        pf_off = 0U;
    }

    return (int)n;
}

static int tls_w5500_send(void* ctx, const unsigned char* buf, size_t len)
{
    int32_t ret;

    if ((ctx == NULL) || (buf == NULL) || (len == 0U)) {
        return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    }

    ret = send(*((int*)ctx), (uint8_t*)buf, (uint16_t)len);

    if (ret > 0) {
        return (int)ret;
    }

    if (ret == SOCK_BUSY) {
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    }

    if (ret == SOCKERR_TIMEOUT) {
        return MBEDTLS_ERR_SSL_TIMEOUT;
    }

    return (int)ret;
}

static void tls_debug_cb(void* ctx, int level, const char* file, int line, const char* str)
{
    (void)ctx;
    (void)file;
    (void)line;

    if (level <= TLS_DEBUG_LEVEL) {
        printf("%s", str);
    }
}

TlsManagerError tls_manager_init(wiz_tls_context* ctx, int* server_fd)
{
    const char* pers = "w5500_tls";

    if (!ctx || !server_fd) {
        return TLS_MANAGER_NULL_PTR;
    }

    ctx->ctr_drbg = malloc(sizeof(mbedtls_ctr_drbg_context));
    ctx->ssl = malloc(sizeof(mbedtls_ssl_context));
    ctx->conf = malloc(sizeof(mbedtls_ssl_config));
    ctx->cacert = malloc(sizeof(mbedtls_x509_crt));

    if (!ctx->ctr_drbg || !ctx->ssl || !ctx->conf || !ctx->cacert) {
        tls_manager_deinit(ctx);
        return TLS_MANAGER_TLS_ERROR;
    }

    mbedtls_ctr_drbg_init(ctx->ctr_drbg);
    mbedtls_ssl_init(ctx->ssl);
    mbedtls_ssl_config_init(ctx->conf);
    mbedtls_x509_crt_init(ctx->cacert);

    srand(1U);

    if (mbedtls_ctr_drbg_seed(ctx->ctr_drbg, tls_entropy_cb, NULL,
                              (const unsigned char*)pers, strlen(pers)) != 0) {
        tls_manager_deinit(ctx);
        return TLS_MANAGER_TLS_ERROR;
    }

    if (mbedtls_ssl_config_defaults(ctx->conf,
                                    MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
        tls_manager_deinit(ctx);
        return TLS_MANAGER_TLS_ERROR;
    }

    mbedtls_ssl_conf_rng(ctx->conf, mbedtls_ctr_drbg_random, ctx->ctr_drbg);
    mbedtls_ssl_conf_authmode(ctx->conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_dbg(ctx->conf, tls_debug_cb, NULL);
    mbedtls_debug_set_threshold(TLS_DEBUG_LEVEL);

    if (mbedtls_ssl_setup(ctx->ssl, ctx->conf) != 0) {
        tls_manager_deinit(ctx);
        return TLS_MANAGER_TLS_ERROR;
    }

    mbedtls_ssl_set_bio(ctx->ssl, server_fd, tls_w5500_send, tls_w5500_recv, NULL);

    return TLS_MANAGER_OK;
}

TlsManagerError tls_manager_deinit(wiz_tls_context* ctx)
{
    if (!ctx) {
        return TLS_MANAGER_NULL_PTR;
    }

    if (ctx->ssl) {
        mbedtls_ssl_free(ctx->ssl);
        free(ctx->ssl);
        ctx->ssl = NULL;
    }
    if (ctx->conf) {
        mbedtls_ssl_config_free(ctx->conf);
        free(ctx->conf);
        ctx->conf = NULL;
    }
    if (ctx->ctr_drbg) {
        mbedtls_ctr_drbg_free(ctx->ctr_drbg);
        free(ctx->ctr_drbg);
        ctx->ctr_drbg = NULL;
    }
    if (ctx->cacert) {
        mbedtls_x509_crt_free(ctx->cacert);
        free(ctx->cacert);
        ctx->cacert = NULL;
    }

    return TLS_MANAGER_OK;
}

TlsManagerError tls_manager_connect(wiz_tls_context* ctx, const uint8_t* server_ip_buff, size_t server_ip_buf_len, uint16_t server_port)
{
    int ret;
    int sock_ret;
    char hostname[16];

    if (!ctx || !server_ip_buff || !ctx->ssl) {
        return TLS_MANAGER_NULL_PTR;
    }

    if (server_ip_buf_len < DEFAULT_IP_ADDRESS_BUF_LENGTH) {
        return TLS_MANAGER_INVALID_LENGTH;
    }

    /* Fresh connection: discard any stale prefetched bytes. */
    pf_off = 0U;
    pf_len = 0U;

    sock_ret = socket(0, Sn_MR_TCP, 0, 0x00);
    if (sock_ret != 0) {
        return TLS_MANAGER_TLS_ERROR;
    }

    sock_ret = connect(0, (uint8_t*)server_ip_buff, server_port);
    if (sock_ret != SOCK_OK) {
        return TLS_MANAGER_TLS_ERROR;
    }

    snprintf(hostname, sizeof(hostname), "%u.%u.%u.%u",
             server_ip_buff[0], server_ip_buff[1], server_ip_buff[2], server_ip_buff[3]);
    mbedtls_ssl_set_hostname(ctx->ssl, hostname);

    while ((ret = mbedtls_ssl_handshake(ctx->ssl)) != 0) {
        if ((ret != MBEDTLS_ERR_SSL_WANT_READ) && (ret != MBEDTLS_ERR_SSL_WANT_WRITE)) {
            return TLS_MANAGER_TLS_ERROR;
        }
    }

    return TLS_MANAGER_OK;
}

TlsManagerError tls_manager_read(wiz_tls_context* ctx, unsigned char* read_buff, unsigned int read_len, int* bytes_read)
{
    int ret;

    if (!ctx || !ctx->ssl || !read_buff || !bytes_read) {
        return TLS_MANAGER_NULL_PTR;
    }

    ret = mbedtls_ssl_read(ctx->ssl, read_buff, read_len);
    *bytes_read = ret;

    if (ret >= 0 || ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE ||
        ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
        return TLS_MANAGER_OK;
    }

    return TLS_MANAGER_TLS_ERROR;
}

TlsManagerError tls_manager_write(wiz_tls_context* ctx, unsigned char* write_buff, unsigned int write_len, int* bytes_written)
{
    int ret;

    if (!ctx || !ctx->ssl || !write_buff || !bytes_written) {
        return TLS_MANAGER_NULL_PTR;
    }

    ret = mbedtls_ssl_write(ctx->ssl, write_buff, write_len);
    *bytes_written = ret;

    if (ret >= 0 || ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
        return TLS_MANAGER_OK;
    }

    return TLS_MANAGER_TLS_ERROR;
}

TlsManagerError tls_manager_x509_verify(wiz_tls_context* ctx, unsigned int* verify_flags)
{
    uint32_t flags;

    if (!ctx || !ctx->ssl || !verify_flags) {
        return TLS_MANAGER_NULL_PTR;
    }

    flags = mbedtls_ssl_get_verify_result(ctx->ssl);
    *verify_flags = (unsigned int)flags;

    return (flags == 0U) ? TLS_MANAGER_OK : TLS_MANAGER_TLS_ERROR;
}

TlsManagerError tls_manager_close_notify(wiz_tls_context* ctx, unsigned int* close_status)
{
    int ret;

    if (!ctx || !ctx->ssl || !close_status) {
        return TLS_MANAGER_NULL_PTR;
    }

    do {
        ret = mbedtls_ssl_close_notify(ctx->ssl);
    } while (ret == MBEDTLS_ERR_SSL_WANT_WRITE);

    *close_status = (unsigned int)ret;
    return (ret == 0) ? TLS_MANAGER_OK : TLS_MANAGER_TLS_ERROR;
}