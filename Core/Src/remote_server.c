#include "remote_server.h"
#include "tls_manager.h"
#include "ethernet_manager.h"
#include "socket.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <string.h>

 static bool parse_ipv4(const char* str, uint8_t out[4]);
 static bool parse_http_status_code(const char* response_buff, size_t response_buff_len, int* status_code);

/* Authentication endpoint path - change to match server API. */
#define AUTHENTICATE_PATH      "/guest/validate"
#define INVALIDATE_PATH        "/invalidate"
#define HTTP_REQUEST_BUF_SIZE  1024U
#define HTTP_SOCKET_NUM        0
#define HTTPS_RESPONSE_TIMEOUT_MS 10000U

/* ---------- Plain HTTP (raw TCP) transport ---------- */

static RemoteServerError http_authenticate(
    const uint8_t* ip,
    size_t ip_len,
    uint16_t port,
    const unsigned char* request,
    size_t request_len,
    char* response_buff,
    size_t response_buff_len,
    int* bytes_read)
{
    int32_t ret;
    size_t total_read = 0U;
    RemoteServerError result = REMOTE_SERVER_OK;

    ret = socket(HTTP_SOCKET_NUM, Sn_MR_TCP, 0, 0x00);
    if (ret != HTTP_SOCKET_NUM) {
        return REMOTE_SERVER_SOCKET_ERROR;
    }

    ret = connect(HTTP_SOCKET_NUM, (uint8_t*)ip, port);
    if (ret != SOCK_OK) {
        close(HTTP_SOCKET_NUM);
        return REMOTE_SERVER_SOCKET_ERROR;
    }

    /* Send request */
    ret = send(HTTP_SOCKET_NUM, (uint8_t*)request, (uint16_t)request_len);
    if (ret <= 0) {
        disconnect(HTTP_SOCKET_NUM);
        close(HTTP_SOCKET_NUM);
        return REMOTE_SERVER_SOCKET_ERROR;
    }

    /* Read response until server closes or buffer full */
    while (total_read < response_buff_len - 1U) {
        ret = recv(HTTP_SOCKET_NUM,
                   (uint8_t*)response_buff + total_read,
                   (uint16_t)(response_buff_len - 1U - total_read));
        if (ret == SOCK_BUSY) {
            continue;
        }
        if (ret <= 0) {
            break;
        }
        total_read += (size_t)ret;
    }

    response_buff[total_read] = '\0';
    *bytes_read = (int)total_read;

    disconnect(HTTP_SOCKET_NUM);
    close(HTTP_SOCKET_NUM);

    return result;
}

/* ---------- HTTPS (TLS) transport ---------- */

static RemoteServerError https_authenticate(
    const uint8_t* ip,
    size_t ip_len,
    uint16_t port,
    const char* hostname,
    const unsigned char* request,
    size_t request_len,
    char* response_buff,
    size_t response_buff_len,
    int* bytes_read)
{
    wiz_tls_context tls_ctx;
    int socket_fd = HTTP_SOCKET_NUM;
    TlsManagerError tls_err;
    int written;
    int chunk;
    size_t total_read = 0U;
    uint32_t response_start;
    unsigned int close_status;
    RemoteServerError result = REMOTE_SERVER_OK;

    tls_err = tls_manager_init(&tls_ctx, &socket_fd);
    if (tls_err != TLS_MANAGER_OK) {
        return REMOTE_SERVER_TLS_ERROR;
    }

    tls_err = tls_manager_connect(&tls_ctx, ip, ip_len, port, hostname);
    if (tls_err != TLS_MANAGER_OK) {
        tls_manager_deinit(&tls_ctx);
        return REMOTE_SERVER_TLS_ERROR;
    }

    tls_err = tls_manager_write(
        &tls_ctx,
        (unsigned char*)request,
        (unsigned int)request_len,
        &written);
    if (tls_err != TLS_MANAGER_OK) {
        tls_manager_close_notify(&tls_ctx, &close_status);
        tls_manager_deinit(&tls_ctx);
        return REMOTE_SERVER_TLS_ERROR;
    }

    /* Wait through transient nonblocking reads; the server may not have
     * produced the response by the time the first TLS read is attempted. */
    response_start = HAL_GetTick();
    while (total_read < response_buff_len - 1U) {
        tls_err = tls_manager_read(
            &tls_ctx,
            (unsigned char*)response_buff + total_read,
            (unsigned int)(response_buff_len - 1U - total_read),
            &chunk);

        if (chunk == MBEDTLS_ERR_SSL_WANT_READ || chunk == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if ((HAL_GetTick() - response_start) >= HTTPS_RESPONSE_TIMEOUT_MS) {
                result = REMOTE_SERVER_TLS_ERROR;
                break;
            }
            HAL_Delay(1U);
            continue;
        }

        if (chunk == 0 || chunk == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            break;
        }

        if (chunk < 0 || tls_err != TLS_MANAGER_OK) {
            result = REMOTE_SERVER_TLS_ERROR;
            break;
        }

        total_read += (size_t)chunk;
    }

    response_buff[total_read] = '\0';
    *bytes_read = (int)total_read;

    tls_manager_close_notify(&tls_ctx, &close_status);
    tls_manager_deinit(&tls_ctx);

    return result;
}

