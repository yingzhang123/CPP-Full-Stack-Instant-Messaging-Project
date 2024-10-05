#include "CServer.h"
#include <iostream>
#include "HttpConnection.h"
#include "AsioIOServicePool.h"

// 构造函数：初始化 CServer 对象，创建接受器（_acceptor）来监听指定端口上的连接请求
CServer::CServer(boost::asio::io_context& ioc, unsigned short& port) 
    : _ioc(ioc),  // 初始化 io_context，用于 I/O 操作
      _acceptor(ioc, tcp::endpoint(tcp::v4(), port))  // 初始化 acceptor，监听 TCP v4 连接
{
}

// 启动服务器，开始异步接受连接
void CServer::Start() {
    // 保留 this 指针的共享指针，确保捕获的 self 在异步操作中仍然有效   确保在异步操作过程中，CServer 对象不会被销毁。
    auto self = shared_from_this();

    // 从 IO 服务池中获取一个 io_context 实例，用于新连接的处理
    auto& io_context = AsioIOServicePool::GetInstance()->GetIOService();
    
    // 创建一个 HttpConnection 对象，用于处理新连接           创建一个新的 HttpConnection 对象来管理每个新连接。
    std::shared_ptr<HttpConnection> new_con = std::make_shared<HttpConnection>(io_context);

    // 异步接受新连接，等待客户端的连接请求                异步地等待新连接，接受后执行回调函数
    _acceptor.async_accept(new_con->GetSocket(),                                           //  指定接收连接的目标 socket，接受连接后会将客户端连接的 socket 赋给 HttpConnection 的 socket。
        [self, new_con](beast::error_code ec) {                                            // 处理连接请求的回调函数
            try {
                // 如果接收连接时发生错误，重新开始监听新的连接
                if (ec) {
                    self->Start();
                    return;
                }

                // 如果接收连接成功，开始处理新连接
                new_con->Start();
                
                // 继续监听新的连接请求，保持服务器持续运行
                self->Start();
            }
            catch (std::exception& exp) {
                // 捕获并处理异常，打印错误信息，并继续监听新连接
                std::cout << "exception is " << exp.what() << std::endl;
                self->Start();
            }
    });
}


