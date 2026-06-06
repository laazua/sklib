# sklib

轻量级 C 语言客户端-服务端通信库，支持原始字节和 JSON 两种通信模式，内置消息粘包处理和加密。

## 构建

```bash
# 静态库
gcc -std=c11 -c message.c security.c json.c server.c client.c
ar rcs libsklib.a *.o

# 动态库
gcc -std=c11 -fPIC -c message.c security.c json.c server.c client.c
gcc -shared -o libsklib.so *.o

# 链接你的程序
gcc -std=c11 -o myapp myapp.c -L. -lsklib -lpthread
```

## 模块架构

```
┌──────────────────────────────────────────┐
│            你的应用程序                    │
├──────────────────────────────────────────┤
│  server.h / client.h    (服务端/客户端)    │
├──────────────────────────────────────────┤
│  json.h                 (JSON 序列化)     │
├──────────────────────────────────────────┤
│  message.h              (消息粘包处理)     │
├──────────────────────────────────────────┤
│  security.h             (加密)            │
└──────────────────────────────────────────┘
```

**数据流（发送）**: 用户数据 → encrypt → length-prefix frame → socket
**数据流（接收）**: socket → buffer → extract frame → decrypt → 用户数据

---

## 示例一：原始字节通信

### 服务端

```c
#include <stdio.h>
#include <string.h>
#include "server.h"

int main(void)
{
    /* 创建服务端，监听 8080 端口，使用密钥 "my-secret" */
    Server *s = server_new(8080, "my-secret");
    if (!s) { perror("server_new"); return 1; }

    printf("Listening on port 8080...\n");

    /* 接受一个客户端连接 */
    int fd = server_accept(s);
    if (fd < 0) { perror("accept"); server_free(s); return 1; }
    printf("Client connected.\n");

    /* 接收消息 */
    char buf[4096];
    ssize_t n = server_recv(s, fd, buf, sizeof(buf));
    if (n > 0) {
        printf("Received: %.*s\n", (int)n, buf);

        /* 回复消息 */
        const char *reply = "Hello from server!";
        server_send(s, fd, reply, strlen(reply));
    }

    server_close_client(s, fd);
    server_free(s);
    return 0;
}
```

### 客户端

```c
#include <stdio.h>
#include <string.h>
#include "client.h"

int main(void)
{
    /* 创建客户端，连接 127.0.0.1:8080 */
    Client *c = client_new("127.0.0.1", 8080, "my-secret");
    if (!c) { perror("client_new"); return 1; }

    if (client_connect(c) != 0) { perror("connect"); client_free(c); return 1; }
    printf("Connected.\n");

    /* 发送消息 */
    const char *msg = "Hello from client!";
    client_send(c, msg, strlen(msg));

    /* 接收回复 */
    char buf[4096];
    ssize_t n = client_recv(c, buf, sizeof(buf));
    if (n > 0) {
        printf("Received: %.*s\n", (int)n, buf);
    }

    client_free(c);
    return 0;
}
```

---

## 示例二：JSON 通信（请求-响应模式）

### 服务端

```c
#include <stdio.h>
#include <string.h>
#include "server.h"
#include "json.h"

int main(void)
{
    Server *s = server_new(8080, "my-secret");
    if (!s) return 1;

    printf("Listening on port 8080...\n");
    int fd = server_accept(s);
    if (fd < 0) { server_free(s); return 1; }

    /* 接收 JSON 请求 */
    JsonValue *req = server_recv_json(s, fd);
    if (!req) { server_free(s); return 1; }

    const char *method = json_as_string(json_object_get(req, "method"));
    printf("Method: %s\n", method);

    /* 构建 JSON 响应 */
    JsonValue *res = json_new_object();
    json_object_set(res, "code",    json_new_number(0));
    json_object_set(res, "message", json_new_string("success"));

    /* 返回数组数据 */
    JsonValue *data = json_new_array();
    json_array_push(data, json_new_number(1.0));
    json_array_push(data, json_new_number(2.0));
    json_array_push(data, json_new_number(3.0));
    json_object_set(res, "data", data);

    server_send_json(s, fd, res);

    json_free(req);
    json_free(res);
    server_close_client(s, fd);
    server_free(s);
    return 0;
}
```

### 客户端

