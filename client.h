#ifndef CLIENT_H
#define CLIENT_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/**
 * Client module — TCP client with send/recv.
 *
 * Internally composes Message (framing) and Security (encryption) modules.
 * client_send: encrypts → frames → writes to socket.
 * client_recv: reads from socket → unframes → decrypts → returns payload.
 */
typedef struct Client Client;
typedef struct JsonValue JsonValue;

/**
 * Create a client instance. Does NOT connect — call client_connect().
 *
 * @param server_ip  destination IP address (e.g. "127.0.0.1")
 * @param port       destination TCP port
 * @param enc_key    encryption key string (may be NULL to disable encryption)
 * @return Client instance, or NULL on error.
 */
Client *client_new(const char *server_ip, int port, const char *enc_key);

/**
 * Connect to the server specified in client_new().
 * Returns 0 on success, -1 on error.
 */
int client_connect(Client *c);

/**
 * Send data to the server. Encrypts and frames automatically.
 *
 * Returns the number of payload bytes sent on success, -1 on error.
 */
ssize_t client_send(Client *c, const void *buf, size_t len);

/**
 * Receive a decrypted, unframed message from the server.
 *
 * Blocks until a complete message is available.
 * @param buf  caller-provided buffer
 * @param len  capacity of @buf
 * Returns the number of bytes written to @buf on success,
 *         0 if the server closed the connection,
 *        -1 on error.
 */
ssize_t client_recv(Client *c, void *buf, size_t len);

/**
 * Send a JSON value to the server. Serializes to string, then encrypts,
 * frames, and writes.
 *
 * Returns 0 on success, -1 on error.
 */
int client_send_json(Client *c, const JsonValue *obj);

/**
 * Receive and parse a JSON value from the server.
 *
 * Blocks until a complete message is available, then parses it as JSON.
 * Returns a heap-allocated JsonValue (caller must json_free), or NULL on error.
 */
JsonValue *client_recv_json(Client *c);

/**
 * Disconnect from the server and free all resources.
 */
void client_free(Client *c);

#endif /* CLIENT_H */
