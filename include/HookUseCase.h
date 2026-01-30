//
// Created by wxx on 2026/1/21.
//

#ifndef STREAMGATE_HOOKUSECASE_H
#define STREAMGATE_HOOKUSECASE_H
#include "ZlmHookCommon.h"
#include "StreamTaskScheduler.h"

using HookDecisionCallback = std::function<void(const HookDecision&)>;

class HookUseCase
{
public:
    explicit HookUseCase(StreamTaskScheduler& s) : _scheduler(s)
    {
    }

    // 异步操作：涉及鉴权和资源调度
    void processPublish(const ZlmHookRequest& req, HookDecisionCallback cb) const;
    void processPlay(const ZlmHookRequest& req, HookDecisionCallback cb) const;

    // 同步操作：纯状态清理，无需等待回调
    HookDecision processPublishDone(const ZlmHookRequest& req) const;
    HookDecision processPlayDone(const ZlmHookRequest& req) const;

private:
    static HookDecision mapResult(const StreamTaskScheduler::SchedulerResult& res);
    StreamTaskScheduler& _scheduler;
};
#endif //STREAMGATE_HOOKUSECASE_H
