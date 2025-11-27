//
// Created by X on 2025/11/16.
//
#include "HookServer.h"
#include <iostream>
#include <thread>
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
        std::cerr << "Read error: " << ec.message() << std::endl;
        return;
    }

    handle_request();
}

void HookSession::handle_request()
{
    using response_type = http::response<http::string_body>;
    response_type res;

    if (request_.method() == http::verb::post && request_.target() == "/hook")
    {
        res.result(http::status::ok);
        res.set(http::field::server, "StreamGate-Beast");
        res.set(http::field::content_type, "application/json");
        res.body() = R"({"code":0,"msg":"success"})";
    }
    else
    {
        res.result(http::status::not_found);
        res.set(http::field::server, "StreamGate-Beast");
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
        std::cerr << "Write error: " << ec.message() << std::endl;
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
            std::cerr << "Shutdown error: " << shutdown_ec.message() << std::endl;
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
    std::cout << "Server listening on port " << acceptor_.local_endpoint().port() << std::endl;

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
                std::cerr << "Accept error: " << ec.message() << std::endl;
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
    std::cout << "--- StreamGate Server Initialization ---" << std::endl;

    try
    {
        ConfigLoader::instance().load(".env");

        std::cout << "Initialization complete." << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FATAL Initialization Error: " << e.what() << std::endl;
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

    std::cout << "Starting IO service on " << address << ":" << port << " with " << num_threads << " threads..." <<
        std::endl;
    start_service();

    for (auto& t : threads)
    {
        if (t.joinable())
        {
            t.join();
        }
    }

    std::cout << "--- StreamGate Server shutdown complete ---" << std::endl;
}