RemoteServerError remote_server_invalidate(
    RemoteServerProtocol protocol,
    const char* server_addr,
    uint16_t server_port,
    const char* json_payload,
    size_t json_payload_len,
    char* response_buff,
    size_t response_buff_len,
    int* bytes_read)
{
    unsigned char http_request[HTTP_REQUEST_BUF_SIZE];
    uint8_t ip[4];
    int header_len;

    if (!server_addr || !json_payload || !response_buff || !bytes_read) {
        return REMOTE_SERVER_NULL_PTR;
    }
    if (json_payload_len == 0U || response_buff_len == 0U) {
        return REMOTE_SERVER_INVALID_LENGTH;
    }

    *bytes_read = 0;

    /* Resolve server address: try IPv4 literal first, then DNS. */
    if (!parse_ipv4(server_addr, ip)) {
        if (!ethernet_dns_resolve(server_addr, ip)) {
            return REMOTE_SERVER_SOCKET_ERROR;
        }
    }

    /* Build the HTTP/1.1 POST request for /invalidate */
    header_len = snprintf((char*)http_request, sizeof(http_request),
        "POST " INVALIDATE_PATH " HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n"
        "\r\n",
        server_addr,
        (unsigned int)json_payload_len);

    if (header_len < 0 ||
        (size_t)header_len + json_payload_len >= sizeof(http_request)) {
        return REMOTE_SERVER_INVALID_LENGTH;
    }

    memcpy(http_request + header_len, json_payload, json_payload_len);

    if (protocol == REMOTE_SERVER_PROTO_HTTPS) {
        return https_authenticate(ip, 4U, server_port, server_addr,
                                  http_request, (size_t)header_len + json_payload_len,
                                  response_buff, response_buff_len, bytes_read);
    }

    return http_authenticate(ip, 4U, server_port,
                             http_request, (size_t)header_len + json_payload_len,
                             response_buff, response_buff_len, bytes_read);
}


/* ---------- Public API ---------- */

