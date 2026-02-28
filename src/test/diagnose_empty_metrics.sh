#!/bin/bash
# 诊断为什么metrics components是空的

echo "========================================="
echo "诊断 metrics components 为空的问题"
echo "========================================="
echo ""

# 1. 获取当前metrics
echo "1. 当前metrics内容:"
curl -s http://localhost:9000/metrics | jq .
echo ""

# 2. 等待刷新
echo "2. 等待2秒让刷新线程运行..."
sleep 2
echo ""

# 3. 再次获取
echo "3. 刷新后的metrics:"
curl -s http://localhost:9000/metrics | jq .
echo ""

# 4. 发送一些请求来触发统计
echo "4. 发送10个测试请求..."
for i in {1..10}; do
    echo -n "."
    curl -s -X POST http://localhost:9000/index/hook/on_publish \
        -H "Content-Type: application/json" \
        -d '{"app":"live","stream":"test'$i'","id":"test'$i'"}' > /dev/null
done
echo " 完成"
echo ""

# 5. 等待刷新
echo "5. 等待2秒让指标刷新..."
sleep 2
echo ""

# 6. 检查是否有数据
echo "6. 发送请求后的metrics:"
METRICS=$(curl -s http://localhost:9000/metrics)
echo "$METRICS" | jq .
echo ""

# 7. 逐个检查每个组件
echo "7. 详细检查:"
echo ""

echo "server_metrics:"
echo "$METRICS" | jq '.components.server_metrics'
echo ""

echo "scheduler_metrics:"
echo "$METRICS" | jq '.components.scheduler_metrics'
echo ""

echo "cache_metrics:"
echo "$METRICS" | jq '.components.cache_metrics'
echo ""

echo "database_metrics:"
echo "$METRICS" | jq '.components.database_metrics'
echo ""

# 8. 检查服务日志
echo "8. 检查是否有refresh相关的错误日志:"
echo "（如果有日志文件的话）"
if [ -f streamgate.log ]; then
    tail -20 streamgate.log | grep -i "refresh\|provider\|metrics"
fi
echo ""

echo "========================================="
echo "诊断建议："
echo "========================================="

SERVER=$(echo "$METRICS" | jq '.components.server_metrics')
if [ "$SERVER" = "{}" ] || [ "$SERVER" = "null" ]; then
    echo "❌ server_metrics 是空的"
    echo "   可能原因："
    echo "   1. ServerMetricsProvider的refresh()方法没有被调用"
    echo "   2. 或者refresh()方法没有正确调用updateSnapshot()"
    echo "   3. 或者Thread-Local的aggregate()返回了0"
    echo ""
else
    echo "✓ server_metrics 有数据"
fi

DB=$(echo "$METRICS" | jq '.components.database_metrics')
if [ "$DB" = "{}" ] || [ "$DB" = "null" ]; then
    echo "❌ database_metrics 是空的"
    echo "   可能原因："
    echo "   1. DatabaseMetricsProvider的refresh()方法没有正确实现"
    echo "   2. 或者db_manager指针为空"
    echo ""
else
    echo "✓ database_metrics 有数据"
fi

CACHE=$(echo "$METRICS" | jq '.components.cache_metrics')
if [ "$CACHE" = "{}" ] || [ "$CACHE" = "null" ]; then
    echo "❌ cache_metrics 是空的"
    echo "   可能原因："
    echo "   1. CacheMetricsProvider的refresh()方法没有正确实现"
    echo "   2. 或者cache指针为空"
    echo ""
else
    echo "✓ cache_metrics 有数据"
fi

SCHED=$(echo "$METRICS" | jq '.components.scheduler_metrics')
if [ "$SCHED" = "{}" ] || [ "$SCHED" = "null" ]; then
    echo "❌ scheduler_metrics 是空的"
    echo "   可能原因："
    echo "   1. SchedulerMetricsProvider的refresh()方法没有正确实现"
    echo "   2. 或者scheduler指针为空"
    echo "   3. 或者scheduler->getMetrics()方法不存在"
    echo ""
else
    echo "✓ scheduler_metrics 有数据"
fi

echo ""
echo "建议："
echo "1. 检查每个Provider的refresh()方法是否正确调用了updateSnapshot()"
echo "2. 检查依赖注入是否成功（日志中已显示成功）"
echo "3. 检查是否有异常被catch但没有记录日志"
echo ""