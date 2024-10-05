#include "LogicSystem.h"
#include "StatusGrpcClient.h"
#include "MysqlMgr.h"
#include "const.h"
#include "RedisMgr.h"
#include "UserMgr.h"
#include "ChatGrpcClient.h"

using namespace std;

// 构造函数
LogicSystem::LogicSystem() : _b_stop(false) { // 初始化停止标志为 false
    RegisterCallBacks(); // 注册消息处理的回调函数
    // 创建工作线程，用于处理消息
    _worker_thread = std::thread(&LogicSystem::DealMsg, this); 
}

// 析构函数
LogicSystem::~LogicSystem() {
    _b_stop = true; // 设置停止标志为 true，表示工作线程应停止
    _consume.notify_one(); // 通知条件变量，有新的状态变化
    _worker_thread.join(); // 等待工作线程完成
}

// 向消息队列中投递消息
void LogicSystem::PostMsgToQue(shared_ptr<LogicNode> msg) {
    std::unique_lock<std::mutex> unique_lk(_mutex); // 获取互斥锁
    _msg_que.push(msg); // 将消息放入消息队列
    // 如果队列从空变为非空，通知消费线程
    if (_msg_que.size() == 1) {
        unique_lk.unlock(); // 释放互斥锁
        _consume.notify_one(); // 通知条件变量
    }
}
/*
DealMsg 方法负责从消息队列中获取消息，并根据消息 ID 调用相应的回调函数进行处理。它通过条件变量和互斥锁实现了线程安全的消息处理机制，同时支持优雅地关闭工作线程，确保在系统关闭时处理完所有待处理消息。
*/
void LogicSystem::DealMsg() {
	for (;;) {
		std::unique_lock<std::mutex> unique_lk(_mutex);
		//判断队列为空则用条件变量阻塞等待，并释放锁
		while (_msg_que.empty() && !_b_stop) {
			_consume.wait(unique_lk);
		}

		//判断是否为关闭状态，把所有逻辑执行完后则退出循环
		if (_b_stop ) {
			while (!_msg_que.empty()) {
				auto msg_node = _msg_que.front();
				cout << "recv_msg id  is " << msg_node->_recvnode->_msg_id << endl;
				auto call_back_iter = _fun_callbacks.find(msg_node->_recvnode->_msg_id);
				if (call_back_iter == _fun_callbacks.end()) {
					_msg_que.pop();
					continue;
				}
				call_back_iter->second(msg_node->_session, msg_node->_recvnode->_msg_id,
					std::string(msg_node->_recvnode->_data, msg_node->_recvnode->_cur_len));
				_msg_que.pop();
			}
			break;
		}

		//如果没有停服，且说明队列中有数据
		auto msg_node = _msg_que.front();
		cout << "recv_msg id  is " << msg_node->_recvnode->_msg_id << endl;
		auto call_back_iter = _fun_callbacks.find(msg_node->_recvnode->_msg_id);
		if (call_back_iter == _fun_callbacks.end()) {
			_msg_que.pop();
			std::cout << "msg id [" << msg_node->_recvnode->_msg_id << "] handler not found" << std::endl;
			continue;
		}
		call_back_iter->second(msg_node->_session, msg_node->_recvnode->_msg_id, std::string(msg_node->_recvnode->_data, msg_node->_recvnode->_cur_len));
		_msg_que.pop();
	}
}

void LogicSystem::RegisterCallBacks() {
	_fun_callbacks[MSG_CHAT_LOGIN] = std::bind(&LogicSystem::LoginHandler, this,
		placeholders::_1, placeholders::_2, placeholders::_3);

	_fun_callbacks[ID_SEARCH_USER_REQ] = std::bind(&LogicSystem::SearchInfo, this,
		placeholders::_1, placeholders::_2, placeholders::_3);

	_fun_callbacks[ID_ADD_FRIEND_REQ] = std::bind(&LogicSystem::AddFriendApply, this,
		placeholders::_1, placeholders::_2, placeholders::_3);

	_fun_callbacks[ID_AUTH_FRIEND_REQ] = std::bind(&LogicSystem::AuthFriendApply, this,
		placeholders::_1, placeholders::_2, placeholders::_3);

	_fun_callbacks[ID_TEXT_CHAT_MSG_REQ] = std::bind(&LogicSystem::DealChatTextMsg, this,
		placeholders::_1, placeholders::_2, placeholders::_3);
	
}

