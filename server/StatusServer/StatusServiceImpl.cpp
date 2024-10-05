#include "StatusServiceImpl.h"
#include "ConfigMgr.h"
#include "const.h"
#include "RedisMgr.h"
#include <climits>

// 生成一个全局唯一标识符 (UUID) 并将其转换为字符串。
std::string generate_unique_string() {
    // 创建UUID对象，使用随机生成器生成唯一的UUID
    boost::uuids::uuid uuid = boost::uuids::random_generator()();

    // 将UUID对象转换为字符串表示形式
    std::string unique_string = to_string(uuid);

    return unique_string; // 返回生成的唯一字符串
}

// gRPC方法：获取负载最小的聊天服务器信息
Status StatusServiceImpl::GetChatServer(ServerContext* context, const GetChatServerReq* request, GetChatServerRsp* reply)
{
    std::string prefix("llfc status server has received :  ");
    
    // 从服务器列表中获取当前负载最小的聊天服务器
    const auto& server = getChatServer();
    
    // 设置响应中的聊天服务器主机和端口信息
    reply->set_host(server.host);
    reply->set_port(server.port);
    
    // 设置成功状态码
    reply->set_error(ErrorCodes::Success);
    
    // 生成唯一的用户 token，并将其设置到响应中
    reply->set_token(generate_unique_string());
    
    // 将生成的 token 插入 Redis，记录该用户的 token
    insertToken(request->uid(), reply->token());
    
    // 返回成功状态
    return Status::OK;
}


// 构造函数：初始化聊天服务器信息
StatusServiceImpl::StatusServiceImpl()
{
    // 获取配置文件中的聊天服务器信息
    auto& cfg = ConfigMgr::Inst();
    
    // 从配置中获取聊天服务器的名称列表
    auto server_list = cfg["chatservers"]["Name"];

    std::vector<std::string> words;
    std::stringstream ss(server_list);
    std::string word;

    // 将聊天服务器名称字符串按逗号分隔，存入words向量
    while (std::getline(ss, word, ',')) {
        words.push_back(word);
    }

    // 遍历所有聊天服务器的名称，加载服务器的配置信息
    for (auto& word : words) {
        if (cfg[word]["Name"].empty()) {
            continue; // 如果服务器名称为空，则跳过该服务器
        }

        // 创建 ChatServer 对象并设置其主机名、端口和名称
        ChatServer server;
        server.port = cfg[word]["Port"];
        server.host = cfg[word]["Host"];
        server.name = cfg[word]["Name"];
        
        // 将服务器对象存入服务器映射表
        _servers[server.name] = server;
    }
}

// 从服务器列表中获取当前负载最小的聊天服务器
ChatServer StatusServiceImpl::getChatServer() {
    // 加锁保护服务器列表的访问，防止并发操作
    std::lock_guard<std::mutex> guard(_server_mtx);

    // 假设第一个服务器是负载最小的服务器
    auto minServer = _servers.begin()->second;
    
    // 从 Redis 中获取该服务器的连接数
    auto count_str = RedisMgr::GetInstance()->HGet(LOGIN_COUNT, minServer.name);   //联合字段
    
    if (count_str.empty()) {
        // 如果 Redis 中没有该服务器的连接数信息，默认设置为最大值
        minServer.con_count = INT_MAX;
    } else {
        // 将连接数字符串转换为整数
        minServer.con_count = std::stoi(count_str);
    }

    // 遍历服务器列表，寻找连接数最小的服务器
    for (auto& server : _servers) {
        if (server.second.name == minServer.name) {
            continue; // 跳过当前最小负载服务器
        }

        // 从 Redis 获取其他服务器的连接数
        auto count_str = RedisMgr::GetInstance()->HGet(LOGIN_COUNT, server.second.name);
        
        if (count_str.empty()) {
            // 如果连接数不存在，设置为最大值
            server.second.con_count = INT_MAX;
        } else {
            // 将连接数字符串转换为整数
            server.second.con_count = std::stoi(count_str);
        }

        // 如果当前服务器的连接数小于当前最小服务器的连接数，更新最小服务器
        if (server.second.con_count < minServer.con_count) {
            minServer = server.second;
        }
    }

    // 返回负载最小的服务器
    return minServer;
}


// gRPC方法：处理用户登录逻辑
Status StatusServiceImpl::Login(ServerContext* context, const LoginReq* request, LoginRsp* reply)
{
    // 获取请求中的用户ID和token
    auto uid = request->uid();
    auto token = request->token();

    // 构造用户 token 在 Redis 中的键
    std::string uid_str = std::to_string(uid);
    std::string token_key = USERTOKENPREFIX + uid_str;
    
    // 从 Redis 中获取该用户的 token
    std::string token_value = "";
    bool success = RedisMgr::GetInstance()->Get(token_key, token_value);
    
    if (!success) {
        // 如果 Redis 中不存在该用户的 token，则返回无效用户ID错误
        reply->set_error(ErrorCodes::UidInvalid);
        return Status::OK;
    }

    // 如果 token 不匹配，返回无效 token 错误
    if (token_value != token) {
        reply->set_error(ErrorCodes::TokenInvalid);
        return Status::OK;
    }

    // 如果 token 匹配，返回成功，并将用户ID和 token 返回
    reply->set_error(ErrorCodes::Success);
    reply->set_uid(uid);
    reply->set_token(token);
    return Status::OK;
}


// 将用户的 token 插入 Redis，记录登录状态
void StatusServiceImpl::insertToken(int uid, std::string token)
{
    // 构造用户 token 在 Redis 中的键
    std::string uid_str = std::to_string(uid);
    std::string token_key = USERTOKENPREFIX + uid_str;
    
    // 将 token 存储到 Redis 中
    RedisMgr::GetInstance()->Set(token_key, token);
}

