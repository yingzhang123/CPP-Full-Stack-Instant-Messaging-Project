#include "CServer.h"
#include <iostream>
#include "AsioIOServicePool.h"
#include "UserMgr.h"

// 构造函数，初始化I/O上下文、监听端口以及TCP接收器，启动接受新连接的过程
CServer::CServer(boost::asio::io_context& io_context, short port)
    : _io_context(io_context), _port(port),
      _acceptor(io_context, tcp::endpoint(tcp::v4(), port))  // 初始化TCP接收器，绑定端口
{
    // 打印服务器启动成功的消息
    cout << "Server start success, listen on port : " << _port << endl;

    // 开始接受新连接
    StartAccept();
}

// 析构函数，打印服务器销毁时的消息
CServer::~CServer() {
    cout << "Server destruct listen on port : " << _port << endl;
}

// 处理新接受的连接请求
void CServer::HandleAccept(shared_ptr<CSession> new_session, const boost::system::error_code& error) {
    if (!error) {  // 如果没有错误
        new_session->Start();  // 启动新会话

        // 使用互斥锁保护会话列表的访问，确保线程安全
        lock_guard<mutex> lock(_mutex);

        // 将新会话添加到活跃会话映射表中，键为会话ID
        _sessions.insert(make_pair(new_session->GetSessionId(), new_session));
    } else {
        // 如果接收新连接时出现错误，打印错误消息
        cout << "Session accept failed, error is: " << error.message() << endl;
    }

    // 无论是否成功处理当前连接，都继续监听并接受新的连接
    StartAccept();
}


// 开始监听并接受新连接的函数
void CServer::StartAccept() {
    // 获取一个I/O上下文服务池中的I/O服务对象，用于处理异步I/O操作
    auto &io_context = AsioIOServicePool::GetInstance()->GetIOService();

    // 创建一个新的会话对象
    shared_ptr<CSession> new_session = make_shared<CSession>(io_context, this);

    // 异步接受新连接，成功后调用HandleAccept处理该连接
    _acceptor.async_accept(new_session->GetSocket(), 
        std::bind(&CServer::HandleAccept, this, new_session, placeholders::_1));
}


// 清除指定会话，根据会话ID移除会话
void CServer::ClearSession(std::string uuid) {
    // 检查会话是否存在
    if (_sessions.find(uuid) != _sessions.end()) {
        // 调用用户管理器移除用户和会话之间的关联
        UserMgr::GetInstance()->RmvUserSession(_sessions[uuid]->GetUserId());
    }

    // 使用互斥锁保护会话映射表的访问，确保线程安全
    {
        lock_guard<mutex> lock(_mutex);
        // 从会话映射表中移除指定会话
        _sessions.erase(uuid);
    }
}