/* Try to parse "a.b.c.d" into 4 bytes. Returns true if valid IPv4 literal. */
static bool parse_ipv4(const char* str, uint8_t out[4])
{
    unsigned int a, b, c, d;
    if (sscanf(str, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return false;
    }
    if (a > 255 || b > 255 || c > 255 || d > 255) {
        return false;
    }
    out[0] = (uint8_t)a;
    out[1] = (uint8_t)b;
    out[2] = (uint8_t)c;
    out[3] = (uint8_t)d;
    return true;
}

/* Parse HTTP status code from first response line.
 * Accepts both "HTTP/1.1 200 OK" and minimal "200" / "200 OK" forms. */
static bool parse_http_status_code(const char* response_buff, size_t response_buff_len, int* status_code)
{
    size_t line_end = 0U;
    size_t i;

    if (!response_buff || !status_code || response_buff_len == 0U) {
        return false;
    }

    /* Restrict parsing to first line. */
    while (line_end < response_buff_len && response_buff[line_end] != '\n' && response_buff[line_end] != '\0') {
        line_end++;
    }

    if (line_end == 0U) {
        return false;
    }

    /* Try standard HTTP status line: HTTP/<ver> <code> ... */
    if (line_end >= 5U &&
        response_buff[0] == 'H' && response_buff[1] == 'T' &&
        response_buff[2] == 'T' && response_buff[3] == 'P' &&
        response_buff[4] == '/') {

        i = 5U;

        while (i < line_end && response_buff[i] != ' ' && response_buff[i] != '\t' && response_buff[i] != '\r') {
            i++;
        }
        while (i < line_end && (response_buff[i] == ' ' || response_buff[i] == '\t')) {
            i++;
        }

        if (i + 2U < line_end &&
            response_buff[i] >= '0' && response_buff[i] <= '9' &&
            response_buff[i + 1U] >= '0' && response_buff[i + 1U] <= '9' &&
            response_buff[i + 2U] >= '0' && response_buff[i + 2U] <= '9') {
            *status_code = (int)(response_buff[i] - '0') * 100 +
                           (int)(response_buff[i + 1U] - '0') * 10 +
                           (int)(response_buff[i + 2U] - '0');
            return true;
        }
    }

    /* Fallback: first token is status code, e.g. "200" or "200 OK". */
    i = 0U;
    while (i < line_end && (response_buff[i] == ' ' || response_buff[i] == '\t' || response_buff[i] == '\r')) {
        i++;
    }

    if (i + 2U < line_end &&
        response_buff[i] >= '0' && response_buff[i] <= '9' &&
        response_buff[i + 1U] >= '0' && response_buff[i + 1U] <= '9' &&
        response_buff[i + 2U] >= '0' && response_buff[i + 2U] <= '9') {
        size_t next = i + 3U;
        if (next == line_end || response_buff[next] == ' ' || response_buff[next] == '\t' || response_buff[next] == '\r') {
            *status_code = (int)(response_buff[i] - '0') * 100 +
                           (int)(response_buff[i + 1U] - '0') * 10 +
                           (int)(response_buff[i + 2U] - '0');
            return true;
        }
    }

    return false;
}

RemoteServerError remote_server_authenticate(
    RemoteServerProtocol protocol,
    const char* server_addr,
    uint16_t server_port,
    const char* json_payload,
    size_t json_payload_len,
    char* response_buff,
    size_t response_buff_len,
    int* bytes_read)
{
    unsigned char http_request[HTTP_REQUEST_BUF_SIZE];
    uint8_t ip[4];
    int header_len;

    if (!server_addr || !json_payload || !response_buff || !bytes_read) {
        return REMOTE_SERVER_NULL_PTR;
    }
    if (json_payload_len == 0U || response_buff_len == 0U) {
        return REMOTE_SERVER_INVALID_LENGTH;
    }

    *bytes_read = 0;

    /* Resolve server address: try IPv4 literal first, then DNS. */
    if (!parse_ipv4(server_addr, ip)) {
        if (!ethernet_dns_resolve(server_addr, ip)) {
            return REMOTE_SERVER_SOCKET_ERROR;
        }
    }

    /* Build the HTTP/1.1 POST request. Use the original address string
     * as the Host header (works for both IP and hostname). */
    header_len = snprintf((char*)http_request, sizeof(http_request),
        "POST " AUTHENTICATE_PATH " HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n"
        "\r\n",
        server_addr,
        (unsigned int)json_payload_len);

    if (header_len < 0 ||
        (size_t)header_len + json_payload_len >= sizeof(http_request)) {
        return REMOTE_SERVER_INVALID_LENGTH;
    }

    memcpy(http_request + header_len, json_payload, json_payload_len);

    if (protocol == REMOTE_SERVER_PROTO_HTTPS) {
        return https_authenticate(ip, 4U, server_port, server_addr,
                                  http_request, (size_t)header_len + json_payload_len,
                                  response_buff, response_buff_len, bytes_read);
    }

    return http_authenticate(ip, 4U, server_port,
                             http_request, (size_t)header_len + json_payload_len,
                             response_buff, response_buff_len, bytes_read);
}


RemoteServerAuthResult remote_server_parse_auth_response(const char* response_buff, size_t response_buff_len){
    int http_status = 0;

    if(!response_buff || response_buff_len == 0){
        return REMOTE_SERVER_AUTH_FAILED;
    }

    if (!parse_http_status_code(response_buff, response_buff_len, &http_status)) {
        return REMOTE_SERVER_AUTH_FAILED;
    }

    if (http_status >= 200 && http_status < 300) {
        return REMOTE_SERVER_AUTH_OK;
    }

    return REMOTE_SERVER_AUTH_FAILED;
}
