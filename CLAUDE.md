# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Development

- **Compiler**: GCC 14.3.1 (Red Hat 14.3.1-4), C11 or later
- **Dependencies**: None — standard C library only
- **Build static lib**: `gcc -std=c11 -c message.c security.c json.c server.c client.c && ar rcs libsklib.a *.o`
- **Build shared lib**: `gcc -std=c11 -fPIC -c message.c security.c json.c server.c client.c && gcc -shared -o libsklib.so *.o`
- **Build with test/demo**: `gcc -std=c11 -o demo demo.c -L. -lsklib -lpthread`

## Architecture

Four-module client-server communication library, organized with OOP-in-C patterns.

| Module      | Files                | Responsibility                     |
|-------------|----------------------|------------------------------------|
| Server      | `server.c/server.h`  | Server lifecycle, client management |
| Client      | `client.c/client.h`  | Client lifecycle, connection       |
| JSON        | `json.c/json.h`      | JSON parse, stringify, value tree  |
| Message     | `message.c/message.h`| Message framing, packet boundary handling (粘包) |
| Security    | `security.c/security.h`| Stream-cipher encryption (XOR+LCG) |

### OOP-in-C Conventions

- Each module uses an opaque `struct` typedef'd with a descriptive name (e.g., `typedef struct Server Server`).
- Constructor-style functions return heap-allocated instances: `Server *server_new(...)`.
- Destructor-style functions free instances: `void server_free(Server *s)`.
- All "methods" take the instance pointer as their first argument.
- Internal/private details stay in the `.c` file; the `.h` exposes only the public API and an incomplete type declaration.

### Public API

**Server:**
- `Server *server_new(int port, const char *enc_key)` — create, bind, listen
- `int server_accept(Server *s)` — block until a client connects, return fd
- `ssize_t server_recv(Server *s, int fd, void *buf, size_t len)` — recv raw bytes
- `ssize_t server_send(Server *s, int fd, const void *buf, size_t len)` — send raw bytes
- `int server_recv_json(Server *s, int fd)` → `JsonValue *` — recv and parse JSON (caller must json_free)
- `int server_send_json(Server *s, int fd, const JsonValue *obj)` — stringify and send JSON
- `int server_close_client(Server *s, int fd)` — close one client
- `void server_free(Server *s)` — shutdown and free all

**Client:**
- `Client *client_new(const char *ip, int port, const char *enc_key)` — create
- `int client_connect(Client *c)` — connect to server
- `ssize_t client_send(Client *c, const void *buf, size_t len)` — send raw bytes
- `ssize_t client_recv(Client *c, void *buf, size_t len)` — recv raw bytes
- `int client_send_json(Client *c, const JsonValue *obj)` — stringify and send JSON
- `JsonValue *client_recv_json(Client *c)` — recv and parse JSON (caller must json_free)
- `void client_free(Client *c)` — disconnect and free

**JSON** — `json_object_set` and `json_array_push` take ownership of the value. `json_object_get` and `json_array_get` return borrowed pointers — clone before passing to a setter if the source tree is still alive.

## Constraints

- **Standard C only** — no POSIX-specific extensions unless unavoidable for sockets. Use `#ifdef` guards if platform-specific code is needed.
- Libraries are distributed as both `.a` (static) and `.so` (shared).
