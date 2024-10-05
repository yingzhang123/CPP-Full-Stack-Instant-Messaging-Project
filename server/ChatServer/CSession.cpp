#include "CSession.h"
#include "CServer.h"
#include <iostream>
#include <sstream>
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>
#include "LogicSystem.h"

// CSession 构造函数，初始化TCP socket、服务器指针、会话标识和接收消息头
CSession::CSession(boost::asio::io_context& io_context, CServer* server)
	: _socket(io_context), _server(server), _b_close(false), _b_head_parse(false), _user_uid(0) {
    // 生成唯一的会话ID
	boost::uuids::uuid a_uuid = boost::uuids::random_generator()();
	_session_id = boost::uuids::to_string(a_uuid);
	_recv_head_node = make_shared<MsgNode>(HEAD_TOTAL_LEN);    // HEAD_TOTAL_LEN = 4
}

CSession::~CSession() {
	std::cout << "~CSession destruct" << endl;
}

tcp::socket& CSession::GetSocket() {
	return _socket;
}

std::string& CSession::GetSessionId() {
	return _session_id;
}

void CSession::SetUserId(int uid)
{
	_user_uid = uid;
}

int CSession::GetUserId()
{
	return _user_uid;
}

// 开始会话，首先读取消息头部
void CSession::Start() {
	AsyncReadHead(HEAD_TOTAL_LEN);        // HEAD_TOTAL_LEN = 4 （消息ID(2) + 消息长度（2））
}

void CSession::Send(std::string msg, short msgid) {
    // 使用std::lock_guard自动管理互斥锁，确保线程安全
    std::lock_guard<std::mutex> lock(_send_lock);

    // 获取当前发送队列的大小
    int send_que_size = _send_que.size();

    // 检查发送队列是否已满
    if (send_que_size > MAX_SENDQUE) {
        // 如果队列已满，打印调试信息并返回
        std::cout << "session: " << _session_id << " send que fulled, size is " << MAX_SENDQUE << endl;
        return;
    }

    // 创建一个新的SendNode对象，并将其推入发送队列
    _send_que.push(make_shared<SendNode>(msg.c_str(), msg.length(), msgid));

    // 如果发送队列中已经有其他消息在等待发送，则直接返回
    if (send_que_size > 0) {
        return;
    }

    // 获取发送队列中的第一个消息节点
    auto& msgnode = _send_que.front();

    // 异步写入数据到socket
    boost::asio::async_write(
        _socket,  // 目标socket
        boost::asio::buffer(msgnode->_data, msgnode->_total_len),  // 要发送的数据缓冲区
        std::bind(&CSession::HandleWrite, this, std::placeholders::_1, SharedSelf())  // 完成处理回调
    );
}

void CSession::Send(char* msg, short max_length, short msgid) {
	std::lock_guard<std::mutex> lock(_send_lock);
	int send_que_size = _send_que.size();
	if (send_que_size > MAX_SENDQUE) {
		std::cout << "session: " << _session_id << " send que fulled, size is " << MAX_SENDQUE << endl;
		return;
	}

	_send_que.push(make_shared<SendNode>(msg, max_length, msgid));
	if (send_que_size>0) {
		return;
	}
	auto& msgnode = _send_que.front();
	boost::asio::async_write(_socket, boost::asio::buffer(msgnode->_data, msgnode->_total_len), 
		std::bind(&CSession::HandleWrite, this, std::placeholders::_1, SharedSelf()));
}

void CSession::Close() {
	_socket.close();
	_b_close = true;
}

std::shared_ptr<CSession>CSession::SharedSelf() {
	return shared_from_this();
}

