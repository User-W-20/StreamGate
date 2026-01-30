//
// Created by X on 2025/11/16.
//
#include "HookServer.h"

#include <iostream>

#include "Logger.h"
#include <nlohmann/json.hpp>
#include <unordered_set>

using json = nlohmann::json;

namespace
{
    HookAction map_path_to_action(std::string_view path)
    {
        static const std::unordered_map<std::string_view, HookAction> action_map = {
            {"/index/hook/on_publish", HookAction::Publish},
            {"/index/hook/on_play", HookAction::Play},
            {"/index/hook/on_publish_done", HookAction::PublishDone},
            {"/index/hook/on_play_done", HookAction::PlayDone},
            {"/index/hook/on_stream_none_reader", HookAction::StreamNoneReader},
            {"/index/hook/on_stream_not_found", HookAction::StreamNotFound}
        };

        auto it = action_map.find(path);
        return (it != action_map.end()) ? it->second : HookAction::Unknown;
    }

    // 状态码映射逻辑集中管理
    std::pair<http::status, int> map_to_http(ZlmHookResult code)
    {
        switch (code)
        {
        case ZlmHookResult::SUCCESS:
            return {http::status::ok, 0};
        case ZlmHookResult::AUTH_DENIED:
            return {http::status::ok, 1};
        case ZlmHookResult::INVALID_FORMAT:
            return {http::status::bad_request, 2};
        case ZlmHookResult::UNSUPPORTED_ACTION:
            return {http::status::bad_request, 3};
        case ZlmHookResult::INTERNAL_ERROR:
            return {http::status::ok, 4};
        case ZlmHookResult::TIMEOUT:
            return {http::status::gateway_timeout, 5};
        case ZlmHookResult::RESOURCE_NOT_READY:
            return {http::status::service_unavailable, 6};

        default:
            return {http::status::internal_server_error, 4};
        }
    }
}

//HookSession
HookSession::HookSession(tcp::socket socket, HookController& controller)
    : _socket(std::move(socket)),
      _strand(_socket.get_executor()),
      _controller(controller)
{
}

void HookSession::do_shutdown()
{
    beast::error_code ec;
    [[maybe_unused]] auto& _ = (_socket.shutdown(tcp::socket::shutdown_send, ec), ec);

    if (ec && ec != net::error::not_connected)
    {
        LOG_DEBUG("Optional socket shutdown hint: " + ec.message());
    }
}

void HookSession::start()
{
    do_read();
}

void HookSession::do_read()
{
    _request = {};
    http::async_read(_socket, _buffer, _request,
                     beast::bind_front_handler(&HookSession::on_read, shared_from_this()));
}

void HookSession::on_read(const beast::error_code& ec, std::size_t bytes_transferred)
{
    if (ec == net::error::operation_aborted)return;

    if (ec == http::error::end_of_stream)
    {
        do_shutdown();
        return;
    }

    if (ec)
    {
        LOG_ERROR("Session read error: " + ec.message());
        return;
    }

    LOG_DEBUG("Received " + std::to_string(bytes_transferred) + " bytes from hook client");

    handle_request();
}

void HookSession::handle_request()
{
    std::string_view target{_request.target()};

    if (_request.method() != http::verb::post)
        return send_response(405, 999, "Method not allowed");

    auto path = target.substr(0, target.find('?'));

    HookAction action = map_path_to_action(path);

    if (action == HookAction::Unknown)
    {
        return send_response(404, 999, "Not found");
    }

    try
    {
        auto j = json::parse(_request.body());

        auto hook_opt = ZlmHookRequest::from_json(j);

        if (!hook_opt)
        {
            LOG_WARN("JSON parse failed for action: " + std::to_string(static_cast<int>(action)));
            return send_response(400, 2, "Invalid hook format");
        }

        hook_opt->action = action;

        _controller.routeHook(*hook_opt, [self=shared_from_this()](const ZlmHookResponse& resp)
        {
            bool expected = false;
            if (!self->_responded.compare_exchange_strong(expected, true))return;

            auto [h_status,b_code] = map_to_http(resp.code);
            net::post(self->_strand, [self,h=h_status,b=b_code,m=resp.message]
            {
                self->send_response(static_cast<int>(h), b, m);
            });
        });
    }
    catch (const json::exception& e)
    {
        LOG_WARN("JSON error: " + std::string(e.what()));
        send_response(400, 2, "Protocol format error");
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Critical process error: " + std::string(e.what()));
        auto [h_status,b_code] = map_to_http(ZlmHookResult::INTERNAL_ERROR);
        send_response(static_cast<int>(h_status), b_code, "Internal service error");
    }
}

