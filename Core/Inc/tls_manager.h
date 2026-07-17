#ifndef __TLS_MANAGER_H__
#define __TLS_MANAGER_H__

#include <stddef.h>
#include <stdint.h>

#include "mbedtls/build_info.h"
#include "mbedtls/ssl.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"

typedef struct {
    mbedtls_ctr_drbg_context* ctr_drbg;
    mbedtls_ssl_context* ssl;
    mbedtls_ssl_config* conf;
    mbedtls_x509_crt* cacert;
} wiz_tls_context;

typedef enum {
    TLS_MANAGER_OK = 0,
    TLS_MANAGER_INVALID_LENGTH,
    TLS_MANAGER_NULL_PTR,
    TLS_MANAGER_TLS_ERROR,
}TlsManagerError;

/* Backward-compatible alias for old typo. */
#define TLS_MANAGER_INVALID_LENGHT TLS_MANAGER_INVALID_LENGTH


TlsManagerError tls_manager_init(wiz_tls_context* ctx, int* server_fd);

TlsManagerError tls_manager_deinit(wiz_tls_context* ctx);

TlsManagerError tls_manager_connect(wiz_tls_context* ctx, const uint8_t* server_ip_buff, size_t server_ip_buf_len, uint16_t server_port);

TlsManagerError tls_manager_read(wiz_tls_context* ctx, unsigned char* read_buff, unsigned int read_len, int* bytes_read);

TlsManagerError tls_manager_write(wiz_tls_context* ctx, unsigned char* write_buff, unsigned int write_len, int* bytes_written);

TlsManagerError tls_manager_x509_verify(wiz_tls_context* ctx, unsigned int* verify_flags);

TlsManagerError tls_manager_close_notify(wiz_tls_context* ctx, unsigned int* close_status);

#endif