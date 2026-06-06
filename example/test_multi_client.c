/**
 * test_multi_client — 多客户端并发测试（纯 C 实现）
 *
 * 启动 echo_server, 创建多个线程同时发送请求, 验证结果。
 * 无需 shell 脚本，所有测试逻辑在 C 代码内完成。
 *
 * 构建: make test_multi_client
 * 运行: ./test_multi_client
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>

#include "client.h"
#include "json.h"

#define TEST_PORT  19980
#define NUM_CLIENTS 10
#define TEST_KEY    NULL  /* 不加密 */

static volatile int g_tests_passed = 0;
static volatile int g_tests_failed = 0;
static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int         id;
    const char *host;
    int         port;
    const char *method;
    double      a, b;
    const char *message;
} TestCase;

static void *run_client(void *arg)
{
    TestCase *tc = (TestCase *)arg;
    int ok = 0;

    Client *c = client_new(tc->host, tc->port, TEST_KEY);
    if (!c) goto done;

    if (client_connect(c) != 0) goto done;

    /* Build request */
    JsonValue *req = json_new_object();
    json_object_set(req, "method", json_new_string(tc->method));

    if (strcmp(tc->method, "echo") == 0) {
        json_object_set(req, "message", json_new_string(tc->message));
    } else if (strcmp(tc->method, "add") == 0) {
        json_object_set(req, "a", json_new_number(tc->a));
        json_object_set(req, "b", json_new_number(tc->b));
    }

    if (client_send_json(c, req) != 0) { json_free(req); goto done; }
    json_free(req);

    JsonValue *res = client_recv_json(c);
    if (!res) goto done;

    /* Validate */
    if (strcmp(tc->method, "echo") == 0) {
        const char *echo = json_as_string(json_object_get(res, "echo"));
        ok = (echo && strcmp(echo, tc->message) == 0);
    } else if (strcmp(tc->method, "add") == 0) {
        double result = json_as_number(json_object_get(res, "result"));
        ok = (result == tc->a + tc->b);
    }

    json_free(res);

done:
    if (c) client_free(c);

    pthread_mutex_lock(&g_mtx);
    if (ok) {
        g_tests_passed++;
        printf("  [PASS] client #%d: %s\n", tc->id, tc->message ? tc->message : "add");
    } else {
        g_tests_failed++;
        printf("  [FAIL] client #%d: %s\n", tc->id, tc->message ? tc->message : "add");
    }
    pthread_mutex_unlock(&g_mtx);

    free(tc);
    return NULL;
}

int main(void)
{
    /* Step 1: Start server (child process) */
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }

    if (pid == 0) {
        /* Child: run server */
        execl("./echo_server", "./echo_server", "19980", NULL);
        perror("execl");
        _exit(1);
    }

    /* Parent: give server time to start */
    sleep(1);

    printf("=== Multi-Client Concurrent Test ===\n");
    printf("Server PID: %d, Port: %d, Clients: %d\n\n", pid, TEST_PORT, NUM_CLIENTS);

    pthread_t threads[NUM_CLIENTS];
    const char *msgs[] = {
        "alpha", "bravo", "charlie", "delta", "echo",
        "foxtrot", "golf", "hotel", "india", "juliet"
    };

    /* Launch clients */
    printf("Launching %d concurrent clients...\n", NUM_CLIENTS);
    for (int i = 0; i < NUM_CLIENTS; i++) {
        TestCase *tc = malloc(sizeof(*tc));
        tc->id      = i + 1;
        tc->host    = "127.0.0.1";
        tc->port    = TEST_PORT;

        if (i % 2 == 0) {
            tc->method  = "echo";
            tc->message = msgs[i];
            tc->a = tc->b = 0;
        } else {
            tc->method  = "add";
            tc->message = NULL;
            tc->a = (double)(i + 1);
            tc->b = (double)(100 - i);
        }

        pthread_create(&threads[i], NULL, run_client, tc);
    }

    /* Wait for all clients */
    for (int i = 0; i < NUM_CLIENTS; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("\n=== Results: %d passed, %d failed ===\n",
           g_tests_passed, g_tests_failed);

    /* Stop server */
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
    printf("Server stopped.\n");

    return g_tests_failed > 0 ? 1 : 0;
}
