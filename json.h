#ifndef JSON_H
#define JSON_H

#include <stddef.h>
#include <stdint.h>

/**
 * JSON module — lightweight JSON parser and builder.
 *
 * Supports: null, bool, number (double), string, array, object.
 * All standard C — no external dependencies.
 */
typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JsonType;

typedef struct JsonValue JsonValue;

struct JsonValue {
    JsonType type;
    union {
        int      bool_val;   /* JSON_BOOL */
        double   num_val;    /* JSON_NUMBER */
        char    *str_val;    /* JSON_STRING */
        struct {              /* JSON_ARRAY */
            JsonValue **items;
            size_t      count;
        } arr;
        struct {              /* JSON_OBJECT */
            char      **keys;
            JsonValue **vals;
            size_t      count;
        } obj;
    } data;
};

/* ---- Lifecycle ---- */

/** Parse a JSON string. Returns NULL on syntax error. */
JsonValue *json_parse(const char *str);

/** Convert a JsonValue tree to a JSON string. Caller must free(). */
char *json_stringify(const JsonValue *v);

/** Deep-free a JsonValue tree. */
void json_free(JsonValue *v);

/* ---- Constructors ---- */

JsonValue *json_new_null(void);
JsonValue *json_new_bool(int b);
JsonValue *json_new_number(double n);
JsonValue *json_new_string(const char *s);
JsonValue *json_new_array(void);
JsonValue *json_new_object(void);

/* ---- Array operations ---- */

/** Append an item (takes ownership). Returns new count, or -1 on error. */
int json_array_push(JsonValue *arr, JsonValue *item);

/** Get item at index (no bounds check). Returns NULL if not an array. */
JsonValue *json_array_get(const JsonValue *arr, size_t idx);

/** Return the number of elements. */
size_t json_array_size(const JsonValue *arr);

/* ---- Object operations ---- */

/**
 * Set a key-value pair (takes ownership of val).
 * If the key already exists, the old value is freed and replaced.
 * Returns 0 on success, -1 on error.
 */
int json_object_set(JsonValue *obj, const char *key, JsonValue *val);

/** Get value by key. Returns NULL if key not found or not an object. */
JsonValue *json_object_get(const JsonValue *obj, const char *key);

/** Return the number of key-value pairs. */
size_t json_object_size(const JsonValue *obj);

/* ---- Convenience accessors ---- */

/** Extract a string value. Returns NULL for non-string types. */
const char *json_as_string(const JsonValue *v);

/** Extract a number value. Returns 0.0 for non-number types. */
double json_as_number(const JsonValue *v);

/** Extract a bool value. Returns 0 for non-bool types. */
int json_as_bool(const JsonValue *v);

#endif /* JSON_H */
