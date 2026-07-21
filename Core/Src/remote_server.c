#include "remote_server.h"
#include "tls_manager.h"
#include "ethernet_manager.h"
#include "socket.h"
#include <stdio.h>
#include <string.h>

/* Authentication endpoint path - change to match server API. */
#define AUTHENTICATE_PATH      "/authenticate"
#define HTTP_REQUEST_BUF_SIZE  1024U
#define HTTP_SOCKET_NUM        0

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
    unsigned int close_status;
    RemoteServerError result = REMOTE_SERVER_OK;

    tls_err = tls_manager_init(&tls_ctx, &socket_fd);
    if (tls_err != TLS_MANAGER_OK) {
        return REMOTE_SERVER_TLS_ERROR;
    }

    tls_err = tls_manager_connect(&tls_ctx, ip, ip_len, port);
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

    /* Read response until the server closes the connection or the buffer is full. */
    while (total_read < response_buff_len - 1U) {
        tls_err = tls_manager_read(
            &tls_ctx,
            (unsigned char*)response_buff + total_read,
            (unsigned int)(response_buff_len - 1U - total_read),
            &chunk);

        if (chunk <= 0) {
            if (tls_err != TLS_MANAGER_OK) {
                result = REMOTE_SERVER_TLS_ERROR;
            }
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
        return https_authenticate(ip, 4U, server_port,
                                  http_request, (size_t)header_len + json_payload_len,
                                  response_buff, response_buff_len, bytes_read);
    }

    return http_authenticate(ip, 4U, server_port,
                             http_request, (size_t)header_len + json_payload_len,
                             response_buff, response_buff_len, bytes_read);
}

RemoteServerAuthResult remote_server_parse_auth_response(const char* response_buff, size_t response_buff_len){
    if(!response_buff || response_buff_len == 0){
        return REMOTE_SERVER_AUTH_FAILED;
    }
    if(strstr(response_buff, "200 OK") != NULL){
        return REMOTE_SERVER_AUTH_OK;
    }
    return REMOTE_SERVER_AUTH_FAILED;
}