void HookSession::send_response(int http_status, int business_code, const std::string& message)
{
    _response = {};
    _response.version(_request.version());
    _response.result(static_cast<http::status>(http_status));
    _response.set(http::field::content_type, "application/json");
    _response.set(http::field::server, "StreamGate/1.0");
    _response.keep_alive(_request.keep_alive());

    try
    {
        json rj = {{"code", business_code}, {"msg", message}};
        _response.body() = rj.dump();
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("JSON dump failed: " + std::string(e.what()));
        _response.body() = R"({"code":500,"msg":"internal error"})";
    }

    _response.prepare_payload();

    http::async_write(
        _socket,
        _response,
        net::bind_executor(
            _strand,
            [self=shared_from_this(),keep_alive=_response.keep_alive()](const beast::error_code& ec, std::size_t bytes)
            {
                self->on_write(keep_alive, ec, bytes);
            }));
}

void HookSession::on_write(bool keep_alive, const beast::error_code& ec, std::size_t bytes_transferred)
{
    if (ec == net::error::operation_aborted)return;

    if (ec)
    {
        LOG_ERROR("Write error: " + ec.message());
        return;
    }

    (void)bytes_transferred;

    if (keep_alive)
    {
        _buffer.consume(_buffer.size());
        do_read();
    }
    else
    {
        beast::error_code sc;

        [[maybe_unused]] auto& _ = (_socket.shutdown(tcp::socket::shutdown_send, sc), sc);

        if (sc)
        {
            LOG_DEBUG("HookSession: Socket shutdown notice: " + sc.message());
        }
    }
}

//HookListener

HookListener::HookListener(net::io_context& ioc, const tcp::endpoint& endpoint, HookController& controller)
    : _ioc(ioc),
      _acceptor(net::make_strand(ioc)),
      _controller(controller)
{
    _acceptor.open(endpoint.protocol());
    _acceptor.set_option(net::socket_base::reuse_address(true));
    _acceptor.bind(endpoint);
    _acceptor.listen();
}

void HookListener::start()
{
    do_accept();
}

void HookListener::stop()
{
    beast::error_code ec;

    [[maybe_unused]] auto& _ = (_acceptor.close(ec), ec);

    if (ec)
    {
        LOG_WARN("Acceptor close status: " + ec.message());
    }
}

void HookListener::do_accept()
{
    _acceptor.async_accept(net::make_strand(_ioc),
                           [self=shared_from_this()](const beast::error_code& ec, tcp::socket socket)
                           {
                               if (ec == net::error::operation_aborted)return;

                               if (!ec)
                               {
                                   try
                                   {
                                       std::make_shared<HookSession>(std::move(socket), self->_controller)->start();
                                   }
                                   catch (const std::exception& e)
                                   {
                                       LOG_ERROR("Session creation failed: " + std::string(e.what()));
                                   }
                               }
                               if (self->_acceptor.is_open()) self->do_accept();
                           });
}

//HookServer
HookServer::HookServer(Config config, HookController& controller)
    : _config(std::move(config)),
      _controller(controller)
{
}

HookServer::~HookServer()
{
    stop();
}

bool HookServer::start()
{
    bool expected = false;

    if (!_running.compare_exchange_strong(expected, true))
    {
        LOG_WARN("HookServer: Attempted to start an already running server.");
        return true;
    }

    try
    {
        tcp::endpoint ep(net::ip::make_address(_config.address), _config.port);
        _listener = std::make_shared<HookListener>(_ioc, ep, _controller);

        _listener->start();

        _work_guard.emplace(net::make_work_guard(_ioc));
        for (int i = 0; i < _config.io_threads; ++i)
        {
            _worker_threads.emplace_back([this]
            {
                _ioc.run();
            });
        }

        LOG_INFO("HookServer: started successfully on port " + std::to_string(_config.port));
        return true;
    }
    catch (const std::exception& e)
    {
        _running.store(false, std::memory_order_relaxed);
        LOG_ERROR("HookServer: failed to start: " + std::string(e.what()));

        return false;
    }
}

void HookServer::stop()
{
    if (!_running.exchange(false))
    {
        return;
    }

    LOG_INFO("HookServer: Shutdown initiated...");

    if (_listener)
    {
        _listener->stop();
        _listener.reset();
        LOG_INFO("HookServer: Listener stopped and reset.");
    }

    _work_guard.reset();
    LOG_INFO("HookServer: I/O work guard released.");

    size_t thread_count = _worker_threads.size();
    LOG_INFO("HookServer: Joining " + std::to_string(thread_count) + " worker threads...");

    for (auto& t : _worker_threads)
    {
        if (t.joinable())
        {
            t.join();
        }
    }

    _worker_threads.clear();

    LOG_INFO("HookServer: All worker threads joined. Shutdown complete.");
}
