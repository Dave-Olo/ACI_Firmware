/*
 * Minimal mbedTLS 3.6 LTS configuration for STM32F411 bare-metal
 * TLS 1.2 client over WIZnet W5500.
 *
 * Selected via CMake: MBEDTLS_CONFIG_FILE="mbedtls_user_config.h"
 */

#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H

/* ------------ Platform (bare metal, no OS) ------------ */
#define MBEDTLS_NO_PLATFORM_ENTROPY

/* ------------ TLS protocol ------------ */
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_SERVER_NAME_INDICATION

/* ------------ Key exchange ------------ */
#define MBEDTLS_KEY_EXCHANGE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED

/* ------------ Symmetric crypto / hashes ------------ */
#define MBEDTLS_AES_C
#define MBEDTLS_AES_ROM_TABLES
#define MBEDTLS_GCM_C
#define MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_CIPHER_C
#define MBEDTLS_MD_C
#define MBEDTLS_SHA224_C
#define MBEDTLS_SHA256_C

/* ------------ Public key crypto ------------ */
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_NIST_OPTIM

/* ------------ RNG ------------ */
#define MBEDTLS_CTR_DRBG_C

/* ------------ X.509 / parsing ------------ */
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_OID_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_BASE64_C

/* ------------ Diagnostics ------------ */
#define MBEDTLS_ERROR_C
#define MBEDTLS_DEBUG_C

/* ------------ RAM footprint tuning ------------ */
/* Incoming records up to 4KB (certificate chain fits; matches previous
 * working MBEDTLS_SSL_MAX_CONTENT_LEN=4096 setup). Outgoing 2KB is
 * plenty for the HTTP requests this device sends. */
#define MBEDTLS_SSL_IN_CONTENT_LEN  4096
#define MBEDTLS_SSL_OUT_CONTENT_LEN 2048
#define MBEDTLS_MPI_MAX_SIZE        512

#endif /* MBEDTLS_CONFIG_H */
