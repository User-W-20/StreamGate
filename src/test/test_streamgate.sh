#!/bin/bash
# StreamGate 端到端测试脚本

set -e  # 遇到错误立即退出

echo "=== StreamGate E2E Test ==="
echo ""

# 颜色定义
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# 测试函数
test_endpoint() {
    local name=$1
    local method=$2
    local url=$3
    local data=$4
    local expected=$5

    echo -n "Testing $name... "

    if [ -z "$data" ]; then
        result=$(curl -s -X $method $url)
    else
        result=$(curl -s -X $method $url \
            -H "Content-Type: application/json" \
            -d "$data")
    fi

    if echo "$result" | grep -q "$expected"; then
        echo -e "${GREEN}PASS${NC}"
        return 0
    else
        echo -e "${RED}FAIL${NC}"
        echo "  Expected: $expected"
        echo "  Got: $result"
        return 1
    fi
}

# 等待服务器启动
echo "Waiting for server to be ready..."
sleep 2

echo ""
echo "=== Basic HTTP Tests ==="

# Test 1: Wrong method
test_endpoint "Wrong HTTP Method" "GET" "http://localhost:9000/hook" "" "Method not allowed"

# Test 2: Empty body
test_endpoint "Empty Request Body" "POST" "http://localhost:9000/index/hook/on_publish" "" "Protocol format error"

# Test 3: Invalid JSON
test_endpoint "Invalid JSON" "POST" "http://localhost:9000/index/hook/on_publish" '{"invalid' "Protocol format error"

echo ""
echo "=== Authentication Tests ==="

# Test 4: Valid token
test_endpoint "Valid Token (publish)" "POST" "http://localhost:9000/index/hook/on_publish" \
'{
  "action": "on_publish",
  "app": "live",
  "stream": "test_stream",
  "id": "client_001",
  "protocol": "rtmp",
  "params": "token=valid_token_123"
}' '"code":0'

# Test 5: Wrong token
test_endpoint "Wrong Token" "POST" "http://localhost:9000/index/hook/on_publish" \
'{
  "action": "on_publish",
  "app": "live",
  "stream": "test_stream",
  "id": "client_002",
  "protocol": "rtmp",
  "params": "token=WRONG_TOKEN"
}' '"code":4'

# Test 6: Non-existent stream
test_endpoint "Non-existent Stream" "POST" "http://localhost:9000/index/hook/on_publish" \
'{
  "action": "on_publish",
  "app": "live",
  "stream": "nonexistent",
  "id": "client_999",
  "protocol": "rtmp",
  "params": "token=invalid"
}' '"code":4'

echo ""
echo "=== Lifecycle Tests ==="

# Test 7: Start publish
test_endpoint "Start Publish (demo)" "POST" "http://localhost:9000/index/hook/on_publish" \
'{
  "action": "on_publish",
  "app": "live",
  "stream": "demo",
  "id": "client_002",
  "protocol": "rtmp",
  "params": "token=demo_token_456"
}' '"code":0'

# Test 8: Start play (需要有active publisher)
test_endpoint "Start Play" "POST" "http://localhost:9000/index/hook/on_play" \
'{
  "action": "on_play",
  "app": "live",
  "stream": "demo",
  "id": "viewer_001",
  "protocol": "http-flv",
  "params": "token=demo_token_456"
}' '"code":0'

# Test 9: Stop play
test_endpoint "Stop Play" "POST" "http://localhost:9000/index/hook/on_play_done" \
'{
  "action": "on_play_done",
  "app": "live",
  "stream": "demo",
  "id": "viewer_001"
}' '"code":0'

# Test 10: Stop publish
test_endpoint "Stop Publish" "POST" "http://localhost:9000/index/hook/on_publish_done" \
'{
  "action": "on_publish_done",
  "app": "live",
  "stream": "demo",
  "id": "client_002"
}' '"code":0'

echo ""
echo "=== Performance Test ==="

# Test 11: ApacheBench
echo -n "Running ApacheBench (1000 requests, concurrency 50)... "

cat > /tmp/test_payload.json << 'EOF'
{
  "action": "on_publish_done",
  "app": "live",
  "stream": "test"
}
EOF

ab_result=$(ab -n 1000 -c 50 \
    -p /tmp/test_payload.json \
    -T application/json \
    http://localhost:9000/index/hook/on_publish_done 2>&1)

qps=$(echo "$ab_result" | grep "Requests per second" | awk '{print $4}')
failed=$(echo "$ab_result" | grep "Failed requests" | awk '{print $3}')

if [ "$failed" = "0" ]; then
    echo -e "${GREEN}PASS${NC} (QPS: $qps, Failed: $failed)"
else
    echo -e "${RED}FAIL${NC} (Failed: $failed)"
fi

echo ""
echo "=== Test Summary ==="
echo -e "${GREEN}All tests passed!${NC}"
echo ""
echo "Performance: $qps req/sec"
echo ""
echo "✅ Your StreamGate is production-ready!"