void LogicSystem::LoginHandler(shared_ptr<CSession> session, const short &msg_id, const string &msg_data) {
    // 创建 JSON 解析器和 JSON 值对象
    Json::Reader reader;
    Json::Value root;
    // 解析传入的消息数据
    reader.parse(msg_data, root);

    // 获取用户 ID 和 Token
    auto uid = root["uid"].asInt(); // 从 JSON 中提取用户 ID
    auto token = root["token"].asString(); // 从 JSON 中提取 Token
    std::cout << "user login uid is  " << uid << " user token  is " << token << endl;

    Json::Value  rtvalue; // 用于存储返回结果
    // 使用 Defer 结构体确保最后发送结果给客户端
    Defer defer([this, &rtvalue, session]() {
        std::string return_str = rtvalue.toStyledString(); // 将 JSON 对象转为字符串
        session->Send(return_str, MSG_CHAT_LOGIN_RSP); // 发送登录响应
    });

    // 从 Redis 获取用户的 Token 是否正确
    std::string uid_str = std::to_string(uid); // 将用户 ID 转为字符串
    std::string token_key = USERTOKENPREFIX + uid_str; // 构建 Redis 中 Token 的键
    std::string token_value = ""; // 用于存储从 Redis 获取的 Token
    bool success = RedisMgr::GetInstance()->Get(token_key, token_value); // 获取 Token
    if (!success) {
        rtvalue["error"] = ErrorCodes::UidInvalid; // 如果获取失败，返回 UID 无效
        return;
    }

    // 检查 Token 是否与 Redis 中的值匹配
    if (token_value != token) {
        rtvalue["error"] = ErrorCodes::TokenInvalid; // 如果不匹配，返回 Token 无效
        return;
    }

    rtvalue["error"] = ErrorCodes::Success; // 登录成功，设置返回状态为成功

    // 从数据库获取用户基本信息
    std::string base_key = USER_BASE_INFO + uid_str; // 构建用户基本信息的键
    auto user_info = std::make_shared<UserInfo>(); // 创建用户信息对象
    bool b_base = GetBaseInfo(base_key, uid, user_info); // 获取用户基本信息
    if (!b_base) {
        rtvalue["error"] = ErrorCodes::UidInvalid; // 如果获取失败，返回 UID 无效
        return;
    }

    // 将用户基本信息添加到返回值中
    rtvalue["uid"] = uid; // 用户 ID
    rtvalue["pwd"] = user_info->pwd; // 用户密码
    rtvalue["name"] = user_info->name; // 用户名
    rtvalue["email"] = user_info->email; // 用户邮箱
    rtvalue["nick"] = user_info->nick; // 用户昵称
    rtvalue["desc"] = user_info->desc; // 用户描述
    rtvalue["sex"] = user_info->sex; // 用户性别
    rtvalue["icon"] = user_info->icon; // 用户头像

  // 从数据库获取好友申请列表
    std::vector<std::shared_ptr<ApplyInfo>> apply_list; // 存储申请信息的列表
    auto b_apply = GetFriendApplyInfo(uid, apply_list); // 获取好友申请信息
    if (b_apply) {
        for (auto &apply : apply_list) {
            Json::Value obj; // 创建 JSON 对象存储申请信息
            obj["name"] = apply->_name; // 申请者姓名
            obj["uid"] = apply->_uid; // 申请者用户 ID
            obj["icon"] = apply->_icon; // 申请者头像
            obj["nick"] = apply->_nick; // 申请者昵称
            obj["sex"] = apply->_sex; // 申请者性别
            obj["desc"] = apply->_desc; // 申请者描述
            obj["status"] = apply->_status; // 申请状态
            rtvalue["apply_list"].append(obj); // 将申请信息添加到返回值
        }
    }

    // 获取好友列表
    std::vector<std::shared_ptr<UserInfo>> friend_list; // 存储好友信息的列表
    bool b_friend_list = GetFriendList(uid, friend_list); // 获取好友列表
    for (auto &friend_ele : friend_list) {
        Json::Value obj; // 创建 JSON 对象存储好友信息
        obj["name"] = friend_ele->name; // 好友姓名
        obj["uid"] = friend_ele->uid; // 好友用户 ID
        obj["icon"] = friend_ele->icon; // 好友头像
        obj["nick"] = friend_ele->nick; // 好友昵称
        obj["sex"] = friend_ele->sex; // 好友性别
        obj["desc"] = friend_ele->desc; // 好友描述
        obj["back"] = friend_ele->back; // 好友背景信息
        rtvalue["friend_list"].append(obj); // 将好友信息添加到返回值
    }

    // 获取当前服务器名称
    auto server_name = ConfigMgr::Inst().GetValue("SelfServer", "Name");

    // 更新登录数量
    auto rd_res = RedisMgr::GetInstance()->HGet(LOGIN_COUNT, server_name); // 从 Redis 获取登录计数
    int count = 0;
    if (!rd_res.empty()) {
        count = std::stoi(rd_res); // 将登录计数转换为整数
    }

    count++; // 登录计数增加
    auto count_str = std::to_string(count); // 转换为字符串
    RedisMgr::GetInstance()->HSet(LOGIN_COUNT, server_name, count_str); // 更新 Redis 中的登录计数

    // 将用户 ID 绑定到当前会话中
    session->SetUserId(uid);
	
    // 为用户设置登录 IP 和服务器名称
    std::string ipkey = USERIPPREFIX + uid_str; // 构建用户 IP 的 Redis 键
    RedisMgr::GetInstance()->Set(ipkey, server_name); // 在 Redis 中设置用户 IP
	//uid和session绑定管理,方便以后踢人操作
    // 绑定用户 ID 和会话，以便将来可以踢掉用户
    UserMgr::GetInstance()->SetUserSession(uid, session);

	return; // 完成登录处理
}

