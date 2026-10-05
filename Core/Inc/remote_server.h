#ifndef __REMOTE_SERVER_H__
#define __REMOTE_SERVER_H__

#include <stddef.h>
#include <stdint.h>

typedef enum {
    REMOTE_SERVER_OK = 0,
    REMOTE_SERVER_INVALID_LENGTH,
    REMOTE_SERVER_NULL_PTR,
    REMOTE_SERVER_TLS_ERROR,
    REMOTE_SERVER_SOCKET_ERROR,
}RemoteServerError;

typedef enum {
    REMOTE_SERVER_AUTH_OK = 0,
    REMOTE_SERVER_AUTH_FAILED,
}RemoteServerAuthResult;

typedef enum {
    REMOTE_SERVER_PROTO_HTTP = 0,
    REMOTE_SERVER_PROTO_HTTPS,
}RemoteServerProtocol;

RemoteServerError remote_server_authenticate(RemoteServerProtocol protocol, const char* server_addr, uint16_t server_port, const char* json_payload, size_t json_payload_len, char* response_buff, size_t response_buff_len, int* bytes_read);
RemoteServerError remote_server_invalidate(RemoteServerProtocol protocol, const char* server_addr, uint16_t server_port, const char* json_payload, size_t json_payload_len, char* response_buff, size_t response_buff_len,int* bytes_read);
RemoteServerAuthResult remote_server_parse_auth_response(const char* response_buff, size_t response_buff_len);

#endif // __REMOTE_SERVER_H__