// 实现了异步读取消息体的功能，并在读取完成后将消息投递到逻辑系统中进行处理。整个过程也是基于回调函数进行异步操作处理
void CSession::AsyncReadBody(int total_len)
{
	 // 保持当前会话的共享指针，以避免在异步操作时对象被销毁
	auto self = shared_from_this();
	// 异步读取消息体，读取总字节数为 total_len
	asyncReadFull(total_len, [self, this, total_len](const boost::system::error_code& ec, std::size_t bytes_transfered) {
		try {
			if (ec) {
				std::cout << "handle read failed, error is " << ec.what() << endl;
				Close();
				_server->ClearSession(_session_id);
				return;
			}

            // 检查读取的字节数是否与预期的消息体长度一致
            if (bytes_transfered < total_len) {
                std::cout << "read length not match, read [" << bytes_transfered << "] , total ["
                    << total_len << "]" << endl;
                // 如果读取字节数小于预期，关闭会话并清理
                Close();
                _server->ClearSession(_session_id);
                return;
            }

            // 将读取到的消息体数据从临时缓冲区 _data 拷贝到 _recv_msg_node 的 _data 中
            memcpy(_recv_msg_node->_data, _data, bytes_transfered);
            // 更新 _recv_msg_node 的当前长度，增加本次读取的字节数
            _recv_msg_node->_cur_len += bytes_transfered;
            // 确保数据的最后一位是结束符 '\0'，保证消息字符串的合法性
            _recv_msg_node->_data[_recv_msg_node->_total_len] = '\0';
            // 打印接收到的消息数据
            cout << "receive data is " << _recv_msg_node->_data << endl;
			
			//此处将消息投递到逻辑队列中
			LogicSystem::GetInstance()->PostMsgToQue(make_shared<LogicNode>(shared_from_this(), _recv_msg_node));
			//继续监听头部接受事件
			AsyncReadHead(HEAD_TOTAL_LEN);
		}
		catch (std::exception& e) {
			std::cout << "Exception code is " << e.what() << endl;
		}
		});
}

void CSession::AsyncReadHead(int total_len)
{
	// 保持当前会话的共享指针，以避免在异步操作时会话对象被销毁
	auto self = shared_from_this();
	// 异步读取完整的消息头（HEAD_TOTAL_LEN 字节），并通过回调函数处理读取结果      asyncReadFull注册回调
	asyncReadFull(HEAD_TOTAL_LEN, [self, this](const boost::system::error_code& ec, std::size_t bytes_transfered) {
		try {
			 // 处理读取操作的错误
			if (ec) {
				std::cout << "handle read failed, error is " << ec.what() << endl;
				// 关闭当前会话并清理
				Close();
				_server->ClearSession(_session_id);
				return;
			}

			// 检查读取到的字节数是否与预期的消息头长度一致
			if (bytes_transfered < HEAD_TOTAL_LEN) {
				std::cout << "read length not match, read [" << bytes_transfered << "] , total ["
					<< HEAD_TOTAL_LEN << "]" << endl;
				// 如果读取的字节数小于预期，关闭会话并清理资源
				Close();
				_server->ClearSession(_session_id);
				return;
			}

			// 清空接收缓冲区中的旧数据
			_recv_head_node->Clear();

			// 将读取到的字节数据从 _data（临时缓冲区）复制到 _recv_head_node->_data 中，
			// 这是专门用于存储消息头部数据的缓冲区
			memcpy(_recv_head_node->_data, _data, bytes_transfered);


		// 解析消息头中的 MSGID（消息标识符）
			short msg_id = 0;
			// 从接收缓冲区中复制消息 ID 数据
			memcpy(&msg_id, _recv_head_node->_data, HEAD_ID_LEN);

			// 将网络字节序的 msg_id 转换为主机字节序（不同的系统可能采用不同的字节序表示法）
			msg_id = boost::asio::detail::socket_ops::network_to_host_short(msg_id);
			std::cout << "msg_id is " << msg_id << endl;

			// 检查消息 ID 是否有效，如果 ID 超过最大允许值，则认为是非法 ID
			if (msg_id > MAX_LENGTH) {
				std::cout << "invalid msg_id is " << msg_id << endl;
				// 清理会话，关闭连接
				_server->ClearSession(_session_id);
				return;
			}

		// 解析消息头中的消息长度（msg_len）
			short msg_len = 0;
			// 从接收缓冲区中复制消息长度数据
			memcpy(&msg_len, _recv_head_node->_data + HEAD_ID_LEN, HEAD_DATA_LEN);

			// 将网络字节序的 msg_len 转换为主机字节序
			msg_len = boost::asio::detail::socket_ops::network_to_host_short(msg_len);
			std::cout << "msg_len is " << msg_len << endl;

			// 检查消息长度是否有效，如果长度超出允许的最大长度，则认为是非法长度
			if (msg_len > MAX_LENGTH) {
				std::cout << "invalid data length is " << msg_len << endl;
				// 清理会话，关闭连接
				_server->ClearSession(_session_id);
				return;
			}

			// 创建一个接收消息节点，用于存储接收的消息数据（包括消息体部分）
			_recv_msg_node = make_shared<RecvNode>(msg_len, msg_id);
			// 调用 AsyncReadBody 函数，开始异步读取消息体部分
			AsyncReadBody(msg_len);
		}
		catch (std::exception& e) {
			std::cout << "Exception code is " << e.what() << endl;
		}
		});
}