void LogicSystem::SearchInfo(std::shared_ptr<CSession> session, const short& msg_id, const string& msg_data)
{
    Json::Reader reader; // 创建 JSON 解析器
    Json::Value root; // 创建 JSON 值对象
    reader.parse(msg_data, root); // 解析传入的 JSON 字符串数据
    auto uid_str = root["uid"].asString(); // 从 JSON 中获取 "uid" 字段的值
    std::cout << "user SearchInfo uid is  " << uid_str << endl; // 打印搜索的用户 ID

    Json::Value rtvalue; // 创建用于存储返回结果的 JSON 值对象

    // 使用 Defer 对象来确保在方法结束时自动发送结果
    Defer defer([this, &rtvalue, session]() {
        std::string return_str = rtvalue.toStyledString(); // 将结果转换为字符串
        session->Send(return_str, ID_SEARCH_USER_RSP); // 发送结果给客户端
    });

    // 检查 uid_str 是否是纯数字
    bool b_digit = isPureDigit(uid_str); // 判断 uid_str 是否为数字
    if (b_digit) { // 如果是数字，则通过 UID 查询用户信息
        GetUserByUid(uid_str, rtvalue); // 根据 UID 获取用户信息并填充到 rtvalue 中
    }
    else { // 如果不是数字，则通过用户名查询用户信息
        GetUserByName(uid_str, rtvalue); // 根据用户名获取用户信息并填充到 rtvalue 中
    }
    return; // 方法结束，结果会在 Defer 中自动发送
}

