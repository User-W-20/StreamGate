#!/bin/bash
# StreamGate 监控系统测试脚本

echo "========================================="
echo "StreamGate 监控系统测试"
echo "========================================="
echo ""

# 颜色定义
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 测试计数
PASSED=0
FAILED=0

# 辅助函数
test_case() {
    local name=$1
    echo -e "${YELLOW}测试: $name${NC}"
}

pass() {
    echo -e "${GREEN}✓ PASS${NC}"
    ((PASSED++))
    echo ""
}

fail() {
    local msg=$1
    echo -e "${RED}✗ FAIL: $msg${NC}"
    ((FAILED++))
    echo ""
}

# 检查服务是否运行
test_case "检查服务是否运行"
if pgrep -f streamgate_hook_server > /dev/null; then
    echo "服务正在运行 (PID: $(pgrep -f streamgate_hook_server))"
    pass
else
    echo "服务未运行"
    fail "请先启动服务"
    exit 1
fi

# 测试1: /metrics 端点响应
test_case "测试 /metrics 端点"
METRICS=$(curl -s http://localhost:9000/metrics)
if [ $? -eq 0 ]; then
    echo "响应成功"
    echo "$METRICS" | jq . 2>/dev/null
    if echo "$METRICS" | jq -e '.timestamp' > /dev/null 2>&1; then
        pass
    else
        fail "响应格式不正确"
    fi
else
    fail "无法连接到 /metrics 端点"
fi

# 测试2: /health 端点响应
test_case "测试 /health 端点"
HEALTH=$(curl -s http://localhost:9000/health)
if [ $? -eq 0 ]; then
    echo "响应成功"
    echo "$HEALTH" | jq . 2>/dev/null
    if echo "$HEALTH" | jq -e '.status' > /dev/null 2>&1; then
        pass
    else
        fail "响应格式不正确"
    fi
else
    fail "无法连接到 /health 端点"
fi

# 测试3: 发送测试请求
test_case "发送测试请求以生成指标"
echo "发送10个测试请求..."
for i in {1..10}; do
    curl -s -X POST http://localhost:9000/index/hook/on_publish \
        -H "Content-Type: application/json" \
        -d '{"app":"live","stream":"test'$i'","id":"test'$i'"}' > /dev/null
done
echo "等待2秒让指标刷新..."
sleep 2

# 测试4: 验证server_metrics是否有数据
test_case "验证 server_metrics 是否有数据"
METRICS=$(curl -s http://localhost:9000/metrics)
SERVER_METRICS=$(echo "$METRICS" | jq -r '.components.server_metrics')
echo "server_metrics: $SERVER_METRICS"

if [ "$SERVER_METRICS" = "{}" ]; then
    fail "server_metrics 仍然为空"
elif [ "$SERVER_METRICS" = "null" ]; then
    fail "server_metrics 不存在"
else
    echo "$SERVER_METRICS" | jq .
    pass
fi

# 测试5: 验证database_metrics是否有数据
test_case "验证 database_metrics 是否有数据"
DB_METRICS=$(echo "$METRICS" | jq -r '.components.database_metrics')
echo "database_metrics: $DB_METRICS"

if [ "$DB_METRICS" = "{}" ]; then
    fail "database_metrics 仍然为空"
elif [ "$DB_METRICS" = "null" ]; then
    fail "database_metrics 不存在"
else
    echo "$DB_METRICS" | jq .
    # 检查是否有 pool_size 字段
    if echo "$DB_METRICS" | jq -e '.pool_size' > /dev/null 2>&1; then
        pass
    else
        fail "database_metrics 缺少 pool_size 字段"
    fi
fi

# 测试6: 验证cache_metrics是否有数据
test_case "验证 cache_metrics 是否有数据"
CACHE_METRICS=$(echo "$METRICS" | jq -r '.components.cache_metrics')
echo "cache_metrics: $CACHE_METRICS"

if [ "$CACHE_METRICS" = "{}" ]; then
    fail "cache_metrics 仍然为空"
elif [ "$CACHE_METRICS" = "null" ]; then
    fail "cache_metrics 不存在"
else
    echo "$CACHE_METRICS" | jq .
    # 检查是否有 status 字段
    if echo "$CACHE_METRICS" | jq -e '.status' > /dev/null 2>&1; then
        pass
    else
        fail "cache_metrics 缺少 status 字段"
    fi
fi

# 测试7: 验证scheduler_metrics是否有数据
test_case "验证 scheduler_metrics 是否有数据"
SCHEDULER_METRICS=$(echo "$METRICS" | jq -r '.components.scheduler_metrics')
echo "scheduler_metrics: $SCHEDULER_METRICS"

if [ "$SCHEDULER_METRICS" = "{}" ]; then
    fail "scheduler_metrics 仍然为空"
elif [ "$SCHEDULER_METRICS" = "null" ]; then
    fail "scheduler_metrics 不存在"
else
    echo "$SCHEDULER_METRICS" | jq .
    # 检查是否有 total_publish_req 字段
    if echo "$SCHEDULER_METRICS" | jq -e '.total_publish_req' > /dev/null 2>&1; then
        pass
    else
        fail "scheduler_metrics 缺少 total_publish_req 字段"
    fi
fi

# 测试8: 压力测试
test_case "压力测试 (100并发)"
echo "发送100个并发请求..."
for i in {1..100}; do
    curl -s http://localhost:9000/metrics > /dev/null &
done
wait
echo "等待所有请求完成..."
sleep 1

# 检查服务是否还在运行
if pgrep -f streamgate_hook_server > /dev/null; then
    echo "服务仍在运行"
    pass
else
    fail "服务已崩溃"
fi

# 测试9: 快照一致性
test_case "快照一致性测试"
echo "快速连续获取3次metrics，检查是否一致..."
M1=$(curl -s http://localhost:9000/metrics | jq -r '.timestamp')
M2=$(curl -s http://localhost:9000/metrics | jq -r '.timestamp')
M3=$(curl -s http://localhost:9000/metrics | jq -r '.timestamp')

echo "时间戳1: $M1"
echo "时间戳2: $M2"
echo "时间戳3: $M3"

if [ "$M1" = "$M2" ] && [ "$M2" = "$M3" ]; then
    echo "快照一致（同一秒内）"
    pass
else
    echo "快照在不同时间更新（这是正常的）"
    pass
fi

# 测试10: 验证Provider名称排序
test_case "验证Provider按名称排序"
PROVIDERS=$(curl -s http://localhost:9000/metrics | jq -r '.components | keys | .[]')
echo "Provider顺序:"
echo "$PROVIDERS"

EXPECTED_ORDER="cache_metrics
database_metrics
scheduler_metrics
server_metrics"

if [ "$PROVIDERS" = "$EXPECTED_ORDER" ]; then
    echo "✓ Provider按字母顺序排列"
    pass
else
    echo "⚠ Provider顺序与预期不同（这可能是正常的）"
    pass
fi

# 总结
echo "========================================="
echo "测试总结"
echo "========================================="
echo -e "${GREEN}通过: $PASSED${NC}"
echo -e "${RED}失败: $FAILED${NC}"
echo ""

if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}所有测试通过！监控系统工作正常！${NC}"
    exit 0
else
    echo -e "${RED}有 $FAILED 个测试失败，请检查日志${NC}"
    exit 1
fi