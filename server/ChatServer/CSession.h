#pragma once
#include <boost/asio.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <queue>
#include <mutex>
#include <memory>
#include "const.h"
#include "MsgNode.h"
using namespace std;


namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>


class CServer;  // 预声明CServer类
class LogicSystem;  // 预声明LogicSystem类

// CSession类：管理每个客户端会话
class CSession: public std::enable_shared_from_this<CSession>
{
public:
	CSession(boost::asio::io_context& io_context, CServer* server);
	~CSession();
	tcp::socket& GetSocket();
	std::string& GetSessionId();
	void SetUserId(int uid);
	int GetUserId();
	void Start();
	void Send(char* msg,  short max_length, short msgid);
	void Send(std::string msg, short msgid);
	void Close();
	// 返回共享的自身对象
	std::shared_ptr<CSession> SharedSelf();
	// 异步读取消息体
	void AsyncReadBody(int length);
	// 异步读取消息头
	void AsyncReadHead(int total_len);
private:
	// 异步读取完整消息
	void asyncReadFull(std::size_t maxLength, std::function<void(const boost::system::error_code& , std::size_t)> handler);
	// 异步读取指定长度的数据
	void asyncReadLen(std::size_t  read_len, std::size_t total_len,
		std::function<void(const boost::system::error_code&, std::size_t)> handler);
	
	// 处理写操作完成时的回调
	void HandleWrite(const boost::system::error_code& error, std::shared_ptr<CSession> shared_self);
	// TCP连接的socket对象
	tcp::socket _socket;
	// 会话的唯一标识符 (UUID)
	std::string _session_id;

	// 存储传输数据的缓冲区
	char _data[MAX_LENGTH];

	// 指向服务器对象的指针，用于与服务器交互
	CServer* _server;
	// 会话关闭标志
	bool _b_close;
	// 消息发送队列
	std::queue<shared_ptr<SendNode> > _send_que;
	// 发送队列的互斥锁，确保线程安全
	std::mutex _send_lock;

	// 接收的消息结构体指针
	std::shared_ptr<RecvNode> _recv_msg_node;
	// 标志是否正在解析消息头
	bool _b_head_parse;

	// 接收到的消息头部结构体
	std::shared_ptr<MsgNode> _recv_head_node;
	// 用户的唯一ID
	int _user_uid;
};


// LogicNode类：用于管理逻辑处理中的会话和接收节点
class LogicNode {
	friend class LogicSystem;  // LogicSystem类可以访问LogicNode的私有成员
public:
	// 构造函数，初始化会话和接收节点
	LogicNode(shared_ptr<CSession>, shared_ptr<RecvNode>);
private:
	// 存储会话对象
	shared_ptr<CSession> _session;

	// 存储接收消息节点对象
	shared_ptr<RecvNode> _recvnode;
};