void LogicSystem::AddFriendApply(std::shared_ptr<CSession> session, const short& msg_id, const string& msg_data)
{
    // 创建Json解析器和根节点
    Json::Reader reader;
    Json::Value root;

    // 解析传入的消息数据
    if (!reader.parse(msg_data, root)) {
        // 如果解析失败，可以在这里处理错误，例如返回错误响应给客户端
        // 本例中假设总是能够成功解析
        return;
    }

 	// 从JSON中提取发送者UID、申请名称、备注名称以及接收者UID
    auto uid = root["uid"].asInt();         // 发送者的UID
    auto applyname = root["applyname"].asString();  // 申请时使用的名称
    auto bakname = root["bakname"].asString();      // 备注名称
    auto touid = root["touid"].asInt();             // 接收者的UID

    // 打印调试信息
    std::cout << "user login uid is  " << uid << " applyname  is " << applyname << " bakname is " << bakname << " touid is " << touid << endl;

    // 构建响应JSON对象
    Json::Value rtvalue;
    rtvalue["error"] = ErrorCodes::Success;  // 设置错误码为成功
	
    // 使用Defer模式确保在函数结束前向客户端发送响应
    Defer defer([this, &rtvalue, session]() {
        std::string return_str = rtvalue.toStyledString();  // 将JSON对象转换成格式化的字符串
        session->Send(return_str, ID_ADD_FRIEND_RSP);       // 向客户端发送响应
    });

    // 更新数据库，记录好友申请
    MysqlMgr::GetInstance()->AddFriendApply(uid, touid);

    // 查询Redis以查找接收者对应的服务器IP
    auto to_str = std::to_string(touid);
    auto to_ip_key = USERIPPREFIX + to_str;  // 组合成键
    std::string to_ip_value = "";            // 存储查询结果
    bool b_ip = RedisMgr::GetInstance()->Get(to_ip_key, to_ip_value);  // 执行查询
    if (!b_ip) {  // 如果没有找到对应的IP地址
        // 可以在这里添加错误处理逻辑，例如设置错误码并返回
        return;
    }

    // 获取当前服务器配置
    auto& cfg = ConfigMgr::Inst();
    auto self_name = cfg["SelfServer"]["Name"];  // 当前服务器名称


   // 从Redis中获取发送者的用户基本信息
    std::string base_key = USER_BASE_INFO + std::to_string(uid);
    auto apply_info = std::make_shared<UserInfo>();  // 创建UserInfo指针
    bool b_info = GetBaseInfo(base_key, uid, apply_info);  // 获取用户基本信息

    // 检查接收者是否在同一服务器上
    if (to_ip_value == self_name) {
        // 在同一服务器上直接获取会话并发送消息
        auto target_session = UserMgr::GetInstance()->GetSession(touid);
        if (target_session) {
            // 构建通知JSON对象
            Json::Value notify;
            notify["error"] = ErrorCodes::Success;
            notify["applyuid"] = uid;  // 申请者的UID
            notify["name"] = applyname;  // 申请时使用的名称
            notify["desc"] = "";  // 备注（这里为空）

            // 如果获取到用户基本信息，则填充相应字段
            if (b_info) {
                notify["icon"] = apply_info->icon;  // 用户头像
                notify["sex"] = apply_info->sex;    // 用户性别
                notify["nick"] = apply_info->nick;  // 用户昵称
            }

            // 转换成字符串并发送通知
            std::string return_str = notify.toStyledString();
            target_session->Send(return_str, ID_NOTIFY_ADD_FRIEND_REQ);
        }
        return;  // 直接返回，因为已经在本地处理完毕
    }

	
    // 构建gRPC请求对象
    AddFriendReq add_req;
    add_req.set_applyuid(uid);  // 设置申请者的UID
    add_req.set_touid(touid);   // 设置接收者的UID
    add_req.set_name(applyname);  // 设置申请时使用的名称
    add_req.set_desc("");        // 设置备注（这里为空）

    // 如果获取到用户基本信息，则填充相应字段   
    if (b_info) {
        add_req.set_icon(apply_info->icon);  // 用户头像
        add_req.set_sex(apply_info->sex);    // 用户性别
        add_req.set_nick(apply_info->nick);  // 用户昵称
    }

    // 通过gRPC客户端发送跨服务器的通知
    ChatGrpcClient::GetInstance()->NotifyAddFriend(to_ip_value, add_req);

}

