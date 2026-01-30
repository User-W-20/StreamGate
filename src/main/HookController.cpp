//
// Created by wxx on 2025/12/22.
//
#include "HookController.h"
#include <sw/redis++/cxx_utils.h>
#include "Logger.h"

HookController::HookController(HookUseCase& use_case)
    : _use_case(use_case)
{
}

void HookController::routeHook(const ZlmHookRequest& hook, ZlmHookCallback callback) const
{
    switch (hook.action)
    {
    case HookAction::Publish:
        handlePublish(hook, std::move(callback));
        break;

    case HookAction::Play:
        handlePlay(hook, std::move(callback));
        break;

    // 语义化合并：无人观看和发布停止在 StreamGate 中均视为流结束
    case HookAction::PublishDone:
    case HookAction::StreamNoneReader:
        handlePublishDone(hook, callback);
        break;

    case HookAction::PlayDone:
        handlePlayDone(hook, callback);
        break;

    default:
        LOG_WARN("Unsupported hook action received");
        callback(ZlmHookResponse(ZlmHookResult::UNSUPPORTED_ACTION, "Unsupported action"));
        break;
    }
}

void HookController::handlePublish(const ZlmHookRequest& hook, ZlmHookCallback callback) const
{
    _use_case.processPublish(hook, [cb=std::move(callback)](const auto& dec)
    {
        cb(dec.to_response());
    });
}

void HookController::handlePlay(const ZlmHookRequest& hook, ZlmHookCallback callback) const
{
    LOG_INFO("DEBUG: on_play hook content: vhost=" + hook.vhost + ", app=" + hook.app + ", stream=" + hook.stream);
    _use_case.processPlay(hook, [cb = std::move(callback)](const auto& dec)
    {
        cb(dec.to_response());
    });
}

void HookController::handlePublishDone(const ZlmHookRequest& hook, const ZlmHookCallback& callback) const
{
    callback(_use_case.processPublishDone(hook).to_response());
}

void HookController::handlePlayDone(const ZlmHookRequest& hook, const ZlmHookCallback& callback) const
{
    callback(_use_case.processPlayDone(hook).to_response());
}
