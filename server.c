#include "server.h"
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

/* Per-client state tracked by the server */
typedef struct {
    int      fd;       /* client socket, -1 if slot is free */
    Message *msg;      /* per-client message buffer for partial reads */
} ClientSlot;

struct Server {
    int         listen_fd;   /* listening socket */
    Security   *sec;         /* shared security instance (NULL if encryption disabled) */
    ClientSlot *clients;     /* dynamic array of client slots */
    size_t      num_clients; /* number of slots in use */
    size_t      cap_clients; /* allocated capacity */
};

/* Find or allocate a slot for @fd. Returns NULL if no capacity. */
static ClientSlot *server_find_slot(Server *s, int fd)
{
    /* search for existing */
    for (size_t i = 0; i < s->num_clients; i++) {
        if (s->clients[i].fd == fd) return &s->clients[i];
    }
    return NULL;
}

/* Allocate a new slot for @fd. Returns the slot or NULL. */
static ClientSlot *server_add_slot(Server *s, int fd)
{
    /* try to reuse a freed slot (fd == -1) first */
    for (size_t i = 0; i < s->num_clients; i++) {
        if (s->clients[i].fd == -1) {
            s->clients[i].fd  = fd;
            s->clients[i].msg = message_new();
            if (!s->clients[i].msg) {
                s->clients[i].fd = -1;
                return NULL;
            }
            return &s->clients[i];
        }
    }

    /* need to grow the array */
    if (s->num_clients >= s->cap_clients) {
        size_t new_cap = s->cap_clients ? s->cap_clients * 2 : 8;
        ClientSlot *p = realloc(s->clients, new_cap * sizeof(ClientSlot));
        if (!p) return NULL;
        s->clients    = p;
        s->cap_clients = new_cap;
    }

    ClientSlot *slot = &s->clients[s->num_clients++];
    slot->fd  = fd;
    slot->msg = message_new();
    if (!slot->msg) {
        slot->fd = -1;
        return NULL;
    }
    return slot;
}

/* Remove a slot (close fd and free message buffer) */
static void server_remove_slot(Server *s, int fd)
{
    for (size_t i = 0; i < s->num_clients; i++) {
        if (s->clients[i].fd == fd) {
            close(fd);
            message_free(s->clients[i].msg);
            s->clients[i].fd  = -1;
            s->clients[i].msg = NULL;
            return;
        }
    }
}

Server *server_new(int port, const char *enc_key)
{
    Server *s = calloc(1, sizeof(*s));
    if (!s) return NULL;

    s->listen_fd = -1;

    /* Create encryption layer if a key was provided */
    if (enc_key && *enc_key) {
        s->sec = security_new(enc_key);
        if (!s->sec) goto fail;
    }

    /* Create socket */
    s->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s->listen_fd < 0) goto fail;

    /* Allow address reuse */
    int opt = 1;
    setsockopt(s->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* Bind */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)port);

    if (bind(s->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        goto fail;

    /* Listen */
    if (listen(s->listen_fd, SOMAXCONN) < 0) goto fail;

    return s;

fail:
    if (s->listen_fd >= 0) close(s->listen_fd);
    security_free(s->sec);
    free(s);
    return NULL;
}

int server_accept(Server *s)
{
    if (!s || s->listen_fd < 0) return -1;

    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    int client_fd = accept(s->listen_fd,
                           (struct sockaddr *)&client_addr, &addr_len);
    if (client_fd < 0) return -1;

    /* Allocate tracking slot */
    if (!server_add_slot(s, client_fd)) {
        close(client_fd);
        return -1;
    }

    return client_fd;
}

ssize_t server_recv(Server *s, int client_fd, void *buf, size_t len)
{
    if (!s || !buf || len == 0) return -1;

    ClientSlot *slot = server_find_slot(s, client_fd);
    if (!slot) { errno = EBADF; return -1; }

    uint8_t sock_buf[4096];

    /* Keep reading from the socket until we have a complete message */
    for (;;) {
        /* Try to extract a message from the buffer first */
        uint8_t *payload   = NULL;
        size_t   payload_len = 0;
        int rc = message_unpack(slot->msg, NULL, 0, &payload, &payload_len);
        if (rc == 1) {
            /* Got a complete message — decrypt if needed */
            ssize_t result;
            if (s->sec) {
                uint8_t *plain     = NULL;
                size_t   plain_len = 0;
                if (security_decrypt(s->sec, payload, payload_len,
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

        /* Need more data — read from socket */
        ssize_t n = read(client_fd, sock_buf, sizeof(sock_buf));
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) {
            /* Client closed connection */
            return 0;
        }

        /* Feed new data into the message buffer, then loop to try unpacking */
        rc = message_unpack(slot->msg, sock_buf, (size_t)n, &payload, &payload_len);
        if (rc == 1) {
            ssize_t result;
            if (s->sec) {
                uint8_t *plain     = NULL;
                size_t   plain_len = 0;
                if (security_decrypt(s->sec, payload, payload_len,
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
        /* rc == 0: still need more data, loop and read again */
    }
}

ssize_t server_send(Server *s, int client_fd, const void *buf, size_t len)
{
    if (!s || !buf || len == 0) return -1;

    /* Check client exists */
    if (!server_find_slot(s, client_fd)) { errno = EBADF; return -1; }

    /* Encrypt if security is enabled */
    const uint8_t *to_send    = buf;
    size_t         to_send_len = len;
    uint8_t       *encrypted  = NULL;

    if (s->sec) {
        if (security_encrypt(s->sec, buf, len, &encrypted, &to_send_len) != 0)
            return -1;
        to_send = encrypted;
    }

    /* Frame the message */
    uint8_t *packed   = NULL;
    size_t   packed_len = 0;
    /* Use a dummy Message for packing (pack is stateless anyway) */
    Message *tmp = message_new();
    if (!tmp) { free(encrypted); return -1; }
    int rc = message_pack(tmp, to_send, to_send_len, &packed, &packed_len);
    message_free(tmp);
    free(encrypted);

    if (rc != 0) return -1;

    /* Write to socket */
    ssize_t written = write(client_fd, packed, packed_len);
    free(packed);

    if (written < 0) return -1;
    /* Return payload bytes (not wire bytes) */
    return (ssize_t)len;
}

int server_send_json(Server *s, int client_fd, const JsonValue *obj)
{
    if (!s || !obj) return -1;

    char *json_str = json_stringify(obj);
    if (!json_str) return -1;

    ssize_t n = server_send(s, client_fd, json_str, strlen(json_str));
    free(json_str);

    if (n < 0) return -1;
    return 0;
}

JsonValue *server_recv_json(Server *s, int client_fd)
{
    if (!s) return NULL;

    /* 64 KB stack buffer for receiving JSON strings */
    char buf[65536];
    ssize_t n = server_recv(s, client_fd, buf, sizeof(buf) - 1);
    if (n <= 0) return NULL;

    buf[n] = '\0';
    return json_parse(buf);
}

int server_close_client(Server *s, int client_fd)
{
    if (!s) return -1;
    server_remove_slot(s, client_fd);
    return 0;
}

void server_free(Server *s)
{
    if (!s) return;

    /* Close all clients */
    for (size_t i = 0; i < s->num_clients; i++) {
        if (s->clients[i].fd >= 0) {
            close(s->clients[i].fd);
            message_free(s->clients[i].msg);
        }
    }
    free(s->clients);

    /* Close listen socket */
    if (s->listen_fd >= 0) close(s->listen_fd);

    /* Free security */
    security_free(s->sec);

    memset(s, 0, sizeof(*s));
    free(s);
}
