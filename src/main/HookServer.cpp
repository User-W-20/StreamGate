//
// Created by X on 2025/11/16.
//
#include "HookServer.h"
#include "AuthManager.h"
#include "Logger.h"
#include <iostream>
#include <thread>
#include <boost/json.hpp>
#include <boost/system/error_code.hpp>
#include <boost/lexical_cast.hpp>

#include <iostream>
#include <string>

HookSession::HookSession(tcp::socket socket, AuthManager& authManager)
    : socket_(std::move(socket)),
      strand_(socket_.get_executor()),
      _authManager(authManager)
{
}

void HookSession::start()
{
    do_read();
}

void HookSession::do_read()
{
    request_ = {};

    http::async_read(
        socket_,
        buffer_,
        request_,
        beast::bind_front_handler(
            &HookSession::on_read,
            shared_from_this()
            )
        );
}

void HookSession::on_read(beast::error_code ec,[[maybe_unused]] std::size_t bytes_transferred)
{
    if (ec == http::error::end_of_stream)
    {
        return;
    }

    if (ec)
    {
        LOG_ERROR("Read error: "+ec.message());
        return;
    }

    handle_request();
}

void HookSession::handle_request()
{
    LOG_INFO("Hook received request: "+request_.body());

    if (request_.method() != http::verb::post || request_.target() != "/hook")
    {
        this->send_response(404, 999, "The requested resource was not found.");
        return;
    }

    HookParams params;

    try
    {
        LOG_INFO("Body size = " + std::to_string(request_.body().size()));
        LOG_INFO("Body = " + request_.body());

        boost::json::value jv = boost::json::parse(request_.body());

        auto& obj = jv.as_object();

        params.streamKey = boost::json::value_to<std::string>(obj.at("streamKey"));
        params.clientId = boost::json::value_to<std::string>(obj.at("clientId"));
        params.authToken = boost::json::value_to<std::string>(obj.at("authToken"));
    }
    catch (const boost::system::system_error& e)
    {
        LOG_ERROR("JSON Parsing Error (400): "+e.code().message());
        this->send_response(400, 2, "invalid json format or syntax error");
        return;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Parameter extraction failed (400): "+std::string(e.what()));
        this->send_response(400, 3, "missing required key or invalid param");
        return;
    }

    LOG_INFO("Extracted auth_token: "+params.authToken);
    LOG_INFO("Extracted client_id: "+params.clientId);
    LOG_INFO("StreamName: " +params.streamKey+ ", clientId: "+params.clientId);

    AuthCallback response_callback = [self = shared_from_this()](int final_auth_code)
    {
        int http_status = 0;
        int business_code = 0;
        std::string message;

        if (final_auth_code == AuthError::SUCCESS)
        {
            http_status = 200;
            business_code = 0;
            message = "success";
        }
        else if (final_auth_code == AuthError::AUTH_DENIED)
        {
            http_status = 403;
            business_code = 1;
            message = "authentication failed";
        }
        else if (final_auth_code == AuthError::RUNTIME_ERROR)
        {
            http_status = 500;
            business_code = 4;
            message = "internal server error during auth";
        }

        boost::asio::post(self->strand_,
                          [self,http_status,business_code,message]()
                          {
                              self->send_response(http_status, business_code, message);
                          });
    };


    _authManager.performCheckAsync(
        params,
        response_callback);
}


void HookSession::do_write(http::message_generator&& msg)
{
    bool keep_alive = msg.keep_alive();

    beast::async_write(
        socket_,
        std::move(msg),
        beast::bind_front_handler(
            &HookSession::on_write,
            shared_from_this(),
            keep_alive)
        );
}

void HookSession::on_write(bool keep_alive, beast::error_code ec,[[maybe_unused]] std::size_t bytes_transferred)
{
    if (ec)
    {
        LOG_ERROR("Write error: "+ec.message());
        return;
    }

    if (keep_alive)
    {
        do_read();
    }
    else
    {
        boost::system::error_code shutdown_ec;
        socket_.shutdown(tcp::socket::shutdown_send, shutdown_ec);
        if (shutdown_ec)
        {
            LOG_ERROR("Shutdown error: "+shutdown_ec.message());
        }
    }
}