void LogicSystem::AuthFriendApply(std::shared_ptr<CSession> session, const short& msg_id, const string& msg_data) {
	
    // 创建Json解析器和根节点
    Json::Reader reader;
    Json::Value root;

    // 解析传入的消息数据
    if (!reader.parse(msg_data, root)) {
        // 如果解析失败，可以在这里处理错误，例如返回错误响应给客户端
        // 本例中假设总是能够成功解析
        return;
    }
    // 从JSON中提取发送者UID、接收者UID以及备注名称
    auto uid = root["fromuid"].asInt();         // 发送者的UID
    auto touid = root["touid"].asInt();         // 接收者的UID
    auto back_name = root["back"].asString();   // 备注名称

    // 打印调试信息
    std::cout << "from " << uid << " auth friend to " << touid << std::endl;

    // 构建响应JSON对象
    Json::Value rtvalue;
    rtvalue["error"] = ErrorCodes::Success;  // 设置错误码为成功

    // 创建UserInfo指针以存储用户信息
    auto user_info = std::make_shared<UserInfo>();

    // 从Redis中获取接收者的用户基本信息
    std::string base_key = USER_BASE_INFO + std::to_string(touid);
    bool b_info = GetBaseInfo(base_key, touid, user_info);  // 获取用户基本信息
    if (b_info) {
        // 如果获取到用户基本信息，则填充相应字段
        rtvalue["name"] = user_info->name;  // 用户名
        rtvalue["nick"] = user_info->nick;  // 用户昵称
        rtvalue["icon"] = user_info->icon;  // 用户头像
        rtvalue["sex"] = user_info->sex;    // 用户性别
        rtvalue["uid"] = touid;             // 用户UID
    } else {
        // 如果没有找到用户信息，设置错误码
        rtvalue["error"] = ErrorCodes::UidInvalid;
    }


    // 使用Defer模式确保在函数结束前向客户端发送响应
    Defer defer([this, &rtvalue, session]() {
        std::string return_str = rtvalue.toStyledString();  // 将JSON对象转换成格式化的字符串
        session->Send(return_str, ID_AUTH_FRIEND_RSP);       // 向客户端发送响应
    });

    // 更新数据库，记录好友认证通过
    MysqlMgr::GetInstance()->AuthFriendApply(uid, touid);

    // 更新数据库，添加好友关系
    MysqlMgr::GetInstance()->AddFriend(uid, touid, back_name);

    // 查询Redis以查找接收者对应的服务器IP
    auto to_str = std::to_string(touid);
    auto to_ip_key = USERIPPREFIX + to_str;  // 组合成键
    std::string to_ip_value = "";            // 存储查询结果
    bool b_ip = RedisMgr::GetInstance()->Get(to_ip_key, to_ip_value);  // 执行查询
    if (!b_ip) {  // 如果没有找到对应的IP地址
        // 可以在这里添加错误处理逻辑，例如设置错误码并返回
        return;
    }

    // 获取当前服务器配置
    auto& cfg = ConfigMgr::Inst();
    auto self_name = cfg["SelfServer"]["Name"];  // 当前服务器名称

    // 检查接收者是否在同一服务器上
    if (to_ip_value == self_name) {
        // 在同一服务器上直接获取会话并发送消息
        auto target_session = UserMgr::GetInstance()->GetSession(touid);
        if (target_session) {
            // 构建通知JSON对象
            Json::Value notify;
            notify["error"] = ErrorCodes::Success;
            notify["fromuid"] = uid;  // 发送者的UID
            notify["touid"] = touid;   // 接收者的UID

            // 从Redis中获取发送者的用户基本信息
            std::string base_key = USER_BASE_INFO + std::to_string(uid);
            auto user_info = std::make_shared<UserInfo>();
            bool b_info = GetBaseInfo(base_key, uid, user_info);  // 获取用户基本信息
            if (b_info) {
                notify["name"] = user_info->name;  // 用户名
                notify["nick"] = user_info->nick;  // 用户昵称
                notify["icon"] = user_info->icon;  // 用户头像
                notify["sex"] = user_info->sex;    // 用户性别
            } else {
                notify["error"] = ErrorCodes::UidInvalid;
            }

            // 转换成字符串并发送通知
            std::string return_str = notify.toStyledString();
            target_session->Send(return_str, ID_NOTIFY_AUTH_FRIEND_REQ);
        }
        return;  // 直接返回，因为已经在本地处理完毕
    }


    // 构建gRPC请求对象
    AuthFriendReq auth_req;
    auth_req.set_fromuid(uid);  // 设置发送者的UID
    auth_req.set_touid(touid);  // 设置接收者的UID

    // 通过gRPC客户端发送跨服务器的通知
    ChatGrpcClient::GetInstance()->NotifyAuthFriend(to_ip_value, auth_req);
}

