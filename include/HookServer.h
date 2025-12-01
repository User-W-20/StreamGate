//
// Created by X on 2025/11/16.
//

#ifndef STREAMGATE_HOOKSERVER_CPP_H
#define STREAMGATE_HOOKSERVER_CPP_H
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <vector>
#include <string>

#include "AuthManager.h"
#include "CacheManager.h"
#include "DBManager.h"
#include "ConfigLoader.h"

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = net::ip::tcp;

//请求处理类
class HookSession : public std::enable_shared_from_this<HookSession>
{
public:
    explicit HookSession(tcp::socket socket);

    void start();

private:
    //异步读取
    void do_read();

    void on_read(beast::error_code ec, std::size_t bytes_transferred);

    //处理请求，生成响应
    void handle_request();

    //异步写
    void do_write(http::message_generator&& msg);

    void on_write(bool keep_alive, beast::error_code ec, std::size_t bytes_transferred);
    tcp::socket socket_;
    net::strand<net::any_io_executor> strand_;

    beast::flat_buffer buffer_;                //存储异步读取数据
    http::request<http::string_body> request_; //存储解析后的http请求

    //std::string extract_token_from_param(const std::string& paramString);
    std::string extract_param(const std::string&paramString,const std::string &key);
};

//服务器监听类
class StreamGateListener : public std::enable_shared_from_this<StreamGateListener>
{
public:
    StreamGateListener(
        net::io_context& ioc,
        const tcp::endpoint& endpoint
        );

    void run();

private:
    void do_accept();

    net::io_context& ioc_;
    tcp::acceptor acceptor_;
};

//服务器应用入口类
class StreamGateServer
{
public:
    void run();

private:
    void initialize();

    void start_service();

    net::io_context ioc_;
};
#endif //STREAMGATE_HOOKSERVER_CPP_H