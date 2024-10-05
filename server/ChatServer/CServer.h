#pragma once
#include <boost/asio.hpp>
#include "CSession.h"
#include <memory.h>
#include <map>
#include <mutex>
using namespace std;
using boost::asio::ip::tcp;    // 使用Boost.Asio的TCP接口

// CServer类：管理服务器端的连接和会话
class CServer
{
public:
	// 构造函数，初始化I/O上下文和监听端口
	CServer(boost::asio::io_context& io_context, short port);
	
	// 析构函数，清理资源
	~CServer();

	// 清除指定的会话，通过会话的标识符（字符串）
	void ClearSession(std::string);

private:
	// 处理新的连接请求
	void HandleAccept(shared_ptr<CSession>, const boost::system::error_code & error);

	// 开始接受新的连接
	void StartAccept();

	// I/O 上下文引用，负责异步I/O操作的管理
	boost::asio::io_context &_io_context;
	
	// 服务器监听的端口号
	short _port;
	
	// TCP 接受器，用于监听并接受新的TCP连接
	tcp::acceptor _acceptor;
	
	// 存储所有活跃会话的映射表，键为会话标识符，值为会话指针
	std::map<std::string, shared_ptr<CSession>> _sessions;       
	
	// 互斥锁，确保对会话映射表的线程安全访问
	std::mutex _mutex;
};

/*
类说明：CServer类用于管理服务器端的TCP连接和会话，包含接受新连接、存储活跃会话以及清除会话的功能。
*/