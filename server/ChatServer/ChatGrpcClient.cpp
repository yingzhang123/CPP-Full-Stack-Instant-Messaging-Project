#include "ChatGrpcClient.h"
#include "RedisMgr.h"
#include "ConfigMgr.h"
#include "UserMgr.h"

#include "CSession.h"
#include "MysqlMgr.h"

ChatGrpcClient::ChatGrpcClient()
{
	auto& cfg = ConfigMgr::Inst();
	auto server_list = cfg["PeerServer"]["Servers"];

	std::vector<std::string> words;

	std::stringstream ss(server_list);
	std::string word;

	while (std::getline(ss, word, ',')) {
		words.push_back(word);
	}

	for (auto& word : words) {
		if (cfg[word]["Name"].empty()) {
			continue;
		}
		_pools[cfg[word]["Name"]] = std::make_unique<ChatConPool>(5, cfg[word]["Host"], cfg[word]["Port"]);
	}
}

// 函数功能：向指定的服务器发送 "添加好友" 的 gRPC 请求，并返回响应结果。
// 输入参数：
//   - server_ip: 目标服务器的 IP 地址。
//   - req: 添加好友的请求消息，包含申请用户 ID 和目标用户 ID 等信息。
// 输出：返回 AddFriendRsp 对象，表示服务器的响应结果。
AddFriendRsp ChatGrpcClient::NotifyAddFriend(std::string server_ip, const AddFriendReq& req)
{
    // 初始化响应对象 rsp，并使用 Defer 类确保在函数结束时设置默认的成功状态和请求的用户 ID 信息。
    AddFriendRsp rsp;
    Defer defer([&rsp, &req]() {
        rsp.set_error(ErrorCodes::Success);   // 设置默认成功状态
        rsp.set_applyuid(req.applyuid());     // 设置申请用户 ID
        rsp.set_touid(req.touid());           // 设置目标用户 ID
    });

    // 查找服务器连接池，找到对应的连接池对象。
    auto find_iter = _pools.find(server_ip);
    if (find_iter == _pools.end()) {
        // 如果找不到对应的服务器 IP，则直接返回当前 rsp，保持默认的成功状态。
        return rsp;
    }

    // 获取找到的服务器连接池对象
    auto &pool = find_iter->second;

    // 创建 gRPC 上下文对象，用于维护请求状态。
    ClientContext context;

    // 从连接池中获取 gRPC 客户端的连接对象
    auto stub = pool->getConnection();

    // 发送 "添加好友" 的 gRPC 请求，传入上下文、请求对象 req 和响应对象 rsp。
    Status status = stub->NotifyAddFriend(&context, req, &rsp);

    // 使用 Defer 确保函数结束时将连接返回到连接池中。
    Defer defercon([&stub, this, &pool]() {
        pool->returnConnection(std::move(stub));  // 将连接归还到连接池
    });

    // 检查 gRPC 请求的状态
    if (!status.ok()) {
        // 如果请求失败，则将错误码设置为 RPC 失败
        rsp.set_error(ErrorCodes::RPCFailed);
        return rsp;
    }

    // 请求成功，返回响应结果
    return rsp;
}