void LogicSystem::DealChatTextMsg(std::shared_ptr<CSession> session, const short& msg_id, const string& msg_data) {
    // 创建Json解析器和根节点
    Json::Reader reader;
    Json::Value root;

    // 解析传入的消息数据
    if (!reader.parse(msg_data, root)) {
        // 如果解析失败，可以在这里处理错误，例如返回错误响应给客户端
        // 本例中假设总是能够成功解析
        return;
    }

    // 从JSON中提取发送者UID、接收者UID以及消息数组
    auto uid = root["fromuid"].asInt();
    auto touid = root["touid"].asInt();
    const Json::Value arrays = root["text_array"];
	
    // 构建响应JSON对象
    Json::Value rtvalue;
    rtvalue["error"] = ErrorCodes::Success;  // 设置错误码为成功
    rtvalue["text_array"] = arrays;          // 将原始文本数组复制到响应中
    rtvalue["fromuid"] = uid;                // 发送者的UID
    rtvalue["touid"] = touid;                // 接收者的UID


    // 使用Defer模式确保在函数结束前向客户端发送响应
    Defer defer([this, &rtvalue, session]() {
        std::string return_str = rtvalue.toStyledString();  // 将JSON对象转换成格式化的字符串
        session->Send(return_str, ID_TEXT_CHAT_MSG_RSP);    // 向客户端发送响应
    });

    // 查询Redis以查找接收者对应的服务器IP
    auto to_str = std::to_string(touid);
    auto to_ip_key = USERIPPREFIX + to_str;  // 组合成键
    std::string to_ip_value = "";            // 存储查询结果
    bool b_ip = RedisMgr::GetInstance()->Get(to_ip_key, to_ip_value);  // 执行查询
    if (!b_ip) {  // 如果没有找到对应的IP地址
        // 可以在这里添加错误处理逻辑，例如设置错误码并返回
        return;
    }

    // 获取当前服务器配置
    auto& cfg = ConfigMgr::Inst();
    auto self_name = cfg["SelfServer"]["Name"];  // 当前服务器名称

    // 检查接收者是否在同一服务器上
    if (to_ip_value == self_name) {
        // 在同一服务器上直接获取会话并发送消息
        auto target_session = UserMgr::GetInstance()->GetSession(touid);
        if (target_session) {
            std::string return_str = rtvalue.toStyledString();  // 转换成字符串
            target_session->Send(return_str, ID_NOTIFY_TEXT_CHAT_MSG_REQ);  // 发送通知
        }
        return;  // 直接返回，因为已经在本地处理完毕
    }


    // 构建gRPC请求对象
    TextChatMsgReq text_msg_req;
    text_msg_req.set_fromuid(uid);  // 设置发送者UID
    text_msg_req.set_touid(touid);  // 设置接收者UID
    // 复制消息内容到gRPC请求对象
    for (const auto& txt_obj : arrays) {
        auto content = txt_obj["content"].asString();  // 消息内容
        auto msgid = txt_obj["msgid"].asString();      // 消息ID
        std::cout << "content is " << content << std::endl;
        std::cout << "msgid is " << msgid << std::endl;

        // 添加单条消息到gRPC请求对象
        auto *text_msg = text_msg_req.add_textmsgs();
        text_msg->set_msgid(msgid);
        text_msg->set_msgcontent(content);
    }



	//发送通知 todo...
	// 通过gRPC客户端发送跨服务器的通知
	ChatGrpcClient::GetInstance()->NotifyTextChatMsg(to_ip_value, text_msg_req, rtvalue);
}



