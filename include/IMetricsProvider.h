//
// Created by wxx on 2026/2/2.
//

#ifndef STREAMGATE_IMETRICSPROVIDER_H
#define STREAMGATE_IMETRICSPROVIDER_H
#include <string_view>
#include <atomic>
#include <memory>
#include <nlohmann/json.hpp>

/**
 * @brief 平台适配与段名定义
 */
#if defined(__ELF__) || defined(__linux__)
#define SG_SECTION_NAME "streamgate_metrics"
#define SG_USED __attribute__((used))
#define SG_RETAIN
#elif defined(__APPLE__)
#define SG_SECTION_NAME "__DATA,streamgate_metrics"
#define SG_USED __attribute__((used))
#define SG_RETAIN __attribute__((retain))
#else
#error "Current platform not supported for Linker-Section Registry"
#endif


/**
 * @brief IMetricsProvider
 * * 核心设计哲学：
 * 1. 读写分离：业务热路径只管更新原子变量，监控线程负责定期构建快照。
 * 2. 零阻塞契约：exportMetrics() 必须保证 O(1) 耗时，禁止任何 IO 或锁竞争。
 * 3. 内存安全：基于 CoW (Copy-on-Write) 模式，确保多线程读取时快照的一致性。
 */
class IMetricsProvider
{
public:
    /**
     * @brief 构造函数：初始化空快照，消除 Release 模式下的空指针检查
     */
    IMetricsProvider() : _metrics_cache()
    {
        _metrics_cache.store(
            std::make_shared<const nlohmann::json>(nlohmann::json::object()),
            std::memory_order_relaxed
        );
    }

    virtual ~IMetricsProvider() = default;

    /**
     * @brief 返回组件唯一标识名
     * @warning 必须指向 static/constexpr 存储期字符串，严禁返回局部变量。
     * @code
     *  return "redis_manager"sv;
     *  @endcode
     */
    virtual std::string_view metricsName() const noexcept =0;

    /**
     * @brief 供调度线程调用，由子类实现具体的统计汇总逻辑
     */
    virtual void refresh() noexcept =0;

    /**
     * @brief 导出当前监控快照
     * 满足极速读取需求：1 次原子加载 + 1 次指针解引用。
     */
    nlohmann::json exportMetrics() const
    {
        // 原子读取当前最新的快照指针
        auto ptr = _metrics_cache.load(std::memory_order_acquire);
        if (!ptr) return nlohmann::json::object();
        // 契约保证：构造函数已初始化，ptr 永远不为 nullptr
        return *ptr;
    }

protected:
    /**
     * @brief 更新内存快照 (CoW 模式)
     * @param new_json 新构建的完整指标 JSON
     * @note 建议在后台线程调用。构建 new_json 的过程会有堆分配，不要放在 QPS 极高的主业务流中。
     */
    void updateSnapshot(const nlohmann::json& new_json)
    {
        // 在内存中创建新副本，不影响正在读取旧副本的线程
        auto next = std::make_shared<const nlohmann::json>(new_json);

        // 原子地切换指针，从此以后 exportMetrics 拿到的就是新数据
        _metrics_cache.store(next, std::memory_order_release);
    }

private:
    // 使用 C++20 原子智能指针特性的底层模拟（支持 shared_ptr 的原子存储）
    std::atomic<std::shared_ptr<const nlohmann::json>> _metrics_cache;
};

/**
 * @brief 辅助宏：强制规范并消除样板代码
 * 自动添加 sv 后缀，确保返回的是 string_view 且带有 noexcept
 */
#define REGISTER_METRICS_NAME(name_str) \
    [[nodiscard]] std::string_view metricsName()const noexcept override{ \
    using namespace std::literals; \
    return name_str##sv; \
    }

/**
 * @brief 核心注册宏：解决 LTO 剔除与静态初始化顺序
 */
#define REGISTER_METRICS(Type) \
        static  std::shared_ptr<IMetricsProvider> Type##_Factory(){ \
        return std::make_shared<Type>(); \
       }   \
        SG_USED SG_RETAIN \
        __attribute__((section(SG_SECTION_NAME))) \
        __attribute__((aligned(alignof(void(*))))) \
        auto const Type##Ptr =&Type##_Factory;

#endif //STREAMGATE_IMETRICSPROVIDER_H
