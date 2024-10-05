#pragma once
#include <string>
#include "const.h"
#include <iostream>
#include <boost/asio.hpp>
using namespace std;
using boost::asio::ip::tcp;
class LogicSystem;

// 消息节点类，表示消息数据的封装，用于发送和接收消息
class MsgNode {
public:
    // 构造函数，初始化消息节点并分配内存，最大长度为 max_len
    MsgNode(short max_len) : _total_len(max_len), _cur_len(0) {
        // 分配内存并初始化为零
        _data = new char[_total_len + 1]();
        _data[_total_len] = '\0';  // 确保字符串以 '\0' 结尾
    }

    // 析构函数，销毁消息节点并释放内存
    ~MsgNode() {
        std::cout << "destruct MsgNode" << endl;
        delete[] _data;
    }

    // 清空消息内容，将数据置零并重置当前长度
    void Clear() {
        ::memset(_data, 0, _total_len);
        _cur_len = 0;
    }

    short _cur_len;   // 当前消息长度
    short _total_len; // 消息的总长度
    char* _data;      // 指向消息内容的指针
};

// 接收消息节点类，继承自 MsgNode，用于表示接收到的消息
class RecvNode : public MsgNode {
    friend class LogicSystem;  // 声明 LogicSystem 为友元类，允许其访问私有成员
public:
    // 构造函数，初始化接收节点，指定消息长度和消息ID
    RecvNode(short max_len, short msg_id);

private:
    short _msg_id;  // 消息ID
};


// 发送消息节点类，继承自 MsgNode，用于表示要发送的消息
class SendNode : public MsgNode {
    friend class LogicSystem;  // 声明 LogicSystem 为友元类
public:
    // 构造函数，初始化发送节点，指定消息内容、消息长度和消息ID
    SendNode(const char* msg, short max_len, short msg_id);

private:
    short _msg_id;  // 消息ID
};