bool LogicSystem::isPureDigit(const std::string& str)
{
	for (char c : str) {
		if (!std::isdigit(c)) {
			return false;
		}
	}
	return true;
}

void LogicSystem::GetUserByUid(std::string uid_str, Json::Value& rtvalue)
{
	rtvalue["error"] = ErrorCodes::Success;

	std::string base_key = USER_BASE_INFO + uid_str;

	//优先查redis中查询用户信息
	std::string info_str = "";
	bool b_base = RedisMgr::GetInstance()->Get(base_key, info_str);
	if (b_base) {
		Json::Reader reader;
		Json::Value root;
		reader.parse(info_str, root);
		auto uid = root["uid"].asInt();
		auto name = root["name"].asString();
		auto pwd = root["pwd"].asString();
		auto email = root["email"].asString();
		auto nick = root["nick"].asString();
		auto desc = root["desc"].asString();
		auto sex = root["sex"].asInt();
		auto icon = root["icon"].asString();
		std::cout << "user  uid is  " << uid << " name  is "
			<< name << " pwd is " << pwd << " email is " << email <<" icon is " << icon << endl;

		rtvalue["uid"] = uid;
		rtvalue["pwd"] = pwd;
		rtvalue["name"] = name;
		rtvalue["email"] = email;
		rtvalue["nick"] = nick;
		rtvalue["desc"] = desc;
		rtvalue["sex"] = sex;
		rtvalue["icon"] = icon;
		return;
	}

	auto uid = std::stoi(uid_str);
	//redis中没有则查询mysql
	//查询数据库
	std::shared_ptr<UserInfo> user_info = nullptr;
	user_info = MysqlMgr::GetInstance()->GetUser(uid);
	if (user_info == nullptr) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}

	//将数据库内容写入redis缓存
	Json::Value redis_root;
	redis_root["uid"] = user_info->uid;
	redis_root["pwd"] = user_info->pwd;
	redis_root["name"] = user_info->name;
	redis_root["email"] = user_info->email;
	redis_root["nick"] = user_info->nick;
	redis_root["desc"] = user_info->desc;
	redis_root["sex"] = user_info->sex;
	redis_root["icon"] = user_info->icon;

	RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString());

	//返回数据
	rtvalue["uid"] = user_info->uid;
	rtvalue["pwd"] = user_info->pwd;
	rtvalue["name"] = user_info->name;
	rtvalue["email"] = user_info->email;
	rtvalue["nick"] = user_info->nick;
	rtvalue["desc"] = user_info->desc;
	rtvalue["sex"] = user_info->sex;
	rtvalue["icon"] = user_info->icon;
}

void LogicSystem::GetUserByName(std::string name, Json::Value& rtvalue)
{
	rtvalue["error"] = ErrorCodes::Success;

	std::string base_key = NAME_INFO + name;

	//优先查redis中查询用户信息
	std::string info_str = "";
	bool b_base = RedisMgr::GetInstance()->Get(base_key, info_str);
	if (b_base) {
		Json::Reader reader;
		Json::Value root;
		reader.parse(info_str, root);
		auto uid = root["uid"].asInt();
		auto name = root["name"].asString();
		auto pwd = root["pwd"].asString();
		auto email = root["email"].asString();
		auto nick = root["nick"].asString();
		auto desc = root["desc"].asString();
		auto sex = root["sex"].asInt();
		std::cout << "user  uid is  " << uid << " name  is "
			<< name << " pwd is " << pwd << " email is " << email << endl;

		rtvalue["uid"] = uid;
		rtvalue["pwd"] = pwd;
		rtvalue["name"] = name;
		rtvalue["email"] = email;
		rtvalue["nick"] = nick;
		rtvalue["desc"] = desc;
		rtvalue["sex"] = sex;
		return;
	}

	//redis中没有则查询mysql
	//查询数据库
	std::shared_ptr<UserInfo> user_info = nullptr;
	user_info = MysqlMgr::GetInstance()->GetUser(name);
	if (user_info == nullptr) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}

	//将数据库内容写入redis缓存
	Json::Value redis_root;
	redis_root["uid"] = user_info->uid;
	redis_root["pwd"] = user_info->pwd;
	redis_root["name"] = user_info->name;
	redis_root["email"] = user_info->email;
	redis_root["nick"] = user_info->nick;
	redis_root["desc"] = user_info->desc;
	redis_root["sex"] = user_info->sex;

	RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString());
	
	//返回数据
	rtvalue["uid"] = user_info->uid;
	rtvalue["pwd"] = user_info->pwd;
	rtvalue["name"] = user_info->name;
	rtvalue["email"] = user_info->email;
	rtvalue["nick"] = user_info->nick;
	rtvalue["desc"] = user_info->desc;
	rtvalue["sex"] = user_info->sex;
}

