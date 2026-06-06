#ifndef SERVER_H
#define SERVER_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/**
 * Server module — TCP server with per-client send/recv.
 *
 * Internally composes Message (framing) and Security (encryption) modules.
 * server_send: encrypts → frames → writes to socket.
 * server_recv: reads from socket → unframes → decrypts → returns payload.
 */
typedef struct Server Server;
typedef struct JsonValue JsonValue;

/**
 * Create a server bound to @port, listening with a backlog of SOMAXCONN.
 * The socket is created but the event loop is NOT started — call
 * server_accept() to block for connections.
 *
 * @param port     TCP port number (1–65535)
 * @param enc_key  encryption key string for the Security module (may be NULL
 *                 to disable encryption; if NULL, security layer is skipped)
 * @return Server instance, or NULL on error.
 */
Server *server_new(int port, const char *enc_key);

/**
 * Block until a new client connects. Returns the client file descriptor
 * on success, or -1 on error (errno set).
 */
int server_accept(Server *s);

/**
 * Receive a decrypted, unframed message from a specific client.
 *
 * Blocks until a complete message is available on @client_fd.
 * @param buf  caller-provided buffer
 * @param len  capacity of @buf
 * Returns the number of bytes written to @buf on success,
 *         0 if the client closed the connection,
 *        -1 on error.
 */
ssize_t server_recv(Server *s, int client_fd, void *buf, size_t len);

/**
 * Send data to a specific client. The data is encrypted, framed, and
 * written to @client_fd.
 *
 * Returns the number of payload bytes sent on success, -1 on error.
 */
ssize_t server_send(Server *s, int client_fd, const void *buf, size_t len);

/**
 * Send a JSON value to a client. Serializes to string, then encrypts,
 * frames, and writes.
 *
 * Returns 0 on success, -1 on error.
 */
int server_send_json(Server *s, int client_fd, const JsonValue *obj);

/**
 * Receive and parse a JSON value from a client.
 *
 * Blocks until a complete message is available, then parses it as JSON.
 * Returns a heap-allocated JsonValue (caller must json_free), or NULL on error.
 */
JsonValue *server_recv_json(Server *s, int client_fd);

/**
 * Close a client connection and release its per-client buffers.
 * Returns 0 on success, -1 on error.
 */
int server_close_client(Server *s, int client_fd);

/**
 * Shut down the server, close all client connections, and free all resources.
 */
void server_free(Server *s);

#endif /* SERVER_H */
