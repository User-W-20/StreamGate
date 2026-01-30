//
// Created by wxx on 2025/12/22.
//

#ifndef STREAMGATE_HOOKCONTROLLER_H
#define STREAMGATE_HOOKCONTROLLER_H
#include "ZlmHookCommon.h"
#include "HookUseCase.h"

using ZlmHookCallback = std::function<void(const ZlmHookResponse&)>;

/**
 * @brief ZLM Webhook 路由控制器 (Thin Routing Layer)
 * * 职责：
 * 1. 识别 Hook 请求动作类型
 * 2. 路由至对应的业务用例 (HookUseCase)
 * 3. 将业务决策 (HookDecision) 映射为协议响应 (ZlmHookResponse)
 * * 注意：本类不包含任何业务逻辑，仅负责流量分发。
 */
class HookController
{
public:
    explicit HookController(HookUseCase& use_case);

    /**
     * @brief 路由 Hook 请求到对应的处理函数
     * * @param hook 已解析并验证过的 Hook 请求对象
     * @param callback 结果回调函数
     * * 时间语义：
     * - Publish / Play: 可能会涉及外部鉴权或数据库操作，回调通常在线程池中【异步】执行。
     * - Done / NoneReader: 属于状态清理通知，回调将在当前调用栈【同步】执行。
     */
    void routeHook(const ZlmHookRequest& hook, ZlmHookCallback callback) const;

private:
    // 处理流发布（推流开始）
    void handlePublish(const ZlmHookRequest& hook, ZlmHookCallback callback) const;

    // 处理播放（拉流开始）
    void handlePlay(const ZlmHookRequest& hook, ZlmHookCallback callback) const;

    // 处理发布停止（推流结束，合并了 StreamNoneReader 逻辑）
    void handlePublishDone(const ZlmHookRequest& hook, const ZlmHookCallback& callback) const;

    // 处理播放停止（拉流结束）
    void handlePlayDone(const ZlmHookRequest& hook, const ZlmHookCallback& callback) const;

    HookUseCase& _use_case;
};
#endif //STREAMGATE_HOOKCONTROLLER_H
