//
// Created by X on 2025/11/16.
//

#ifndef STREAMGATE_HOOKSERVER_CPP_H
#define STREAMGATE_HOOKSERVER_CPP_H
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <memory>
#include <vector>
#include <string>
#include <atomic>
#include "HookController.h"

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = net::ip::tcp;

/**
 * @brief 处理 ZLMediaKit Webhook 的 HTTP 会话
 */
class HookSession : public std::enable_shared_from_this<HookSession>
{
public:
    explicit HookSession(tcp::socket socket, HookController& controller);

    HookSession(const HookSession&) = delete;
    HookSession& operator=(const HookSession&) = delete;

    void start();

private:
    //异步读取
    void do_read();

    void do_shutdown();

    void on_read(const beast::error_code& ec, std::size_t bytes_transferred);

    //处理请求，生成响应
    void handle_request();

    //异步写
    void send_response(int http_status, int business_code, const std::string& message);
    void on_write(bool keep_alive, const beast::error_code& ec, std::size_t bytes_transferred);

    tcp::socket _socket;
    net::strand<net::any_io_executor> _strand;
    beast::flat_buffer _buffer; //存储异步读取数据
    http::request<http::string_body> _request; //存储解析后的http请求
    http::response<http::string_body> _response; // 成员化复用

    HookController& _controller;
    std::atomic<bool> _responded{false};
};

/**
 * @brief 监听器：负责接受 TCP 连接
 */
class HookListener : public std::enable_shared_from_this<HookListener>
{
public:
    HookListener(
        net::io_context& ioc,
        const tcp::endpoint& endpoint,
        HookController& controller
    );

    void start();
    void stop();

private:
    void do_accept();

    net::io_context& _ioc;
    tcp::acceptor _acceptor;
    HookController& _controller;
};

/**
 * @brief Hook 服务主类：管理线程池与生命周期
 */
class HookServer
{
public:
    struct Config
    {
        std::string address = "0.0.0.0";
        int port = 8080;
        int io_threads = 2;
    };

    HookServer(Config config, HookController& controller);
    ~HookServer();

    bool start();
    void stop();

private:
    Config _config;
    HookController& _controller;
    net::io_context _ioc;
    std::optional<net::executor_work_guard<net::io_context::executor_type>> _work_guard;
    std::shared_ptr<HookListener> _listener;
    std::vector<std::thread> _worker_threads;
    std::atomic<bool> _running{false};
};
#endif //STREAMGATE_HOOKSERVER_CPP_H
