#!/bin/bash
# test_concurrent.sh — 并发多客户端测试
#
# 启动 echo_server，然后同时运行多个客户端发送不同请求。
# 验证服务端能正确处理并发连接。

set -e

PORT=19997
SERVER_PID=""
PASS=0
FAIL=0

cleanup() {
    [ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
}
trap cleanup EXIT

echo "=== 多客户端并发测试 ==="
echo ""

# 启动服务端
./echo_server "$PORT" &
SERVER_PID=$!
sleep 1  # 等待服务端就绪

echo "--- Test 1: single echo ---"
RESULT=$(echo '{"method":"echo","message":"Hello concurrent"}' | ./test_batch 127.0.0.1 "$PORT")
if echo "$RESULT" | grep -q '"Hello concurrent"'; then
    echo "  PASS: $RESULT"
    PASS=$((PASS + 1))
else
    echo "  FAIL: $RESULT"
    FAIL=$((FAIL + 1))
fi

echo "--- Test 2: single add ---"
RESULT=$(echo '{"method":"add","a":42,"b":58}' | ./test_batch 127.0.0.1 "$PORT")
if echo "$RESULT" | grep -q '"result":100'; then
    echo "  PASS: $RESULT"
    PASS=$((PASS + 1))
else
    echo "  FAIL: $RESULT"
    FAIL=$((FAIL + 1))
fi

echo "--- Test 3: 3 concurrent clients ---"
TMPD3=$(mktemp -d)
echo '{"method":"echo","message":"A"}' > "$TMPD3/in_1"
echo '{"method":"echo","message":"B"}' > "$TMPD3/in_2"
echo '{"method":"add","a":1,"b":2}'   > "$TMPD3/in_3"
for i in 1 2 3; do
    ./test_batch 127.0.0.1 "$PORT" < "$TMPD3/in_$i" > "$TMPD3/out_$i" 2>/dev/null &
done
wait
for i in 1 2 3; do
    if grep -qE '(echo|result)' "$TMPD3/out_$i" 2>/dev/null; then
        echo "  PASS [$i]: $(cat "$TMPD3/out_$i")"
        PASS=$((PASS + 1))
    else
        echo "  FAIL [$i]: $(cat "$TMPD3/out_$i")"
        FAIL=$((FAIL + 1))
    fi
done
rm -rf "$TMPD3"

echo "--- Test 4: 5 concurrent clients ---"
TMPD5=$(mktemp -d)
for i in 1 2 3 4 5; do
    echo "{\"method\":\"add\",\"a\":$i,\"b\":$((100-i))}" > "$TMPD5/in_$i"
    ./test_batch 127.0.0.1 "$PORT" < "$TMPD5/in_$i" > "$TMPD5/out_$i" 2>/dev/null &
done
wait
all_ok=true
for i in 1 2 3 4 5; do
    if ! grep -q '"result":100' "$TMPD5/out_$i" 2>/dev/null; then
        echo "  FAIL: client $i: $(cat "$TMPD5/out_$i")"
        all_ok=false
        FAIL=$((FAIL + 1))
    fi
done
if $all_ok; then
    echo "  PASS: all 5 concurrent returned result=100"
    PASS=$((PASS + 1))
fi
rm -rf "$TMPD5"

echo ""
echo "=== 结果: $PASS 通过, $FAIL 失败 ==="
[ "$FAIL" -eq 0 ] && echo "全部测试通过!" || echo "部分测试失败。"

exit $FAIL