bool ChatGrpcClient::GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo)
{
	//优先查redis中查询用户信息
	std::string info_str = "";
	bool b_base = RedisMgr::GetInstance()->Get(base_key, info_str);
	if (b_base) {
		Json::Reader reader;
		Json::Value root;
		reader.parse(info_str, root);
		userinfo->uid = root["uid"].asInt();
		userinfo->name = root["name"].asString();
		userinfo->pwd = root["pwd"].asString();
		userinfo->email = root["email"].asString();
		userinfo->nick = root["nick"].asString();
		userinfo->desc = root["desc"].asString();
		userinfo->sex = root["sex"].asInt();
		userinfo->icon = root["icon"].asString();
		std::cout << "user login uid is  " << userinfo->uid << " name  is "
			<< userinfo->name << " pwd is " << userinfo->pwd << " email is " << userinfo->email << endl;
	}
	else {
		//redis中没有则查询mysql
		//查询数据库
		std::shared_ptr<UserInfo> user_info = nullptr;
		user_info = MysqlMgr::GetInstance()->GetUser(uid);
		if (user_info == nullptr) {
			return false;
		}

		userinfo = user_info;

		//将数据库内容写入redis缓存
		Json::Value redis_root;
		redis_root["uid"] = uid;
		redis_root["pwd"] = userinfo->pwd;
		redis_root["name"] = userinfo->name;
		redis_root["email"] = userinfo->email;
		redis_root["nick"] = userinfo->nick;
		redis_root["desc"] = userinfo->desc;
		redis_root["sex"] = userinfo->sex;
		redis_root["icon"] = userinfo->icon;
		RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString());
	}

}

AuthFriendRsp ChatGrpcClient::NotifyAuthFriend(std::string server_ip, const AuthFriendReq& req) {
	AuthFriendRsp rsp;
	rsp.set_error(ErrorCodes::Success);

	Defer defer([&rsp, &req]() {
		rsp.set_fromuid(req.fromuid());
		rsp.set_touid(req.touid());
		});

	auto find_iter = _pools.find(server_ip);
	if (find_iter == _pools.end()) {
		return rsp;
	}

	auto& pool = find_iter->second;
	ClientContext context;
	auto stub = pool->getConnection();
	Status status = stub->NotifyAuthFriend(&context, req, &rsp);
	Defer defercon([&stub, this, &pool]() {
		pool->returnConnection(std::move(stub));
		});

	if (!status.ok()) {
		rsp.set_error(ErrorCodes::RPCFailed);
		return rsp;
	}

	return rsp;
}

TextChatMsgRsp ChatGrpcClient::NotifyTextChatMsg(std::string server_ip, const TextChatMsgReq& req, const Json::Value& rtvalue) {
    // 创建响应对象并设置初始错误码为成功
    TextChatMsgRsp rsp;
    rsp.set_error(ErrorCodes::Success);

    // 使用 Defer 模式确保在函数结束前填充响应内容
    Defer defer([&rsp, &req]() {
        rsp.set_fromuid(req.fromuid()); // 设置发送者 UID
        rsp.set_touid(req.touid());     // 设置接收者 UID
        
        // 遍历请求中的消息并将其添加到响应中
        for (const auto& text_data : req.textmsgs()) {
            TextChatData* new_msg = rsp.add_textmsgs(); // 添加新消息
            new_msg->set_msgid(text_data.msgid());      // 设置消息 ID
            new_msg->set_msgcontent(text_data.msgcontent()); // 设置消息内容
        }
    });

    // 在连接池中查找指定的服务器 IP
    auto find_iter = _pools.find(server_ip);
    if (find_iter == _pools.end()) {
        // 如果没有找到对应的连接池，返回当前响应
        return rsp;
    }

    // 获取连接池中的连接
    auto& pool = find_iter->second;
    ClientContext context; // 创建 gRPC 客户端上下文
    auto stub = pool->getConnection(); // 获取连接

    // 发送 gRPC 请求并获取响应状态
    Status status = stub->NotifyTextChatMsg(&context, req, &rsp);

    // 使用 Defer 模式确保在函数结束前归还连接
    Defer defercon([&stub, this, &pool]() {
        pool->returnConnection(std::move(stub)); // 归还连接到连接池
    });

    // 检查 gRPC 请求是否成功
    if (!status.ok()) {
        rsp.set_error(ErrorCodes::RPCFailed); // 设置错误码为 RPC 失败
        return rsp; // 返回响应
    }

    return rsp; // 返回成功的响应
}
// 该函数的实现旨在通过 gRPC 向指定的服务器发送文本聊天消息，并确保在处理完请求后正确返回响应。