```c
#include <stdio.h>
#include "client.h"
#include "json.h"

int main(void)
{
    Client *c = client_new("127.0.0.1", 8080, "my-secret");
    if (!c) return 1;
    if (client_connect(c) != 0) { client_free(c); return 1; }

    /* 构建 JSON 请求 */
    JsonValue *req = json_new_object();
    json_object_set(req, "method", json_new_string("get_data"));
    json_object_set(req, "id",     json_new_number(1.0));

    client_send_json(c, req);
    json_free(req);

    /* 接收 JSON 响应 */
    JsonValue *res = client_recv_json(c);
    if (res) {
        double   code    = json_as_number(json_object_get(res, "code"));
        const char *msg  = json_as_string(json_object_get(res, "message"));
        JsonValue *data  = json_object_get(res, "data");

        printf("Response: code=%.0f, msg=%s\n", code, msg);
        for (size_t i = 0; i < json_array_size(data); i++) {
            printf("  data[%zu] = %.0f\n", i,
                   json_as_number(json_array_get(data, i)));
        }

        json_free(res);
    }

    client_free(c);
    return 0;
}
```

---

## 示例三：服务端处理多个客户端

```c
#include <stdio.h>
#include <string.h>
#include "server.h"
#include "json.h"

int main(void)
{
    Server *s = server_new(8080, NULL);  /* 不加密 */
    if (!s) return 1;

    for (;;) {
        int fd = server_accept(s);
        if (fd < 0) break;

        printf("Client %d connected.\n", fd);

        /* 接收一个 JSON 对象 */
        JsonValue *req = server_recv_json(s, fd);
        if (!req) { server_close_client(s, fd); continue; }

        /* 简单回显 */
        JsonValue *res = json_new_object();
        json_object_set(res, "echo", req);  /* req 所有权转移给 res */

        server_send_json(s, fd, res);
        json_free(res);
        server_close_client(s, fd);
    }

    server_free(s);
    return 0;
}
```

---

## API 速查

### Server

| 函数 | 说明 |
|------|------|
| `Server *server_new(int port, const char *enc_key)` | 创建服务端（enc_key 为 NULL 则不加密） |
| `int server_accept(Server *s)` | 阻塞等待客户端连接，返回 fd |
| `ssize_t server_recv(Server *s, int fd, void *buf, size_t len)` | 接收原始数据 |
| `ssize_t server_send(Server *s, int fd, const void *buf, size_t len)` | 发送原始数据 |
| `int server_recv_json(Server *s, int fd)` → `JsonValue *` | 接收并解析 JSON |
| `int server_send_json(Server *s, int fd, const JsonValue *obj)` | 序列化并发送 JSON |
| `int server_close_client(Server *s, int fd)` | 关闭客户端连接 |
| `void server_free(Server *s)` | 释放所有资源 |

### Client

| 函数 | 说明 |
|------|------|
| `Client *client_new(const char *ip, int port, const char *enc_key)` | 创建客户端 |
| `int client_connect(Client *c)` | 连接到服务端 |
| `ssize_t client_send(Client *c, const void *buf, size_t len)` | 发送原始数据 |
| `ssize_t client_recv(Client *c, void *buf, size_t len)` | 接收原始数据 |
| `int client_send_json(Client *c, const JsonValue *obj)` | 序列化并发送 JSON |
| `JsonValue *client_recv_json(Client *c)` | 接收并解析 JSON |
| `void client_free(Client *c)` | 释放所有资源 |

### JSON 构建

| 函数 | 说明 |
|------|------|
| `json_new_object()` | 创建空对象 |
| `json_new_array()` | 创建空数组 |
| `json_new_string(s)` | 创建字符串值 |
| `json_new_number(n)` | 创建数值 |
| `json_new_bool(b)` | 创建布尔值 |
| `json_new_null()` | 创建 null |
| `json_object_set(obj, key, val)` | 设置字段（接管 val 所有权） |
| `json_object_get(obj, key)` | 获取字段（借用指针） |
| `json_array_push(arr, val)` | 追加数组元素（接管 val 所有权） |
| `json_array_get(arr, idx)` | 获取数组元素（借用指针） |
| `json_as_string(v)` / `json_as_number(v)` / `json_as_bool(v)` | 取值 |
| `json_stringify(v)` → `char *` | 序列化为 JSON 字符串（需 free） |
| `json_parse(str)` → `JsonValue *` | 解析 JSON 字符串 |
| `json_free(v)` | 释放 JSON 树 |

### 注意事项

- **所有权**: `json_object_set` 和 `json_array_push` 接管传入值的所有权，调用后不要再单独释放。如需共享，先克隆一份（如用 `json_new_string` 从已有字符串创建新值）。
- **加密密钥**: 服务端和客户端必须使用相同的密钥才能正常通信。传 `NULL` 可禁用加密。
- **消息缓冲**: 每个客户端连接有独立的缓冲区处理粘包，`recv` 会阻塞直到收到完整的一条消息。
- **内存**: 所有以指针形式返回的结果（`json_parse`, `json_stringify`, `*_recv_json`）调用方负责释放。
