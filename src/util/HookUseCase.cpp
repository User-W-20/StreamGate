//
// Created by wxx on 2026/1/22.
//
#include "HookUseCase.h"

void HookUseCase::processPublish(const ZlmHookRequest& req, HookDecisionCallback cb) const
{
    _scheduler.onPublish(req.stream_key(), req.client_id, req.get_token(), req.protocol,
                         [cb=std::move(cb)](const auto& res)
                         {
                             cb(mapResult(res));
                         });
}

void HookUseCase::processPlay(const ZlmHookRequest& req, HookDecisionCallback cb) const
{
    LOG_INFO("Processing play request for stream: " + req.stream);

    _scheduler.onPlay(req.stream_key(), req.client_id, req.get_token(), req.protocol,
                      [cb=std::move(cb)](const auto& res)
                      {
                          cb(mapResult(res));
                      });
}

HookDecision HookUseCase::processPublishDone(const ZlmHookRequest& req) const
{
    _scheduler.onPublishDone(req.stream_key(), req.client_id);
    return HookDecision::allow();
}

HookDecision HookUseCase::processPlayDone(const ZlmHookRequest& req) const
{
    _scheduler.onPlayDone(req.stream_key(), req.client_id);
    return HookDecision::allow();
}

HookDecision HookUseCase::mapResult(const StreamTaskScheduler::SchedulerResult& res)
{
    if (res.error == StreamTaskScheduler::SchedulerResult::Error::SUCCESS)return HookDecision::allow();
    return HookDecision::deny(res.message);
}
