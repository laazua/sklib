#include "message.h"

#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#define MESSAGE_HEADER_LEN 4   /* uint32_t length prefix in network byte order */

struct Message {
    uint8_t *buf;       /* internal buffer for partial data */
    size_t   buf_len;   /* bytes currently stored in buf */
    size_t   buf_cap;   /* allocated capacity of buf */
};

Message *message_new(void)
{
    Message *m = calloc(1, sizeof(*m));
    return m; /* calloc zeroes buf/buf_len/buf_cap */
}

void message_free(Message *m)
{
    if (!m) return;
    free(m->buf);
    /* zero out before freeing */
    memset(m, 0, sizeof(*m));
    free(m);
}

void message_reset(Message *m)
{
    if (!m) return;
    free(m->buf);
    m->buf     = NULL;
    m->buf_len = 0;
    m->buf_cap = 0;
}

int message_pack(Message *m, const uint8_t *data, size_t len,
                 uint8_t **out, size_t *out_len)
{
    (void)m; /* unused — pack is stateless */
    if (!data || !out || !out_len) return -1;

    size_t total = MESSAGE_HEADER_LEN + len;
    uint8_t *packed = malloc(total);
    if (!packed) return -1;

    uint32_t net_len = htonl((uint32_t)len);
    memcpy(packed, &net_len, MESSAGE_HEADER_LEN);
    if (len > 0) {
        memcpy(packed + MESSAGE_HEADER_LEN, data, len);
    }

    *out     = packed;
    *out_len = total;
    return 0;
}

/* Ensure internal buffer has room for @need more bytes. */
static int buffer_reserve(Message *m, size_t need)
{
    size_t want = m->buf_len + need;
    if (want <= m->buf_cap) return 0;

    size_t new_cap = m->buf_cap ? m->buf_cap * 2 : 1024;
    while (new_cap < want) new_cap *= 2;

    uint8_t *p = realloc(m->buf, new_cap);
    if (!p) return -1;

    m->buf     = p;
    m->buf_cap = new_cap;
    return 0;
}

int message_unpack(Message *m, const uint8_t *raw, size_t raw_len,
                   uint8_t **out, size_t *out_len)
{
    if (!m || !out || !out_len) return -1;
    if (raw_len == 0) return 0;

    /* Append new data to internal buffer */
    if (buffer_reserve(m, raw_len) != 0) return -1;
    memcpy(m->buf + m->buf_len, raw, raw_len);
    m->buf_len += raw_len;

    /* Need at least the 4-byte length header */
    if (m->buf_len < MESSAGE_HEADER_LEN) return 0;

    /* Read payload length */
    uint32_t payload_len;
    memcpy(&payload_len, m->buf, MESSAGE_HEADER_LEN);
    payload_len = ntohl(payload_len);

    /* Do we have the full payload yet? */
    if (m->buf_len < MESSAGE_HEADER_LEN + payload_len) return 0;

    /* Extract payload */
    uint8_t *payload = malloc(payload_len ? payload_len : 1);
    if (!payload) return -1;

    if (payload_len > 0) {
        memcpy(payload, m->buf + MESSAGE_HEADER_LEN, payload_len);
    }

    /* Remove consumed bytes from internal buffer */
    size_t consumed = MESSAGE_HEADER_LEN + payload_len;
    size_t remaining = m->buf_len - consumed;
    if (remaining > 0) {
        memmove(m->buf, m->buf + consumed, remaining);
    }
    m->buf_len = remaining;

    *out     = payload;
    *out_len = payload_len;
    return 1;
}