StreamGateListener::StreamGateListener(net::io_context& ioc, const tcp::endpoint& endpoint, AuthManager& authManager)
    : ioc_(ioc),
      acceptor_(ioc),
      _authManager(authManager)
{
    boost::system::error_code ec;

    // 打开 acceptor
    acceptor_.open(endpoint.protocol(), ec);
    if (ec)
        throw boost::system::system_error(ec);

    //设置 SO_REUSEADDR 选项
    acceptor_.set_option(net::socket_base::reuse_address(true), ec);
    if (ec)
        throw boost::system::system_error(ec);

    // 绑定到地址和端口
    acceptor_.bind(endpoint, ec);
    if (ec)
        throw boost::system::system_error(ec);

    // 开始监听
    acceptor_.listen(net::socket_base::max_listen_connections, ec);
    if (ec)
        throw boost::system::system_error(ec);
}

void StreamGateListener::run()
{
    LOG_INFO("Server listening on port " +std::to_string(acceptor_.local_endpoint().port()));

    do_accept();
}

void StreamGateListener::do_accept()
{
    acceptor_.async_accept(
        net::make_strand(ioc_),
        [self=shared_from_this()](boost::system::error_code ec, tcp::socket socket)
        {
            if (! self->acceptor_.is_open())
                return;

            if (ec)
            {
                LOG_ERROR("Accept error: " +ec.message());
            }
            else
            {
                std::make_shared<HookSession>(std::move(socket), self->_authManager)->start();
            }
            self->do_accept();
        }
        );
}

StreamGateServer::StreamGateServer(const std::string& address, int port, int io_threads, AuthManager& authManager)
    : ioc_()
{
    LOG_INFO("Server: Initializing I/O context and Listener.");

    auto const endpoint = net::ip::tcp::endpoint{
        net::ip::make_address(address),
        static_cast<unsigned short>(port)
    };

    listener_ = std::make_shared<StreamGateListener>(ioc_, endpoint, authManager);

    listener_->run();

    start_service(io_threads);
}


void StreamGateServer::start_service(int io_threads)
{
    work_guard_.emplace(net::make_work_guard(ioc_));

    for (int i = 0; i < io_threads; ++i)
    {
        io_threads_pool_.emplace_back(
            [this]
            {
                ioc_.run();
            });
    }

    LOG_INFO("Server: Started "+std::to_string(io_threads)+ " I/O worker threads.");
}

void StreamGateServer::run()
{
    start_service(std::thread::hardware_concurrency());
    LOG_INFO("StreamGate Server is running. Press Ctrl+C to stop...");

    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset,SIGINT);
    sigaddset(&sigset,SIGTERM);
    int sig;
    sigwait(&sigset,&sig);


    LOG_INFO("Received shutdown signal, stopping server...");
    stop();
}

void HookSession::send_response(int http_status, int business_code, const std::string& message)
{
    using response_type = http::response<http::string_body>;
    response_type res;

    res.set(http::field::server, "StreamGate-Beast");
    res.set(http::field::content_type, "application/json");

    res.result(static_cast<http::status>(http_status));

    std::string json_body = "{\"code\":" + std::to_string(business_code) + ",\"msg\":\"" + message + "\"}";

    res.body() = json_body;

    res.prepare_payload();

    do_write(std::move(res));
}

StreamGateServer::~StreamGateServer()
{
    if (work_guard_.has_value())
    {
        work_guard_.reset();
    }

    ioc_.stop();

    for (auto& t : io_threads_pool_)
    {
        if (t.joinable())
        {
            t.join();
        }
    }
}

void StreamGateListener::stop()
{
    boost::system::error_code ec;
    if (acceptor_.is_open())
        acceptor_.close(ec);
}


void StreamGateServer::stop()
{
    if (listener_)
    {
        listener_->stop();
        listener_.reset();
    }

    work_guard_.reset();
    ioc_.stop();

    for (auto& th : io_threads_pool_)
    {
        if (th.joinable())
            th.join();
    }
    io_threads_pool_.clear();

    std::cout << "[INFO] StreamGate Server I/O threads finished." << std::endl;
}