void CSession::HandleWrite(const boost::system::error_code& error, std::shared_ptr<CSession> shared_self) {
    // 增加异常处理
    try {
        // 检查是否有错误发生
        if (!error) {
            // 如果没有错误，使用std::lock_guard自动管理互斥锁，确保线程安全
            std::lock_guard<std::mutex> lock(_send_lock);

            // 从发送队列中移除已成功发送的消息节点
            _send_que.pop();

            // 检查发送队列是否还有其他消息需要发送
            if (!_send_que.empty()) {
                // 获取发送队列中的下一个消息节点
                auto& msgnode = _send_que.front();

                // 异步写入数据到socket
                boost::asio::async_write(
                    _socket,  // 目标socket
                    boost::asio::buffer(msgnode->_data, msgnode->_total_len),  // 要发送的数据缓冲区
                    std::bind(&CSession::HandleWrite, this, std::placeholders::_1, shared_self)  // 完成处理回调
                );
            }
        } else {
            // 如果有错误发生，打印错误信息并关闭会话
            std::cout << "handle write failed, error is " << error.what() << endl;

            // 关闭当前会话
            Close();

            // 从服务器中清除当前会话
            _server->ClearSession(_session_id);
        }
    } catch (std::exception& e) {
        // 捕获并处理标准异常
        std::cerr << "Exception code : " << e.what() << endl;
    }
}

//读取完整长度
void CSession::asyncReadFull(std::size_t maxLength, std::function<void(const boost::system::error_code&, std::size_t)> handler )
{
	// 清空缓冲区
	::memset(_data, 0, MAX_LENGTH);
	// 开始读取指定长度的数据 4个字节
	asyncReadLen(0, maxLength, handler);
}

//读取指定字节数
void CSession::asyncReadLen(std::size_t read_len, std::size_t total_len, std::function<void(const boost::system::error_code&, std::size_t)> handler)
{
	auto self = shared_from_this();
	// 异步读取数据并存储到 _data 缓冲区中
	_socket.async_read_some(boost::asio::buffer(_data + read_len, total_len-read_len),
		[read_len, total_len, handler, self](const boost::system::error_code& ec, std::size_t  bytesTransfered) {
			if (ec) {
				// 出现错误，调用回调函数
				handler(ec, read_len + bytesTransfered);
				return;
			}
			// 如果读取的字节数达到了预期的总长度，则调用回调函数处理数据	
			if (read_len + bytesTransfered >= total_len) {
				//长度够了就调用回调函数
				handler(ec, read_len + bytesTransfered);
				return;
			}

			// 没有错误，且长度不足则继续读取
			self->asyncReadLen(read_len + bytesTransfered, total_len, handler);
	});
}
/*
_data 是一个字符缓冲区，用于存储从网络 socket 中异步读取的字节数据。在每次读取操作时：

_data 会被清空，然后从 socket 中接收到的数据会被存储在这个缓冲区中。
在数据接收完毕后，_data 中的数据会被复制到消息节点（如 _recv_head_node 或 _recv_msg_node）中，供后续解析和处理。
*/

LogicNode::LogicNode(shared_ptr<CSession>  session, 
	shared_ptr<RecvNode> recvnode):_session(session),_recvnode(recvnode) {
	
}