bool LogicSystem::GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo)
{
    // 优先从 Redis 中查询用户信息
    std::string info_str = ""; // 用于存储从 Redis 获取的用户信息字符串
    bool b_base = RedisMgr::GetInstance()->Get(base_key, info_str); // 从 Redis 获取用户信息
    if (b_base) { // 如果成功获取到用户信息
        Json::Reader reader; // 创建 JSON 解析器
        Json::Value root; // 创建 JSON 值对象
        reader.parse(info_str, root); // 解析获取的 JSON 字符串
        
        // 将解析后的信息填充到 userinfo 对象中
        userinfo->uid = root["uid"].asInt(); // 用户 ID
        userinfo->name = root["name"].asString(); // 用户姓名
        userinfo->pwd = root["pwd"].asString(); // 用户密码
        userinfo->email = root["email"].asString(); // 用户邮箱
        userinfo->nick = root["nick"].asString(); // 用户昵称
        userinfo->desc = root["desc"].asString(); // 用户描述
        userinfo->sex = root["sex"].asInt(); // 用户性别
        userinfo->icon = root["icon"].asString(); // 用户头像
        
        // 打印用户登录信息
        std::cout << "user login uid is  " << userinfo->uid << " name  is "
                  << userinfo->name << " pwd is " << userinfo->pwd << " email is " << userinfo->email << endl;
    }
    else {
        // 如果 Redis 中没有信息，则查询 MySQL 数据库
        std::shared_ptr<UserInfo> user_info = nullptr; // 创建指向用户信息的智能指针
        user_info = MysqlMgr::GetInstance()->GetUser(uid); // 从数据库中获取用户信息
        if (user_info == nullptr) { // 如果从数据库中获取失败
            return false; // 返回 false，表示未找到用户信息
        }

        userinfo = user_info; // 将从数据库获取的用户信息赋值给 userinfo

        // 将数据库中的内容写入 Redis 缓存
        Json::Value redis_root; // 创建用于存储 Redis 的 JSON 值对象
        redis_root["uid"] = uid; // 用户 ID
        redis_root["pwd"] = userinfo->pwd; // 用户密码
        redis_root["name"] = userinfo->name; // 用户姓名
        redis_root["email"] = userinfo->email; // 用户邮箱
        redis_root["nick"] = userinfo->nick; // 用户昵称
        redis_root["desc"] = userinfo->desc; // 用户描述
        redis_root["sex"] = userinfo->sex; // 用户性别
        redis_root["icon"] = userinfo->icon; // 用户头像

        // 将用户信息写入 Redis
        RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString()); // 将 JSON 对象转换为字符串并存入 Redis
    }

    return true; // 返回 true，表示成功获取用户基本信息
}


bool LogicSystem::GetFriendApplyInfo(int to_uid, std::vector<std::shared_ptr<ApplyInfo>> &list) {
	//从mysql获取好友申请列表
	return MysqlMgr::GetInstance()->GetApplyList(to_uid, list, 0, 10);
}

bool LogicSystem::GetFriendList(int self_id, std::vector<std::shared_ptr<UserInfo>>& user_list) {
	//从mysql获取好友列表
	return MysqlMgr::GetInstance()->GetFriendList(self_id, user_list);
}
