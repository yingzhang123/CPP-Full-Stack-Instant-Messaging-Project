#include "MsgNode.h"

// RecvNode 构造函数
// 初始化接收消息节点，继承自 MsgNode，设置最大长度，并记录消息 ID
RecvNode::RecvNode(short max_len, short msg_id)
    : MsgNode(max_len),    // 调用基类 MsgNode 的构造函数，初始化消息长度
    _msg_id(msg_id) {      // 初始化消息 ID
}

// SendNode 构造函数
// 初始化发送消息节点，继承自 MsgNode，设置最大长度和消息 ID
SendNode::SendNode(const char* msg, short max_len, short msg_id)
    : MsgNode(max_len + HEAD_TOTAL_LEN),  // 计算消息总长度（消息体加上消息头部）
    _msg_id(msg_id) {                     // 初始化消息 ID

    // 先将消息 ID 转换为网络字节序（大端格式），并复制到数据缓冲区中
    short msg_id_host = boost::asio::detail::socket_ops::host_to_network_short(msg_id);
    memcpy(_data, &msg_id_host, HEAD_ID_LEN);  // 将消息 ID 复制到消息数据的开头

    // 将消息长度（max_len）转换为网络字节序，并复制到数据缓冲区中
    short max_len_host = boost::asio::detail::socket_ops::host_to_network_short(max_len);
    memcpy(_data + HEAD_ID_LEN, &max_len_host, HEAD_DATA_LEN);  // 放入消息头部之后

    // 将实际的消息内容复制到数据缓冲区中（消息头部之后）
    memcpy(_data + HEAD_ID_LEN + HEAD_DATA_LEN, msg, max_len);
}
