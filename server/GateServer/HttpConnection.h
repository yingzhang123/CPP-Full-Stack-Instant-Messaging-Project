#pragma once
#include "const.h"

// 继承了 std::enable_shared_from_this，这意味着可以通过 shared_ptr 来管理自身的生命周期。
// HttpConnection 类是一个负责处理单个 HTTP 连接的类，它管理从客户端接收请求、解析请求参数、处理请求并生成响应的整个过程。
class HttpConnection: public std::enable_shared_from_this<HttpConnection>
{
    // LogicSystem 类可以访问 HttpConnection 的私有成员和函数
    friend class LogicSystem;
    
public:
    // 构造函数：初始化 HttpConnection 对象，使用 boost::asio::io_context 处理异步 I/O 操作
    HttpConnection(boost::asio::io_context& ioc);
    
    // 启动连接，开始接收客户端的 HTTP 请求
    void Start();
    
    // 解析 GET 请求的 URL 参数
    void PreParseGetParam();
    
    // 返回与此连接关联的 socket，用于与客户端进行 I/O 操作
    tcp::socket& GetSocket() {
        return _socket;
    }
    
private:
    // 检查连接的超时情况，如果超时则关闭连接
    void CheckDeadline();
    
    // 向客户端发送 HTTP 响应
    void WriteResponse();
    
    // 处理客户端发送的 HTTP 请求
    void HandleReq();

    // 用于管理与客户端的 socket 连接，处理 I/O 操作
    tcp::socket _socket;

    // 用于读取数据的缓冲区，大小为 8192 字节
    beast::flat_buffer _buffer{ 8192 };

    // 用于存储客户端发来的 HTTP 请求消息
    http::request<http::dynamic_body> _request;

    // 用于存储发送给客户端的 HTTP 响应消息
    http::response<http::dynamic_body> _response;

    // 定时器，用于检测连接是否超时。超时时间为 60 秒
    net::steady_timer deadline_{
        _socket.get_executor(), std::chrono::seconds(60)
    };

    // 存储 GET 请求的 URL
    std::string _get_url;

    // 用于存储 GET 请求的查询参数，使用键值对的方式存储
    std::unordered_map<std::string, std::string> _get_params;
};

