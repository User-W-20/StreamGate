//
// Created by wxx on 2026/2/6.
//

#ifndef STREAMGATE_METRICSREGISTRY_H_H
#define STREAMGATE_METRICSREGISTRY_H_H
#include "IMetricsProvider.h"
#include <vector>
#include <cstring>

class MetricsRegistry
{
public:
    using Factory = std::shared_ptr<IMetricsProvider>(*)();

    static MetricsRegistry& instance()
    {
        static MetricsRegistry reg;
        return reg;
    }

    /**
     * @brief 扫描 .streamgate_metrics 段并创建所有 Provider 实例
     */
    static std::vector<std::shared_ptr<IMetricsProvider>> createAll()
    {
        // 链接器自动生成的段起始/结束边界符号
        // NOLINTBEGIN(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp)
        extern void* __start_streamgate_metrics[] __attribute__((weak));
        extern void* __stop_streamgate_metrics[] __attribute__((weak));
        // NOLINTEND(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp)

        //虚引用防御：防止整个段在 LTO 期间被视为无引用而剔除
        [[maybe_unused]] volatile void* const guard = reinterpret_cast<void*>(__start_streamgate_metrics);

        std::vector<std::shared_ptr<IMetricsProvider>> providers;

        if (!__start_streamgate_metrics || !__stop_streamgate_metrics)
        {
            return providers;
        }

        void** current = __start_streamgate_metrics;
        void** end = __stop_streamgate_metrics;

        if (current >= end)
        {
            return providers;
        }

        while (current < end)
        {
            //使用裸类型进行物理拷贝
            std::shared_ptr<IMetricsProvider> (*func_ptr)() = nullptr;

            if (*current != nullptr)
            {
                std::memcpy(&func_ptr, current, sizeof(void*));
            }

            if (func_ptr != nullptr)
            {
                // 执行工厂函数，获取智能指针并存入 vector
                if (auto p = func_ptr())
                {
                    providers.push_back(std::move(p));
                }
            }
            ++current;
        }
        return providers;
    }

    // 禁止拷贝与移动
    MetricsRegistry(const MetricsRegistry&) = delete;
    MetricsRegistry& operator=(const MetricsRegistry&) = delete;

private:
    // 构造函数私有化，仅允许通过 instance() 访问
    MetricsRegistry() = default;
    ~MetricsRegistry() = default;
};
#endif //STREAMGATE_METRICSREGISTRY_H_H
