#include "security.h"

#include <stdlib.h>
#include <string.h>

/*
 * Simple XOR stream cipher using a key-seeded LCG-based PRNG.
 *
 * This is NOT cryptographically secure. It demonstrates the pipeline
 * architecture so that a real cipher (e.g. AES-GCM via OpenSSL) can be
 * dropped in without changing the API.
 *
 * Key stream generation:
 *   1. Hash the key string into a 64-bit seed (djb2 variant).
 *   2. Use the seed as the initial state of an LCG.
 *   3. For each byte, advance LCG and use the middle byte as the key byte.
 */

#define LCG_MULTIPLIER 6364136223846793005ULL
#define LCG_INCREMENT  1442695040888963407ULL

struct Security {
    uint64_t state;     /* LCG state (also stores the original seed for reset) */
    uint64_t seed;      /* original seed for re-keying */
    uint64_t position;  /* bytes processed (allows seek in keystream) */
};

/* djb2-style hash with XOR folding to produce a 64-bit seed */
static uint64_t key_hash(const char *key)
{
    uint64_t h = 5381;
    while (*key) {
        h = ((h << 5) + h) ^ (unsigned char)*key; /* h * 33 ^ c */
        key++;
    }
    /* mix */
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return h;
}

Security *security_new(const char *key)
{
    if (!key || !*key) return NULL;

    Security *s = calloc(1, sizeof(*s));
    if (!s) return NULL;

    s->seed     = key_hash(key);
    s->state    = s->seed;
    s->position = 0;
    return s;
}

void security_free(Security *s)
{
    if (!s) return;
    /* zero out sensitive material */
    memset(s, 0, sizeof(*s));
    free(s);
}

/* Advance LCG and return the next keystream byte */
static uint8_t next_key_byte(Security *s)
{
    s->state = s->state * LCG_MULTIPLIER + LCG_INCREMENT;
    s->position++;
    /* extract bits 16–23 (reasonable mixing) */
    return (uint8_t)((s->state >> 16) & 0xFF);
}

/* Reset keystream to the beginning (state = seed, position = 0) */
static void keystream_reset(Security *s)
{
    s->state    = s->seed;
    s->position = 0;
}

static int cipher_apply(Security *s, const uint8_t *in, size_t in_len,
                        uint8_t **out, size_t *out_len)
{
    if (!s || !in || !out || !out_len) return -1;

    uint8_t *result = malloc(in_len ? in_len : 1);
    if (!result) return -1;

    keystream_reset(s); /* each message is independently encrypted from seed */
    for (size_t i = 0; i < in_len; i++) {
        result[i] = in[i] ^ next_key_byte(s);
    }

    *out     = result;
    *out_len = in_len;
    return 0;
}

int security_encrypt(Security *s, const uint8_t *plain, size_t plain_len,
                     uint8_t **cipher, size_t *cipher_len)
{
    return cipher_apply(s, plain, plain_len, cipher, cipher_len);
}

int security_decrypt(Security *s, const uint8_t *cipher, size_t cipher_len,
                     uint8_t **plain, size_t *plain_len)
{
    /* XOR is symmetric — encrypt and decrypt are the same operation */
    return cipher_apply(s, cipher, cipher_len, plain, plain_len);
}
