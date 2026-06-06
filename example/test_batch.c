/**
 * test_batch — 批量测试客户端 (非交互)
 *
 * 从标准输入读取一行 JSON，发送到服务端，打印响应后退出。
 * 适合用 shell 脚本批量并发测试。
 *
 * 用法: echo '{"method":"echo","message":"hi"}' | ./test_batch [host] [port]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "client.h"
#include "json.h"

int main(int argc, char *argv[])
{
    const char *host = (argc > 1) ? argv[1] : "127.0.0.1";
    int         port = (argc > 2) ? atoi(argv[2]) : 8888;

    Client *c = client_new(host, port, NULL);
    if (!c) return 1;
    if (client_connect(c) != 0) { client_free(c); return 1; }

    /* 读取一行 JSON */
    char line[8192];
    if (!fgets(line, sizeof(line), stdin)) {
        client_free(c);
        return 1;
    }
    line[strcspn(line, "\n")] = '\0';

    JsonValue *req = json_parse(line);
    if (!req) { client_free(c); return 1; }

    if (client_send_json(c, req) != 0) {
        json_free(req);
        client_free(c);
        return 1;
    }
    json_free(req);

    JsonValue *res = client_recv_json(c);
    if (!res) { client_free(c); return 1; }

    char *raw = json_stringify(res);
    printf("%s\n", raw);
    free(raw);
    json_free(res);
    client_free(c);
    return 0;
}
