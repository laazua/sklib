#ifndef MESSAGE_H
#define MESSAGE_H

#include <stddef.h>
#include <stdint.h>

/**
 * Message module — length-prefix framing protocol.
 *
 * Wire format: 4-byte big-endian payload length, followed by payload bytes.
 * Handles TCP packet sticking/splitting by buffering partial data internally
 * and only returning complete messages.
 */
typedef struct Message Message;

/**
 * Create a new Message instance with an empty internal buffer.
 * Returns NULL on allocation failure.
 */
Message *message_new(void);

/**
 * Free the Message instance and all internal buffers.
 */
void message_free(Message *m);

/**
 * Pack raw data into a framed message.
 *
 * Allocates a new buffer containing: [4-byte length (BE) | payload].
 * Caller must free *out on success.
 *
 * Returns 0 on success, -1 on error.
 */
int message_pack(Message *m, const uint8_t *data, size_t len,
                 uint8_t **out, size_t *out_len);

/**
 * Feed raw bytes received from the socket into the internal buffer.
 * If a complete message can be extracted, *out is set to a heap-allocated
 * buffer containing the payload (without the length prefix) and *out_len
 * is set to the payload length.
 *
 * Returns  1 if a complete message was extracted (caller must free *out),
 *          0 if more data is needed to form a complete message,
 *         -1 on error.
 */
int message_unpack(Message *m, const uint8_t *raw, size_t raw_len,
                   uint8_t **out, size_t *out_len);

/**
 * Discard any buffered partial data (useful after a connection drop).
 */
void message_reset(Message *m);

#endif /* MESSAGE_H */
