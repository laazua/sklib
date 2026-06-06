/**
 * echo_server — 多客户端并发回显服务
 *
 * 使用 pthread 为每个客户端创建一个线程，在线程内循环接收 JSON
 * 消息并返回处理结果。演示了 sklib 在多客户端场景下的用法。
 *
 * 支持的 JSON 请求方法:
 *   {"method":"echo",   "message":"..."}     → 回显消息
 *   {"method":"add",    "a":N, "b":N}        → 返回 a+b
 *   {"method":"stats",  "values":[N,...]}    → 返回统计信息
 *   {"method":"quit"}                        → 服务端主动关闭该客户端
 *
 * 构建: make
 * 运行: ./echo_server [port]
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>

#include "server.h"
#include "json.h"

static volatile int g_running = 1;      /* 服务运行标志 */
static pthread_mutex_t g_print_mtx;     /* printf 线程安全锁 */

#define ECHO_SERVER_VERSION "1.0.0"

/* ---- 线程安全的日志 ---- */
static void log_msg(const char *pfx, int fd, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static void log_msg(const char *pfx, int fd, const char *fmt, ...)
{
    pthread_mutex_lock(&g_print_mtx);
    printf("[%s|fd=%d] ", pfx, fd);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
    pthread_mutex_unlock(&g_print_mtx);
}

/* ---- 信号处理 ---- */
static void sig_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

/* ---- 处理 echo 请求 ---- */
static JsonValue *handle_echo(JsonValue *req, int fd)
{
    (void)fd;
    const char *msg = json_as_string(json_object_get(req, "message"));
    JsonValue *res = json_new_object();
    json_object_set(res, "method", json_new_string("echo"));
    json_object_set(res, "echo",   json_new_string(msg ? msg : ""));
    return res;
}

/* ---- 处理 add 请求 ---- */
static JsonValue *handle_add(JsonValue *req, int fd)
{
    (void)fd;
    double a = json_as_number(json_object_get(req, "a"));
    double b = json_as_number(json_object_get(req, "b"));
    JsonValue *res = json_new_object();
    json_object_set(res, "method", json_new_string("add"));
    json_object_set(res, "a",      json_new_number(a));
    json_object_set(res, "b",      json_new_number(b));
    json_object_set(res, "result", json_new_number(a + b));
    return res;
}

/* ---- 处理 stats 请求 ---- */
static JsonValue *handle_stats(JsonValue *req, int fd)
{
    (void)fd;
    JsonValue *values = json_object_get(req, "values");
    size_t count = json_array_size(values);

    double sum = 0.0, min = 0.0, max = 0.0;
    if (count > 0) {
        min = max = json_as_number(json_array_get(values, 0));
        for (size_t i = 0; i < count; i++) {
            double v = json_as_number(json_array_get(values, i));
            sum += v;
            if (v < min) min = v;
            if (v > max) max = v;
        }
    }

    JsonValue *res = json_new_object();
    json_object_set(res, "method", json_new_string("stats"));
    json_object_set(res, "count",  json_new_number((double)count));
    json_object_set(res, "sum",    json_new_number(sum));
    json_object_set(res, "avg",    json_new_number(count > 0 ? sum / (double)count : 0.0));
    json_object_set(res, "min",    json_new_number(min));
    json_object_set(res, "max",    json_new_number(max));
    return res;
}

/* ---- 处理未知方法 ---- */
static JsonValue *handle_unknown(const char *method, int fd)
{
    (void)fd;
    JsonValue *res = json_new_object();
    json_object_set(res, "error",   json_new_string("unknown method"));
    json_object_set(res, "method",  json_new_string(method ? method : "null"));
    json_object_set(res, "message", json_new_string(
        "Supported: echo, add, stats, quit"));
    return res;
}

/* ---- 请求分发 ---- */
static JsonValue *dispatch(JsonValue *req, int fd)
{
    const char *method = json_as_string(json_object_get(req, "method"));
    if (!method) {
        return handle_unknown("(missing)", fd);
    }

    if (strcmp(method, "echo") == 0)  return handle_echo(req, fd);
    if (strcmp(method, "add") == 0)   return handle_add(req, fd);
    if (strcmp(method, "stats") == 0) return handle_stats(req, fd);
    if (strcmp(method, "quit") == 0)  return NULL; /* 特殊: 返回 NULL 表示断开 */

    return handle_unknown(method, fd);
}

/* ---- 客户端处理线程 ---- */
typedef struct {
    Server *server;
    int     fd;
    int     id;          /* 客户端编号 */
} ClientArgs;

static void *client_handler(void *arg)
{
    ClientArgs *ca  = (ClientArgs *)arg;
    Server     *s   = ca->server;
    int         fd  = ca->fd;
    int         cid = ca->id;
    free(ca);

    log_msg("connect", fd, "client #%d connected", cid);

    int msg_count = 0;
    while (g_running) {
        /* 接收 JSON 请求 */
        JsonValue *req = server_recv_json(s, fd);
        if (!req) {
            if (g_running)
                log_msg("disconnect", fd,
                        "client #%d disconnected (%d msgs processed)",
                        cid, msg_count);
            break;
        }
        msg_count++;

        /* 打印请求摘要 */
        char *raw = json_stringify(req);
        log_msg("request", fd, "#%d %s", msg_count, raw);
        free(raw);

        /* 分发处理 */
        JsonValue *res = dispatch(req, fd);
        json_free(req);

        /* quit 请求: 主动关闭连接 */
        if (!res) {
            JsonValue *bye = json_new_object();
            json_object_set(bye, "method", json_new_string("bye"));
            json_object_set(bye, "message",
                            json_new_string("server closing connection"));
            server_send_json(s, fd, bye);
            json_free(bye);
            log_msg("disconnect", fd, "client #%d sent quit", cid);
            break;
        }

        /* 发送响应 */
        if (server_send_json(s, fd, res) != 0) {
            json_free(res);
            log_msg("error", fd, "send failed, closing client #%d", cid);
            break;
        }
        json_free(res);
    }

    server_close_client(s, fd);
    return NULL;
}

/* ---- 主函数 ---- */
int main(int argc, char *argv[])
{
    int port = (argc > 1) ? atoi(argv[1]) : 8888;
    if (port < 1 || port > 65535) {
        fprintf(stderr, "Usage: %s [port]\n", argv[0]);
        return 1;
    }

    /* 注册信号处理 (禁用 SA_RESTART 以中断 accept) */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sa.sa_flags   = 0;  /* 不设置 SA_RESTART，这样 accept 会返回 EINTR */
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    pthread_mutex_init(&g_print_mtx, NULL);

    /* 创建服务端 (不加密，演示用) */
    Server *s = server_new(port, NULL);
    if (!s) {
        perror("server_new");
        return 1;
    }

    printf("=== Echo Server v%s ===\n", ECHO_SERVER_VERSION);
    printf("Listening on port %d (encryption: off)\n", port);
    printf("Supported methods: echo, add, stats, quit\n");
    printf("Press Ctrl+C to stop.\n\n");

    int client_id = 0;

    while (g_running) {
        int fd = server_accept(s);
        if (fd < 0) {
            if (errno == EINTR && !g_running) break;
            perror("accept");
            continue;
        }

        client_id++;

        /* 为每个客户端创建线程 */
        ClientArgs *ca = malloc(sizeof(*ca));
        ca->server = s;
        ca->fd     = fd;
        ca->id     = client_id;

        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

        if (pthread_create(&tid, &attr, client_handler, ca) != 0) {
            log_msg("error", fd, "pthread_create failed for client #%d", client_id);
            server_close_client(s, fd);
            free(ca);
        }

        pthread_attr_destroy(&attr);
    }

    printf("\nShutting down...\n");
    server_free(s);
    pthread_mutex_destroy(&g_print_mtx);
    printf("Server stopped.\n");
    return 0;
}
