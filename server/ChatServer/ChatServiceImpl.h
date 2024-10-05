#pragma once
#include <grpcpp/grpcpp.h> // 引入gRPC库
#include "message.grpc.pb.h" // 引入gRPC生成的消息头文件
#include "message.pb.h" // 引入Protocol Buffers生成的消息头文件
#include <mutex> // 引入互斥锁库以实现线程安全
#include "data.h" // 引入自定义的数据结构和定义

// 使用gRPC相关命名空间
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

// 使用消息相关的命名空间
using message::AddFriendReq; // 引入添加好友请求消息
using message::AddFriendRsp; // 引入添加好友响应消息

using message::AuthFriendReq; // 引入好友认证请求消息
using message::AuthFriendRsp; // 引入好友认证响应消息

using message::ChatService; // 引入聊天服务接口
using message::TextChatMsgReq; // 引入文本聊天消息请求
using message::TextChatMsgRsp; // 引入文本聊天消息响应
using message::TextChatData; // 引入文本聊天数据结构

// 聊天服务实现类，继承自gRPC生成的ChatService服务接口
class ChatServiceImpl final: public ChatService::Service
{
public:
    ChatServiceImpl(); // 构造函数

    // 处理添加好友请求的方法
    Status NotifyAddFriend(ServerContext* context, const AddFriendReq* request,
                           AddFriendRsp* reply) override;

    // 处理好友认证请求的方法
    Status NotifyAuthFriend(ServerContext* context, 
                            const AuthFriendReq* request, AuthFriendRsp* response) override;

    // 处理文本聊天消息请求的方法
    Status NotifyTextChatMsg(::grpc::ServerContext* context, 
                             const TextChatMsgReq* request, TextChatMsgRsp* response) override;

    // 从数据库或其他存储中获取用户基本信息的方法
    bool GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo);

private:
    // 可以添加私有成员变量和辅助方法
};
