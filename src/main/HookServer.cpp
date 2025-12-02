//
// Created by X on 2025/11/16.
//
#include "HookServer.h"
#include "AuthManager.h"
#include "Logger.h"
#include <iostream>
#include <thread>
#include <boost/json.hpp>
#include <boost/lexical_cast.hpp>

#include <iostream>
#include <string>

HookSession::HookSession(tcp::socket socket) : socket_(std::move(socket)), strand_(socket_.get_executor())
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

void HookSession::on_read(beast::error_code ec, std::size_t bytes_transferred)
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

    using response_type = http::response<http::string_body>;
    response_type res;

    res.set(http::field::server, "StreamGate-Beast");
    res.set(http::field::content_type, "application/json");

    if (request_.method() == http::verb::post && request_.target() == "/hook")
    {
        try
        {
            boost::json::value jv = boost::json::parse(request_.body());

            std::string streamName = jv.at("stream").as_string().c_str();

            std::string paramString = jv.at("param").as_string().c_str();

            std::string authToken = extract_param(paramString, "auth_token");
            std::string clientId = extract_param(paramString, "client_id");

            bool is_authenticated = AuthManager::instance().authenticate(streamName, clientId, authToken);

            LOG_INFO("Param string: "+paramString);
            LOG_INFO("Extracted auth_token: "+authToken);
            LOG_INFO("Extracted client_id: "+clientId);
            LOG_INFO("StreamName: " +streamName+ ", clientId: "+clientId);

            if (is_authenticated)
            {
                res.result(http::status::ok);
                res.body() = R"({"code":0,"msg":"success"})";
            }
            else
            {
                res.result(http::status::forbidden);
                res.body() = R"({"code":1,"msg":"authentication failed"})";
            }
        }
        catch (const boost::json::system_error& e)
        {
            LOG_ERROR("JSON Parsing Error (400): "+e.code().message());
            res.result(http::status::bad_request);
            res.body() = R"({"code":2,"msg":"invalid json format or syntax error"})";
        }
        catch (const std::out_of_range& e)
        {
            LOG_ERROR("JSON Key Missing Error (400): "+std::string(e.what()));
            res.result(http::status::bad_request);
            res.body() = R"({"code":3,"msg":"missing required key"})";
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Internal Server Error (500) during auth: "+std::string(e.what()));
            res.result(http::status::internal_server_error);
            res.body() = R"({"code":4,"msg":"internal server error"})";
        }
    }
    else
    {
        res.result(http::status::not_found);
        res.body() = "The requested resource was not found.";
    }

    res.prepare_payload();

    do_write(std::move(res));
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

void HookSession::on_write(bool keep_alive, beast::error_code ec, std::size_t bytes_transferred)
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


StreamGateListener::StreamGateListener(net::io_context& ioc, const tcp::endpoint& endpoint) : ioc_(ioc), acceptor_(ioc)
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
                std::make_shared<HookSession>(std::move(socket))->start();
            }
            self->do_accept();
        }
        );
}

void StreamGateServer::initialize()
{
    LOG_INFO("--- StreamGate Server Initialization ---");

    try
    {
        ConfigLoader::instance().load(".env");

        LOG_INFO("Initialization complete.");
    }
    catch (const std::exception& e)
    {
        LOG_FATAL("FATAL Initialization Error: " +std::string(e.what()));
        throw;
    }
}

void StreamGateServer::start_service()
{
    ioc_.run();
}

void StreamGateServer::run()
{
    initialize();

    int port = ConfigLoader::instance().getInt("HOOK_SERVER_PORT");
    std::string address = ConfigLoader::instance().getString("SERVER_ADDRESS");

    auto const listen_address = net::ip::make_address(address);
    tcp::endpoint endpoint{listen_address, static_cast<unsigned short>(port)};

    std::make_shared<StreamGateListener>(ioc_, endpoint)->run();

    int num_threads = ConfigLoader::instance().getInt("SERVER_MAX_THREADS", 4);
    std::vector<std::thread> threads;

    if (num_threads < 1)
        num_threads = 1;

    for (int i = 0; i < num_threads - 1; ++i)
    {
        threads.emplace_back([this]
        {
            start_service();
        });
    }

    LOG_INFO(
        "Starting IO service on "+address+":"+std::to_string(port)+" with "+std::to_string(num_threads)+ " threads...");
    start_service();

    for (auto& t : threads)
    {
        if (t.joinable())
        {
            t.join();
        }
    }

    LOG_INFO("--- StreamGate Server shutdown complete ---");
}


std::string HookSession::extract_param(const std::string& paramString, const std::string& key)
{
    std::string search_key = key + "=";
    size_t start_pos = paramString.find(search_key);

    if (start_pos == std::string::npos)
        return "";

    start_pos += search_key.length();

    size_t end_pos = paramString.find('&', start_pos);
    if (end_pos == std::string::npos)
        return paramString.substr(start_pos);

    return paramString.substr(start_pos, end_pos - start_pos);
}