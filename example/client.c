/**
 * client — sklib 交互式测试客户端
 *
 * 连接到 echo_server，提供交互式菜单发送各种 JSON 请求。
 *
 * 构建: make
 * 运行: ./client [host] [port]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "client.h"
#include "json.h"

static void print_usage(const char *prog)
{
    printf("Usage: %s [host] [port]\n", prog);
    printf("Default: 127.0.0.1:8888\n");
}

static void print_menu(void)
{
    printf("\n┌──────────────────────────────────────┐\n");
    printf("│  sklib Test Client                   │\n");
    printf("├──────────────────────────────────────┤\n");
    printf("│  1. echo  — 回显消息                  │\n");
    printf("│  2. add   — 计算 a + b                │\n");
    printf("│  3. stats — 数组统计                  │\n");
    printf("│  4. raw   — 发送自定义 JSON            │\n");
    printf("│  q. quit  — 退出                      │\n");
    printf("└──────────────────────────────────────┘\n");
    printf("Choice: ");
}

static void do_echo(Client *c)
{
    char msg[256];
    printf("Message: ");
    if (!fgets(msg, sizeof(msg), stdin)) return;
    msg[strcspn(msg, "\n")] = '\0';

    JsonValue *req = json_new_object();
    json_object_set(req, "method",  json_new_string("echo"));
    json_object_set(req, "message", json_new_string(msg));

    if (client_send_json(c, req) != 0) {
        printf("Send failed.\n");
        json_free(req);
        return;
    }
    json_free(req);

    JsonValue *res = client_recv_json(c);
    if (!res) { printf("Recv failed.\n"); return; }

    const char *echo = json_as_string(json_object_get(res, "echo"));
    printf("Response: %s\n", echo ? echo : "(null)");
    json_free(res);
}

static void do_add(Client *c)
{
    double a, b;
    printf("a = ");
    if (scanf("%lf", &a) != 1) { while (getchar() != '\n'); return; }
    printf("b = ");
    if (scanf("%lf", &b) != 1) { while (getchar() != '\n'); return; }
    while (getchar() != '\n');

    JsonValue *req = json_new_object();
    json_object_set(req, "method", json_new_string("add"));
    json_object_set(req, "a",      json_new_number(a));
    json_object_set(req, "b",      json_new_number(b));

    if (client_send_json(c, req) != 0) {
        printf("Send failed.\n");
        json_free(req);
        return;
    }
    json_free(req);

    JsonValue *res = client_recv_json(c);
    if (!res) { printf("Recv failed.\n"); return; }

    double result = json_as_number(json_object_get(res, "result"));
    printf("%.2f + %.2f = %.2f\n", a, b, result);
    json_free(res);
}

static void do_stats(Client *c)
{
    char line[512];
    printf("Values (comma-separated numbers): ");
    if (!fgets(line, sizeof(line), stdin)) return;
    line[strcspn(line, "\n")] = '\0';

    JsonValue *req = json_new_object();
    json_object_set(req, "method", json_new_string("stats"));

    JsonValue *arr = json_new_array();
    char *token = strtok(line, ",");
    while (token) {
        json_array_push(arr, json_new_number(strtod(token, NULL)));
        token = strtok(NULL, ",");
    }
    json_object_set(req, "values", arr);

    if (client_send_json(c, req) != 0) {
        printf("Send failed.\n");
        json_free(req);
        return;
    }
    json_free(req);

    JsonValue *res = client_recv_json(c);
    if (!res) { printf("Recv failed.\n"); return; }

    printf("count = %.0f\n", json_as_number(json_object_get(res, "count")));
    printf("sum   = %.2f\n", json_as_number(json_object_get(res, "sum")));
    printf("avg   = %.2f\n", json_as_number(json_object_get(res, "avg")));
    printf("min   = %.2f\n", json_as_number(json_object_get(res, "min")));
    printf("max   = %.2f\n", json_as_number(json_object_get(res, "max")));
    json_free(res);
}

static void do_raw(Client *c)
{
    printf("Enter JSON (single line): ");
    char line[4096];
    if (!fgets(line, sizeof(line), stdin)) return;
    line[strcspn(line, "\n")] = '\0';

    JsonValue *req = json_parse(line);
    if (!req) { printf("Invalid JSON.\n"); return; }

    if (client_send_json(c, req) != 0) {
        printf("Send failed.\n");
        json_free(req);
        return;
    }
    json_free(req);

    JsonValue *res = client_recv_json(c);
    if (!res) { printf("Recv failed.\n"); return; }

    char *raw = json_stringify(res);
    printf("Response: %s\n", raw);
    free(raw);
    json_free(res);
}

int main(int argc, char *argv[])
{
    const char *host = (argc > 1) ? argv[1] : "127.0.0.1";
    int         port = (argc > 2) ? atoi(argv[2]) : 8888;

    if (port < 1 || port > 65535) {
        print_usage(argv[0]);
        return 1;
    }

    printf("Connecting to %s:%d ...\n", host, port);

    Client *c = client_new(host, port, NULL);
    if (!c) { perror("client_new"); return 1; }

    if (client_connect(c) != 0) {
        perror("client_connect");
        client_free(c);
        return 1;
    }

    printf("Connected.\n");

    char choice[16];
    while (1) {
        print_menu();
        if (!fgets(choice, sizeof(choice), stdin)) break;

        switch (choice[0]) {
        case '1': do_echo(c);  break;
        case '2': do_add(c);   break;
        case '3': do_stats(c); break;
        case '4': do_raw(c);   break;
        case 'q': case 'Q':
            printf("Goodbye.\n");
            client_free(c);
            return 0;
        default:
            printf("Unknown choice.\n");
            break;
        }
    }

    client_free(c);
    return 0;
}
