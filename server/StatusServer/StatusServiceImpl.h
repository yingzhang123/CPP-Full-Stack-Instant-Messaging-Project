#pragma once
#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include <mutex>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using message::GetChatServerReq;
using message::GetChatServerRsp;
using message::LoginReq;
using message::LoginRsp;
using message::StatusService;

// 定义一个类 ChatServer 用于存储聊天服务器的相关信息，如 host、port 和服务器的名称。
// 还有一个成员 con_count 用于统计连接数。
class ChatServer {
public:
    // 默认构造函数
    ChatServer() : host(""), port(""), name(""), con_count(0) {}

    // 拷贝构造函数，用于根据已有的 ChatServer 对象创建新的对象。
    ChatServer(const ChatServer& cs) : host(cs.host), port(cs.port), name(cs.name), con_count(cs.con_count) {}

    // 赋值操作符重载，支持对象间的赋值。
    ChatServer& operator=(const ChatServer& cs) {
        if (&cs == this) {
            return *this;
        }

        host = cs.host;
        name = cs.name;
        port = cs.port;
        con_count = cs.con_count;
        return *this;
    }

    // 成员变量，存储服务器的主机名、端口、名称和连接数。
    std::string host;
    std::string port;
    std::string name;
    int con_count;
};



// 继承自 gRPC 生成的 StatusService::Service，StatusServiceImpl 实现了两个 gRPC 服务的具体逻辑：
// 1. GetChatServer：用于获取适合用户的聊天服务器信息。
// 2. Login：用于处理用户登录逻辑。
class StatusServiceImpl final : public StatusService::Service
{
public:
    // 构造函数
    StatusServiceImpl();

    // GetChatServer：从 gRPC 请求中提取用户信息，返回适合的聊天服务器。            用于响应客户端请求，返回适合负载均衡的聊天服务器。此方法需要重载以符合 gRPC 生成的虚函数签名。
    Status GetChatServer(ServerContext* context, const GetChatServerReq* request,
                         GetChatServerRsp* reply) override;

    // Login：处理用户登录请求，返回登录响应。
    Status Login(ServerContext* context, const LoginReq* request,
                 LoginRsp* reply) override;

private:
    // insertToken：保存登录用户的 Token，用户标识用户的唯一身份。
    void insertToken(int uid, std::string token);

    // getChatServer：返回一个适合负载均衡的聊天服务器，基于 con_count 来决定。
    ChatServer getChatServer();

    // _servers：保存所有聊天服务器的信息，键为服务器的名称，值为对应的 ChatServer 对象。
    std::unordered_map<std::string, ChatServer> _servers;

    // _server_mtx：用于保护 _servers 的线程安全操作。
    std::mutex _server_mtx;
};

/*
StatusServiceImpl 类：

GetChatServer：用于响应客户端请求，返回适合负载均衡的聊天服务器。此方法需要重载以符合 gRPC 生成的虚函数签名。
Login：处理用户登录请求。登录后，服务器会生成并返回一个用户的身份验证 token。
insertToken：私有方法，存储用户的 token，帮助用户在登录后进行身份验证。
getChatServer：私有方法，用于返回一个适合用户的聊天服务器。选择依据可以是负载均衡算法，例如选择当前连接数最少的服务器。
*/