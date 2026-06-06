# sklib Examples — Makefile
#
# 构建:
#   make                     构建所有程序
#   make echo_server         只构建服务端
#   make client              只构建交互式客户端
#   make test_batch          只构建批量测试客户端
#   make test_multi_client   只构建 C 原生并发测试
#
# 测试:
#   make test-c              C 原生多客户端并发测试 (推荐, 最可靠)
#   make test                shell 脚本并发测试
#   make test-manual         手动测试 (先启动服务端, 再用 client 交互)
#
# 清理:
#   make clean

CC         = gcc
CFLAGS     = -std=c11 -Wall -Wextra -pedantic
LDFLAGS    = -lpthread
SKLIB_DIR  = ..
INCLUDES   = -I$(SKLIB_DIR)

# 直接链接源文件 (避免库路径问题)
SRCS       = $(SKLIB_DIR)/message.c $(SKLIB_DIR)/security.c \
             $(SKLIB_DIR)/json.c $(SKLIB_DIR)/server.c $(SKLIB_DIR)/client.c

.PHONY: all clean test test-c test-manual

all: echo_server client test_batch test_multi_client

echo_server: echo_server.c $(SKLIB_DIR)/*.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ echo_server.c $(SRCS) $(LDFLAGS)

client: client.c $(SKLIB_DIR)/*.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ client.c $(SRCS) $(LDFLAGS)

test_batch: test_batch.c $(SKLIB_DIR)/*.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_batch.c $(SRCS) $(LDFLAGS)

test_multi_client: test_multi_client.c $(SKLIB_DIR)/*.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_multi_client.c $(SRCS) $(LDFLAGS)

# C 原生多客户端并发测试 (推荐)
test-c: echo_server test_multi_client
	./test_multi_client

# shell 脚本并发测试
test: echo_server test_batch
	bash test_concurrent.sh

# 手动测试提示
test-manual: echo_server client
	@echo "=== 手动测试 ==="
	@echo "1. 在一个终端运行:  ./echo_server 8888"
	@echo "2. 在另一个终端运行: ./client 127.0.0.1 8888"
	@echo "3. 可同时启动多个 client 实例测试并发"

clean:
	rm -f echo_server client test_batch test_multi_client
