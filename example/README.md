### 使用示例

<pre>
example/
├── Makefile              # 构建和测试
├── echo_server.c         # 多线程并发回显服务
├── echo_server           # (编译产物)
├── client.c              # 交互式测试客户端
├── client                # (编译产物)
├── test_batch.c           # 批量测试客户端 (非交互)
├── test_batch             # (编译产物)
├── test_multi_client.c    # C 原生 10 并发客户端测试
├── test_multi_client      # (编译产物)
└── test_concurrent.sh     # Shell 并发测试脚本
echo_server 功能
服务端使用 pthread 一客户端一线程 模式处理并发，支持 4 种 JSON 方法：

方法	请求示例	响应
echo	{"method":"echo","message":"hello"}	{"method":"echo","echo":"hello"}
add	{"method":"add","a":3,"b":7}	{"method":"add","a":3,"b":7,"result":10}
stats	{"method":"stats","values":[1,2,3]}	{"method":"stats","count":3,"sum":6,"avg":2,...}
quit	{"method":"quit"}	{"method":"bye","message":"server closing"} → 断开
使用方式

cd example/

# 构建
make

# 启动服务端
./echo_server 8888

# 另一个终端 — 交互式客户端
./client 127.0.0.1 8888

# 批量测试
echo '{"method":"add","a":1,"b":2}' | ./test_batch

# C 原生 10 并发测试 (验证多客户端)
make test-c
测试结果

=== Results: 10 passed, 0 failed ===
10 个线程同时连接服务端，分别发送 echo 和 add 请求，全部正确返回。服务端日志中可以看到并发的 [connect]/[request]/[disconnect] 交错输出，验证了多线程并发处理能力。
</pre>