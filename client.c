#include "client.h"
#include "message.h"
#include "security.h"
#include "json.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

struct Client {
    int         sock_fd;      /* connected socket, -1 if not connected */
    char       *server_ip;    /* target IP */
    int         port;         /* target port */
    Security   *sec;          /* encryption instance (NULL if disabled) */
    Message    *msg;          /* message buffer for partial reads */
};

Client *client_new(const char *server_ip, int port, const char *enc_key)
{
    if (!server_ip || port < 1 || port > 65535) return NULL;

    Client *c = calloc(1, sizeof(*c));
    if (!c) return NULL;

    c->sock_fd = -1;

    c->server_ip = malloc(strlen(server_ip) + 1);
    if (!c->server_ip) goto fail;
    strcpy(c->server_ip, server_ip);

    c->port  = port;
    c->msg   = message_new();
    if (!c->msg) goto fail;

    if (enc_key && *enc_key) {
        c->sec = security_new(enc_key);
        if (!c->sec) goto fail;
    }

    return c;

fail:
    free(c->server_ip);
    message_free(c->msg);
    security_free(c->sec);
    free(c);
    return NULL;
}

int client_connect(Client *c)
{
    if (!c) return -1;
    if (c->sock_fd >= 0) return 0; /* already connected */

    c->sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (c->sock_fd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)c->port);

    if (inet_pton(AF_INET, c->server_ip, &addr.sin_addr) != 1) {
        close(c->sock_fd);
        c->sock_fd = -1;
        return -1;
    }

    if (connect(c->sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(c->sock_fd);
        c->sock_fd = -1;
        return -1;
    }

    return 0;
}

ssize_t client_send(Client *c, const void *buf, size_t len)
{
    if (!c || !buf || len == 0) return -1;
    if (c->sock_fd < 0) { errno = ENOTCONN; return -1; }

    /* Encrypt if security is enabled */
    const uint8_t *to_send     = buf;
    size_t         to_send_len = len;
    uint8_t       *encrypted   = NULL;

    if (c->sec) {
        if (security_encrypt(c->sec, buf, len, &encrypted, &to_send_len) != 0)
            return -1;
        to_send = encrypted;
    }

    /* Frame the message */
    uint8_t *packed     = NULL;
    size_t   packed_len = 0;
    Message *tmp = message_new();
    if (!tmp) { free(encrypted); return -1; }
    int rc = message_pack(tmp, to_send, to_send_len, &packed, &packed_len);
    message_free(tmp);
    free(encrypted);

    if (rc != 0) return -1;

    /* Write to socket */
    ssize_t written = write(c->sock_fd, packed, packed_len);
    free(packed);

    if (written < 0) return -1;
    return (ssize_t)len;
}

ssize_t client_recv(Client *c, void *buf, size_t len)
{
    if (!c || !buf || len == 0) return -1;
    if (c->sock_fd < 0) { errno = ENOTCONN; return -1; }

    uint8_t sock_buf[4096];

    for (;;) {
        /* Try to extract a complete message from the buffer */
        uint8_t *payload     = NULL;
        size_t   payload_len = 0;
        int rc = message_unpack(c->msg, NULL, 0, &payload, &payload_len);
        if (rc == 1) {
            ssize_t result;
            if (c->sec) {
                uint8_t *plain     = NULL;
                size_t   plain_len = 0;
                if (security_decrypt(c->sec, payload, payload_len,
                                     &plain, &plain_len) != 0) {
                    free(payload);
                    return -1;
                }
                free(payload);
                if (plain_len > len) plain_len = len;
                memcpy(buf, plain, plain_len);
                result = (ssize_t)plain_len;
                free(plain);
            } else {
                if (payload_len > len) payload_len = len;
                memcpy(buf, payload, payload_len);
                result = (ssize_t)payload_len;
                free(payload);
            }
            return result;
        }
        if (rc < 0) return -1;

        /* Need more data */
        ssize_t n = read(c->sock_fd, sock_buf, sizeof(sock_buf));
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return 0; /* server closed */

        rc = message_unpack(c->msg, sock_buf, (size_t)n, &payload, &payload_len);
        if (rc == 1) {
            ssize_t result;
            if (c->sec) {
                uint8_t *plain     = NULL;
                size_t   plain_len = 0;
                if (security_decrypt(c->sec, payload, payload_len,
                                     &plain, &plain_len) != 0) {
                    free(payload);
                    return -1;
                }
                free(payload);
                if (plain_len > len) plain_len = len;
                memcpy(buf, plain, plain_len);
                result = (ssize_t)plain_len;
                free(plain);
            } else {
                if (payload_len > len) payload_len = len;
                memcpy(buf, payload, payload_len);
                result = (ssize_t)payload_len;
                free(payload);
            }
            return result;
        }
        if (rc < 0) return -1;
        /* rc == 0: keep reading */
    }
}

int client_send_json(Client *c, const JsonValue *obj)
{
    if (!c || !obj) return -1;

    char *json_str = json_stringify(obj);
    if (!json_str) return -1;

    ssize_t n = client_send(c, json_str, strlen(json_str));
    free(json_str);

    if (n < 0) return -1;
    return 0;
}

JsonValue *client_recv_json(Client *c)
{
    if (!c) return NULL;

    char buf[65536];
    ssize_t n = client_recv(c, buf, sizeof(buf) - 1);
    if (n <= 0) return NULL;

    buf[n] = '\0';
    return json_parse(buf);
}

void client_free(Client *c)
{
    if (!c) return;

    if (c->sock_fd >= 0) close(c->sock_fd);
    free(c->server_ip);
    security_free(c->sec);
    message_free(c->msg);

    memset(c, 0, sizeof(*c));
    free(c);
}
