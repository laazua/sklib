#include "json.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

/* ==================================================================
 *  Parser
 * ================================================================== */

typedef struct {
    const char *s;
    size_t      pos;
} Parser;

static void skip_ws(Parser *p)
{
    while (p->s[p->pos] && isspace((unsigned char)p->s[p->pos]))
        p->pos++;
}

static JsonValue *parse_value(Parser *p);

/* Read a JSON string (expects opening '"' already consumed). */
static char *parse_string_raw(Parser *p)
{
    /* First pass: measure length */
    size_t start = p->pos, len = 0;
    while (p->s[p->pos]) {
        char c = p->s[p->pos];
        if (c == '"') break;
        if (c == '\\') p->pos++; /* skip escaped char */
        p->pos++;
        len++;
    }
    if (p->s[p->pos] != '"') return NULL; /* unterminated */

    /* Second pass: copy */
    p->pos = start;
    char *str = malloc(len + 1);
    if (!str) return NULL;
    size_t wi = 0;
    while (p->s[p->pos]) {
        char c = p->s[p->pos];
        if (c == '"') { p->pos++; break; }
        if (c == '\\') {
            p->pos++;
            switch (p->s[p->pos]) {
                case '"':  str[wi++] = '"';  break;
                case '\\': str[wi++] = '\\'; break;
                case '/':  str[wi++] = '/';  break;
                case 'b':  str[wi++] = '\b'; break;
                case 'f':  str[wi++] = '\f'; break;
                case 'n':  str[wi++] = '\n'; break;
                case 'r':  str[wi++] = '\r'; break;
                case 't':  str[wi++] = '\t'; break;
                case 'u': {
                    /* \uXXXX — parse hex, encode as UTF-8 */
                    char hex[5] = {0};
                    for (int i = 0; i < 4; i++) hex[i] = p->s[++p->pos];
                    unsigned cp = (unsigned)strtol(hex, NULL, 16);
                    if (cp < 0x80) {
                        str[wi++] = (char)cp;
                    } else if (cp < 0x800) {
                        str[wi++] = (char)(0xC0 | (cp >> 6));
                        str[wi++] = (char)(0x80 | (cp & 0x3F));
                    } else {
                        str[wi++] = (char)(0xE0 | (cp >> 12));
                        str[wi++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        str[wi++] = (char)(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: break;
            }
            p->pos++;
        } else {
            str[wi++] = c;
            p->pos++;
        }
    }
    str[wi] = '\0';
    return str;
}

static JsonValue *parse_object(Parser *p)
{
    JsonValue *obj = json_new_object();
    if (!obj) return NULL;

    skip_ws(p);
    if (p->s[p->pos] == '}') { p->pos++; return obj; } /* empty object */

    for (;;) {
        skip_ws(p);
        if (p->s[p->pos] != '"') { json_free(obj); return NULL; }
        p->pos++; /* skip '"' */
        char *key = parse_string_raw(p);
        if (!key) { json_free(obj); return NULL; }

        skip_ws(p);
        if (p->s[p->pos] != ':') { free(key); json_free(obj); return NULL; }
        p->pos++;

        skip_ws(p);
        JsonValue *val = parse_value(p);
        if (!val) { free(key); json_free(obj); return NULL; }

        if (json_object_set(obj, key, val) != 0) {
            free(key); json_free(val); json_free(obj); return NULL;
        }
        free(key);

        skip_ws(p);
        if (p->s[p->pos] == '}') { p->pos++; return obj; }
        if (p->s[p->pos] != ',') { json_free(obj); return NULL; }
        p->pos++;
    }
}

static JsonValue *parse_array(Parser *p)
{
    JsonValue *arr = json_new_array();
    if (!arr) return NULL;

    skip_ws(p);
    if (p->s[p->pos] == ']') { p->pos++; return arr; } /* empty array */

    for (;;) {
        skip_ws(p);
        JsonValue *item = parse_value(p);
        if (!item) { json_free(arr); return NULL; }

        if (json_array_push(arr, item) < 0) {
            json_free(item); json_free(arr); return NULL;
        }

        skip_ws(p);
        if (p->s[p->pos] == ']') { p->pos++; return arr; }
        if (p->s[p->pos] != ',') { json_free(arr); return NULL; }
        p->pos++;
    }
}

static JsonValue *parse_number(Parser *p)
{
    size_t start = p->pos;

    /* sign */
    if (p->s[p->pos] == '-') p->pos++;

    /* integer part */
    if (p->s[p->pos] == '0') {
        p->pos++;
    } else {
        if (!isdigit((unsigned char)p->s[p->pos])) return NULL;
        while (isdigit((unsigned char)p->s[p->pos])) p->pos++;
    }

    /* fraction */
    if (p->s[p->pos] == '.') {
        p->pos++;
        if (!isdigit((unsigned char)p->s[p->pos])) return NULL;
        while (isdigit((unsigned char)p->s[p->pos])) p->pos++;
    }

    /* exponent */
    if (p->s[p->pos] == 'e' || p->s[p->pos] == 'E') {
        p->pos++;
        if (p->s[p->pos] == '+' || p->s[p->pos] == '-') p->pos++;
        if (!isdigit((unsigned char)p->s[p->pos])) return NULL;
        while (isdigit((unsigned char)p->s[p->pos])) p->pos++;
    }

    /* Copy and convert */
    size_t len = p->pos - start;
    char *buf = malloc(len + 1);
    if (!buf) return NULL;
    memcpy(buf, p->s + start, len);
    buf[len] = '\0';
    double val = strtod(buf, NULL);
    free(buf);

    return json_new_number(val);
}

static JsonValue *parse_value(Parser *p)
{
    skip_ws(p);
    if (!p->s[p->pos]) return NULL;

    char c = p->s[p->pos];

    switch (c) {
    case '{':
        p->pos++;
        return parse_object(p);
    case '[':
        p->pos++;
        return parse_array(p);
    case '"': {
        p->pos++;
        char *s = parse_string_raw(p);
        if (!s) return NULL;
        JsonValue *v = json_new_string(s);
        free(s);
        return v;
    }
    case 'n':
        if (strncmp(p->s + p->pos, "null", 4) == 0) {
            p->pos += 4;
            return json_new_null();
        }
        return NULL;
    case 't':
        if (strncmp(p->s + p->pos, "true", 4) == 0) {
            p->pos += 4;
            return json_new_bool(1);
        }
        return NULL;
    case 'f':
        if (strncmp(p->s + p->pos, "false", 5) == 0) {
            p->pos += 5;
            return json_new_bool(0);
        }
        return NULL;
    default:
        if (c == '-' || isdigit((unsigned char)c))
            return parse_number(p);
        return NULL;
    }
}

JsonValue *json_parse(const char *str)
{
    if (!str) return NULL;
    Parser p = { .s = str, .pos = 0 };
    JsonValue *v = parse_value(&p);
    if (!v) return NULL;

    /* Check that no trailing garbage remains */
    skip_ws(&p);
    if (p.s[p.pos] != '\0') {
        json_free(v);
        return NULL;
    }
    return v;
}

/* ==================================================================
 *  Stringify
 * ================================================================== */

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} StrBuf;

static int sb_init(StrBuf *sb)
{
    sb->cap = 256;
    sb->len = 0;
    sb->buf = malloc(sb->cap);
    return sb->buf ? 0 : -1;
}

static int sb_append(StrBuf *sb, const char *s, size_t len)
{
    size_t need = sb->len + len + 1;
    if (need > sb->cap) {
        size_t nc = sb->cap * 2;
        while (nc < need) nc *= 2;
        char *p = realloc(sb->buf, nc);
        if (!p) return -1;
        sb->buf = p;
        sb->cap = nc;
    }
    memcpy(sb->buf + sb->len, s, len);
    sb->len += len;
    sb->buf[sb->len] = '\0';
    return 0;
}

static int sb_putc(StrBuf *sb, char c)
{
    return sb_append(sb, &c, 1);
}

static int stringify_value(StrBuf *sb, const JsonValue *v);

static int stringify_string(StrBuf *sb, const char *s)
{
    if (sb_putc(sb, '"') != 0) return -1;
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
            case '"':  if (sb_append(sb, "\\\"", 2) != 0) return -1; break;
            case '\\': if (sb_append(sb, "\\\\", 2) != 0) return -1; break;
            case '\b': if (sb_append(sb, "\\b",  2) != 0) return -1; break;
            case '\f': if (sb_append(sb, "\\f",  2) != 0) return -1; break;
            case '\n': if (sb_append(sb, "\\n",  2) != 0) return -1; break;
            case '\r': if (sb_append(sb, "\\r",  2) != 0) return -1; break;
            case '\t': if (sb_append(sb, "\\t",  2) != 0) return -1; break;
            default:
                if (c < 0x20) {
                    char esc[8];
                    snprintf(esc, sizeof(esc), "\\u%04x", c);
                    if (sb_append(sb, esc, 6) != 0) return -1;
                } else {
                    if (sb_putc(sb, (char)c) != 0) return -1;
                }
                break;
        }
    }
    return sb_putc(sb, '"');
}

static int stringify_value(StrBuf *sb, const JsonValue *v)
{
    if (!v) return sb_append(sb, "null", 4);

    switch (v->type) {
    case JSON_NULL:
        return sb_append(sb, "null", 4);

    case JSON_BOOL:
        return sb_append(sb, v->data.bool_val ? "true" : "false",
                         v->data.bool_val ? 4 : 5);

    case JSON_NUMBER: {
        char num[64];
        /* Use %g for compact representation */
        snprintf(num, sizeof(num), "%.17g", v->data.num_val);
        /* Ensure it has a decimal point if it looks like an integer */
        if (!strchr(num, '.') && !strchr(num, 'e') && !strchr(num, 'E')
            && !isnan(v->data.num_val) && !isinf(v->data.num_val)) {
            size_t l = strlen(num);
            num[l] = '.'; num[l + 1] = '0'; num[l + 2] = '\0';
        }
        return sb_append(sb, num, strlen(num));
    }

    case JSON_STRING:
        return stringify_string(sb, v->data.str_val);

    case JSON_ARRAY:
        if (sb_putc(sb, '[') != 0) return -1;
        for (size_t i = 0; i < v->data.arr.count; i++) {
            if (i > 0 && sb_putc(sb, ',') != 0) return -1;
            if (stringify_value(sb, v->data.arr.items[i]) != 0) return -1;
        }
        return sb_putc(sb, ']');

    case JSON_OBJECT:
        if (sb_putc(sb, '{') != 0) return -1;
        for (size_t i = 0; i < v->data.obj.count; i++) {
            if (i > 0 && sb_putc(sb, ',') != 0) return -1;
            if (stringify_string(sb, v->data.obj.keys[i]) != 0) return -1;
            if (sb_putc(sb, ':') != 0) return -1;
            if (stringify_value(sb, v->data.obj.vals[i]) != 0) return -1;
        }
        return sb_putc(sb, '}');

    default:
        return -1;
    }
}

char *json_stringify(const JsonValue *v)
{
    StrBuf sb;
    if (sb_init(&sb) != 0) return NULL;
    if (stringify_value(&sb, v) != 0) {
        free(sb.buf);
        return NULL;
    }
    return sb.buf; /* caller must free */
}

/* ==================================================================
 *  Constructors / Lifecycle
 * ================================================================== */

static JsonValue *json_alloc(JsonType t)
{
    JsonValue *v = calloc(1, sizeof(*v));
    if (v) v->type = t;
    return v;
}

JsonValue *json_new_null(void)   { return json_alloc(JSON_NULL); }
JsonValue *json_new_bool(int b)  { JsonValue *v = json_alloc(JSON_BOOL); if (v) v->data.bool_val = !!b; return v; }
JsonValue *json_new_number(double n) { JsonValue *v = json_alloc(JSON_NUMBER); if (v) v->data.num_val = n; return v; }

JsonValue *json_new_string(const char *s)
{
    JsonValue *v = json_alloc(JSON_STRING);
    if (!v) return NULL;
    if (s) {
        v->data.str_val = malloc(strlen(s) + 1);
        if (!v->data.str_val) { free(v); return NULL; }
        strcpy(v->data.str_val, s);
    } else {
        v->data.str_val = malloc(1);
        if (!v->data.str_val) { free(v); return NULL; }
        v->data.str_val[0] = '\0';
    }
    return v;
}

JsonValue *json_new_array(void)  { return json_alloc(JSON_ARRAY); }
JsonValue *json_new_object(void) { return json_alloc(JSON_OBJECT); }

void json_free(JsonValue *v)
{
    if (!v) return;
    switch (v->type) {
    case JSON_STRING:
        free(v->data.str_val);
        break;
    case JSON_ARRAY:
        for (size_t i = 0; i < v->data.arr.count; i++)
            json_free(v->data.arr.items[i]);
        free(v->data.arr.items);
        break;
    case JSON_OBJECT:
        for (size_t i = 0; i < v->data.obj.count; i++) {
            free(v->data.obj.keys[i]);
            json_free(v->data.obj.vals[i]);
        }
        free(v->data.obj.keys);
        free(v->data.obj.vals);
        break;
    default:
        break;
    }
    memset(v, 0, sizeof(*v));
    free(v);
}

/* ==================================================================
 *  Array operations
 * ================================================================== */

int json_array_push(JsonValue *arr, JsonValue *item)
{
    if (!arr || arr->type != JSON_ARRAY) return -1;

    size_t nc = arr->data.arr.count + 1;
    JsonValue **p = realloc(arr->data.arr.items, nc * sizeof(JsonValue *));
    if (!p) return -1;

    arr->data.arr.items = p;
    arr->data.arr.items[arr->data.arr.count] = item;
    arr->data.arr.count = nc;
    return (int)nc;
}

JsonValue *json_array_get(const JsonValue *arr, size_t idx)
{
    if (!arr || arr->type != JSON_ARRAY) return NULL;
    if (idx >= arr->data.arr.count) return NULL;
    return arr->data.arr.items[idx];
}

size_t json_array_size(const JsonValue *arr)
{
    if (!arr || arr->type != JSON_ARRAY) return 0;
    return arr->data.arr.count;
}

/* ==================================================================
 *  Object operations
 * ================================================================== */

int json_object_set(JsonValue *obj, const char *key, JsonValue *val)
{
    if (!obj || obj->type != JSON_OBJECT || !key) return -1;

    /* Check if key already exists → replace */
    for (size_t i = 0; i < obj->data.obj.count; i++) {
        if (strcmp(obj->data.obj.keys[i], key) == 0) {
            json_free(obj->data.obj.vals[i]);
            obj->data.obj.vals[i] = val;
            return 0;
        }
    }

    /* Grow arrays */
    size_t nc = obj->data.obj.count + 1;
    char **k = realloc(obj->data.obj.keys, nc * sizeof(char *));
    JsonValue **v = realloc(obj->data.obj.vals, nc * sizeof(JsonValue *));
    if (!k || !v) { free(k); free(v); return -1; }

    k[nc - 1] = malloc(strlen(key) + 1);
    if (!k[nc - 1]) { free(k); free(v); return -1; }
    strcpy(k[nc - 1], key);

    obj->data.obj.keys = k;
    obj->data.obj.vals = v;
    obj->data.obj.vals[nc - 1] = val;
    obj->data.obj.count = nc;
    return 0;
}

JsonValue *json_object_get(const JsonValue *obj, const char *key)
{
    if (!obj || obj->type != JSON_OBJECT || !key) return NULL;
    for (size_t i = 0; i < obj->data.obj.count; i++) {
        if (strcmp(obj->data.obj.keys[i], key) == 0)
            return obj->data.obj.vals[i];
    }
    return NULL;
}

size_t json_object_size(const JsonValue *obj)
{
    if (!obj || obj->type != JSON_OBJECT) return 0;
    return obj->data.obj.count;
}

/* ==================================================================
 *  Convenience accessors
 * ================================================================== */

const char *json_as_string(const JsonValue *v)
{
    if (!v || v->type != JSON_STRING) return NULL;
    return v->data.str_val;
}

double json_as_number(const JsonValue *v)
{
    if (!v || v->type != JSON_NUMBER) return 0.0;
    return v->data.num_val;
}

int json_as_bool(const JsonValue *v)
{
    if (!v || v->type != JSON_BOOL) return 0;
    return v->data.bool_val;
}
