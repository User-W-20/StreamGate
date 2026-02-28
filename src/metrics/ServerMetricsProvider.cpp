//
// Created by wxx on 2026/2/6.
//
#include "ServerMetricsProvider.h"
#include "MetricsRegistry.h"
#include <charconv>

//静态注册宏：包含 ForceLink 锚点
REGISTER_METRICS(ServerMetricsProvider)

// --- WorkerStatsSlot 实现 ---
void WorkerStatsSlot::update(uint64_t t, uint64_t s, uint64_t f) noexcept
{
    seq.fetch_add(1, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_release);

    total_requests = t;
    success_requests = s;
    failed_requests = f;

    std::atomic_thread_fence(std::memory_order_release);
    seq.fetch_add(1, std::memory_order_release);
}

void WorkerStatsSlot::read(uint64_t& t, uint64_t& s, uint64_t& f) const noexcept
{
    uint64_t s1, s2;
    do
    {
        s1 = seq.load(std::memory_order_acquire);

        std::atomic_thread_fence(std::memory_order_acquire);

        t = total_requests;
        s = success_requests;
        f = failed_requests;

        std::atomic_thread_fence(std::memory_order_acquire);
        s2 = seq.load(std::memory_order_acquire);
    }
    while ((s1 & 1) || s1 != s2);
}

// --- ThreadLocalRegistry 实现 ---
ThreadLocalRegistry& ThreadLocalRegistry::instance()
{
    static ThreadLocalRegistry reg;
    return reg;
}

std::optional<size_t> ThreadLocalRegistry::acquireSlot()
{
    for (size_t m = 0; m < 2; ++m)
    {
        uint64_t mask = _active_mask->load(std::memory_order_relaxed);
        for (size_t i = 0; i < 64; ++i)
        {
            if (!(mask & (1ULL << i)))
            {
                // 尝试抢占
                if (!(_active_mask[m].fetch_or(1ULL << i, std::memory_order_acq_rel) & (1ULL << i)))
                {
                    size_t id = m * 64 + i;
                    _slots[id].update(0, 0, 0); // 复用前清空
                    return id;
                }
                // 抢占失败说明并发了，继续搜索
                mask = _active_mask[m].load(std::memory_order_relaxed);
            }
        }
    }
    return std::nullopt;
}

void ThreadLocalRegistry::releaseSlot(size_t id)
{
    if (id >= kMaxWorkers)return;

    uint64_t t, s, f, ht, hs, hf;
    _slots[id].read(t, s, f);

    //物理归零
    _slots[id].update(0, 0, 0);

    //归并历史
    _historical_stats.read(ht, hs, hf);
    _historical_stats.update(ht + t, hs + s, hf + f);

    //内存屏障确保归零后再释放标志位
    std::atomic_thread_fence(std::memory_order_seq_cst);
    _active_mask[id / 64].fetch_and(~(1ULL << (id % 64)), std::memory_order_release);
}

void ThreadLocalRegistry::aggregate(uint64_t& t, uint64_t& s, uint64_t& f) const noexcept
{
    uint64_t tt = 0, ts = 0, tf = 0;
    _historical_stats.read(tt, ts, tf);

    for (size_t m = 0; m < 2; ++m)
    {
        uint64_t mask = _active_mask[m].load(std::memory_order_acquire);
        while (mask)
        {
            int i = __builtin_ctzll(mask);
            uint64_t st, ss, sf;
            _slots[m * 64 + i].read(st, ss, sf);
            tt += st;
            ts += ss;
            tf += sf;
            mask &= ~(1ULL << i);
        }
    }
    t = tt;
    s = ts;
    f = tf;
}

// --- ServerMetricsProvider 实现 ---
void ServerMetricsProvider::refresh() noexcept
{
    uint64_t t, s, f;
    ThreadLocalRegistry::instance().aggregate(t, s, f);

    //预分配缓冲区：thread_local 规避堆分配与多线程竞争
    static constexpr size_t kBufferSize = 1024;
    thread_local char buffer[kBufferSize];

    //每次进入重置指针
    char* ptr = buffer;
    char* const end = buffer + kBufferSize;

    //定义辅助 Lambda：保持偏执的零分配与类型安全
    // 这里的 noexcept 保证了整体 refresh 的异常安全性
    auto append_metric = [&](std::string_view name, uint64_t value)noexcept
    {
        // 偏执检查：名称长度 + 数字最大长度(20) + 符号位(2)
        if (ptr + name.size() + 22 >= end)[[unlikely]]
        {
            return;
        }

        // 写入指标名
        ptr = std::copy_n(name.data(), name.size(), ptr);
        *ptr++ = ' ';

        // 使用 <charconv> 实现 0 分配转换，end - 1 为 '\n' 留坑
        auto [p,ec] = std::to_chars(ptr, end - 1, value);
        if (ec == std::errc{})[[likely]]
        {
            ptr = p;
            *ptr++ = '\n';
        }
    };

    // 4. 按照 Prometheus 标准文本格式写入
    // 指标名带上网关前缀，方便在 Grafana 中直接检索
    append_metric("streamgate_requests_total", t);
    append_metric("streamgate_requests_success", s);
    append_metric("streamgate_requests_failed", f);

    //发布快照：通过 string_view 传递，不涉及字符串拷贝
    // 注意：updateSnapshot 内部若需跨线程则由其自行决定是否拷贝
    updateSnapshot(std::string_view(buffer, static_cast<size_t>(ptr - buffer)));
}

extern "C" void ForceLink_ServerMetricsProvider()
{
}
