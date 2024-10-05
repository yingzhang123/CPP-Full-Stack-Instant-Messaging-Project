#include "LogicSystem.h"
#include "HttpConnection.h"
#include "VerifyGrpcClient.h"
#include "RedisMgr.h"
#include "MysqlMgr.h"
#include "StatusGrpcClient.h"

LogicSystem::LogicSystem() {
	
	// 注册 GET 请求 "/get_test" 的处理逻辑
	RegGet("/get_test", [](std::shared_ptr<HttpConnection> connection) {
		// 向客户端返回一个简单的 GET 请求响应
		beast::ostream(connection->_response.body()) << "receive get_test req " << std::endl;
		int i = 0;
		// 遍历查询参数并输出到响应体中
		for (auto& elem : connection->_get_params) {
			i++;
			beast::ostream(connection->_response.body()) << "param" << i << " key is " << elem.first;
			beast::ostream(connection->_response.body()) << ", " << " value is " << elem.second << std::endl;
		}
	});

	// 注册 POST 请求 "/test_procedure" 的处理逻辑
	RegPost("/test_procedure", [](std::shared_ptr<HttpConnection> connection) {
		// 获取 POST 请求体字符串
		auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
		std::cout << "receive body is " << body_str << std::endl;
		// 设置响应内容类型为 JSON
		connection->_response.set(http::field::content_type, "text/json");
		Json::Value root, src_root;
		Json::Reader reader;
		// 解析请求体中的 JSON 数据
		bool parse_success = reader.parse(body_str, src_root);
		if (!parse_success) {
			std::cout << "Failed to parse JSON data!" << std::endl;
			// 如果解析失败，返回错误信息
			root["error"] = ErrorCodes::Error_Json;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		// 检查 JSON 中是否包含 "email" 字段
		if (!src_root.isMember("email")) {
			std::cout << "Failed to parse JSON data!" << std::endl;
			root["error"] = ErrorCodes::Error_Json;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		// 从数据库执行过程查询并返回结果
		auto email = src_root["email"].asString();
		int uid = 0;
		std::string name = "";
		MysqlMgr::GetInstance()->TestProcedure(email, uid, name);
		std::cout << "email is " << email << std::endl;

		// 构造响应 JSON
		root["error"] = ErrorCodes::Success;
		root["email"] = src_root["email"];
		root["name"] = name;
		root["uid"] = uid;
		std::string jsonstr = root.toStyledString();
		beast::ostream(connection->_response.body()) << jsonstr;
		return true;
	});

	// 注册 POST 请求 "/get_varifycode" 的处理逻辑
	RegPost("/get_varifycode", [](std::shared_ptr<HttpConnection> connection) {

		// 读取请求体中的数据并将其转换为字符串形式
		auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
		std::cout << "receive body is " << body_str << std::endl;

		// 设置 HTTP 响应的内容类型为 JSON 格式
		connection->_response.set(http::field::content_type, "text/json");

		// 创建 JSON 对象用于解析请求体中的 JSON 数据
		Json::Value root, src_root;
		Json::Reader reader;

		// 解析请求体中的 JSON 数据
		bool parse_success = reader.parse(body_str, src_root);
		if (!parse_success) {
			// 如果解析失败，返回错误信息
			root["error"] = ErrorCodes::Error_Json;  // 错误代码：JSON 解析失败
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;  // 返回响应并结束处理
		}

		// 从解析出的 JSON 数据中提取用户的邮箱地址
		auto email = src_root["email"].asString();
		// 通过 gRPC 客户端调用获取验证码的接口
		GetVarifyRsp rsp = VerifyGrpcClient::GetInstance()->GetVarifyCode(email);

		// 打印邮箱地址以供调试
		std::cout << "email is " << email << std::endl;
		// 将 gRPC 响应中的错误码存储到 JSON 响应中
		root["error"] = rsp.error();
		// 将请求中的邮箱地址也放入 JSON 响应中
		root["email"] = src_root["email"];

		// 将响应构造成 JSON 格式并写入 HTTP 响应体中
		std::string jsonstr = root.toStyledString();
		beast::ostream(connection->_response.body()) << jsonstr;

		return true;  // 返回响应并结束处理
	});

	// 注册用户注册逻辑 POST 请求 "/user_register"
	RegPost("/user_register", [](std::shared_ptr<HttpConnection> connection) {
		// 获取并解析 JSON 请求体
		auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
		std::cout << "receive body is " << body_str << std::endl;
		connection->_response.set(http::field::content_type, "text/json");
		Json::Value root, src_root;
		Json::Reader reader;
		bool parse_success = reader.parse(body_str, src_root);
		if (!parse_success) {
			root["error"] = ErrorCodes::Error_Json;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		// 验证密码和确认密码是否匹配
		auto pwd = src_root["passwd"].asString();
		auto confirm = src_root["confirm"].asString();
		if (pwd != confirm) {
			root["error"] = ErrorCodes::PasswdErr;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		// 检查 Redis 中的验证码是否过期
		std::string varify_code;
		bool b_get_varify = RedisMgr::GetInstance()->Get(CODEPREFIX + src_root["email"].asString(), varify_code);
		if (!b_get_varify || varify_code != src_root["varifycode"].asString()) {
			root["error"] = ErrorCodes::VarifyCodeErr;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		// 将用户信息注册到 MySQL 数据库中
		auto email = src_root["email"].asString();
		auto name = src_root["user"].asString();
		auto icon = src_root["icon"].asString();
		int uid = MysqlMgr::GetInstance()->RegUser(name, email, pwd, icon);
		if (uid == 0 || uid == -1) {
			root["error"] = ErrorCodes::UserExist;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		// 返回成功的注册信息
		root["error"] = 0;
		root["uid"] = uid;
		std::string jsonstr = root.toStyledString();
		beast::ostream(connection->_response.body()) << jsonstr;
		return true;
	});

	// 注册 POST 请求 "/reset_pwd" 的处理逻辑
	RegPost("/reset_pwd", [](std::shared_ptr<HttpConnection> connection) {

		// 从 HTTP 请求体中提取数据并将其转换为字符串
		auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
		std::cout << "receive body is " << body_str << std::endl;
		
		// 设置 HTTP 响应的内容类型为 JSON 格式
		connection->_response.set(http::field::content_type, "text/json");

		// 定义 JSON 对象，用于解析请求体中的 JSON 数据
		Json::Value root;
		Json::Reader reader;
		Json::Value src_root;

		// 尝试解析请求体中的 JSON 数据
		bool parse_success = reader.parse(body_str, src_root);
		if (!parse_success) {
			// 如果解析失败，返回 JSON 格式的错误信息
			std::cout << "Failed to parse JSON data!" << std::endl;
			root["error"] = ErrorCodes::Error_Json;  // 错误代码：JSON 解析失败
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;  // 返回响应并结束处理
		}

		// 从解析出的 JSON 数据中提取 email、用户名和新密码
		auto email = src_root["email"].asString();
		auto name = src_root["user"].asString();
		auto pwd = src_root["passwd"].asString();

		// 从 Redis 中查找与 email 关联的验证码
		std::string varify_code;
		bool b_get_varify = RedisMgr::GetInstance()->Get(CODEPREFIX + src_root["email"].asString(), varify_code);
		if (!b_get_varify) {
			// 如果验证码已过期或不存在，返回验证码过期的错误信息
			std::cout << " get varify code expired" << std::endl;
			root["error"] = ErrorCodes::VarifyExpired;  // 错误代码：验证码过期
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;  // 返回响应并结束处理
		}

		// 检查用户提供的验证码是否正确
		if (varify_code != src_root["varifycode"].asString()) {
			// 如果验证码不匹配，返回验证码错误的错误信息
			std::cout << " varify code error" << std::endl;
			root["error"] = ErrorCodes::VarifyCodeErr;  // 错误代码：验证码错误
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;  // 返回响应并结束处理
		}

		// 验证用户名和邮箱是否匹配
		bool email_valid = MysqlMgr::GetInstance()->CheckEmail(name, email);
		if (!email_valid) {
			// 如果用户名和邮箱不匹配，返回错误信息
			std::cout << " user email not match" << std::endl;
			root["error"] = ErrorCodes::EmailNotMatch;  // 错误代码：邮箱与用户名不匹配
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;  // 返回响应并结束处理
		}

		// 更新用户密码
		bool b_up = MysqlMgr::GetInstance()->UpdatePwd(name, pwd);
		if (!b_up) {
			// 如果更新密码失败，返回错误信息
			std::cout << " update pwd failed" << std::endl;
			root["error"] = ErrorCodes::PasswdUpFailed;  // 错误代码：密码更新失败
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;  // 返回响应并结束处理
		}

		// 成功更新密码，返回成功的响应信息
		std::cout << "succeed to update password " << pwd << std::endl;
		root["error"] = 0;  // 错误代码 0 表示成功
		root["email"] = email;  // 返回用户的邮箱
		root["user"] = name;  // 返回用户名
		root["passwd"] = pwd;  // 返回更新后的密码
		root["varifycode"] = src_root["varifycode"].asString();  // 返回验证码
		std::string jsonstr = root.toStyledString();

		// 将构造好的 JSON 响应内容写入 HTTP 响应体中
		beast::ostream(connection->_response.body()) << jsonstr;

		return true;  // 返回响应并结束处理
	});

	// 注册 POST 请求 "/user_login" 的处理逻辑
	RegPost("/user_login", [](std::shared_ptr<HttpConnection> connection) {
		
		// 从 HTTP 请求体中提取数据并将其转换为字符串形式
		auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
		std::cout << "receive body is " << body_str << std::endl;
		
		// 设置 HTTP 响应内容的类型为 JSON 格式
		connection->_response.set(http::field::content_type, "text/json");

		// 定义 JSON 对象，用于解析请求体中的 JSON 数据
		Json::Value root;
		Json::Reader reader;
		Json::Value src_root;

		// 解析请求体中的 JSON 数据，并存储在 src_root 对象中
		bool parse_success = reader.parse(body_str, src_root);
		if (!parse_success) {
			// 如果解析失败，返回 JSON 格式的错误信息，并设置错误代码
			std::cout << "Failed to parse JSON data!" << std::endl;
			root["error"] = ErrorCodes::Error_Json; // 定义错误代码为 JSON 解析失败
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;  // 结束请求处理并返回
		}

		// 从解析出的 JSON 数据中提取 email 和密码
		auto email = src_root["email"].asString();
		auto pwd = src_root["passwd"].asString();
		
		// 定义 UserInfo 对象，用于存储从数据库获取的用户信息
		UserInfo userInfo;

		// 查询数据库，判断用户邮箱和密码是否匹配
		bool pwd_valid = MysqlMgr::GetInstance()->CheckPwd(email, pwd, userInfo);
		if (!pwd_valid) {
			// 如果用户名或密码不匹配，返回错误信息并设置错误代码
			std::cout << " user pwd not match" << std::endl;
			root["error"] = ErrorCodes::PasswdInvalid; // 定义错误代码为密码无效
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;  // 结束请求处理并返回
		}

		// 通过 gRPC 调用，查询 StatusServer 服务以获取合适的聊天服务器连接信息
		auto reply = StatusGrpcClient::GetInstance()->GetChatServer(userInfo.uid);
		if (reply.error()) {
			// 如果 gRPC 调用失败，返回错误信息并设置错误代码
			std::cout << " grpc get chat server failed, error is " << reply.error() << std::endl;
			root["error"] = ErrorCodes::RPCFailed; // 定义错误代码为 gRPC 调用失败
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;  // 结束请求处理并返回
		}

		// 成功查询到用户信息和服务器连接信息
		std::cout << "succeed to load userinfo uid is " << userInfo.uid << std::endl;

		// 构造 JSON 格式的响应内容
		root["error"] = 0;  // 错误代码 0 表示成功
		root["email"] = email;  // 返回用户邮箱
		root["uid"] = userInfo.uid;  // 返回用户 ID
		root["token"] = reply.token();  // 返回用户的身份验证令牌
		root["host"] = reply.host();  // 返回聊天服务器的主机地址
		root["port"] = reply.port();  // 返回聊天服务器的端口号
		
		// 将构造好的 JSON 格式字符串写入 HTTP 响应体中
		std::string jsonstr = root.toStyledString();
		beast::ostream(connection->_response.body()) << jsonstr;

		return true;  // 结束请求处理并返回
	});

}

void LogicSystem::RegGet(std::string url, HttpHandler handler) {
	_get_handlers.insert(make_pair(url, handler));
}

void LogicSystem::RegPost(std::string url, HttpHandler handler) {
	_post_handlers.insert(make_pair(url, handler));
}

LogicSystem::~LogicSystem() {

}

bool LogicSystem::HandleGet(std::string path, std::shared_ptr<HttpConnection> con) {
	if (_get_handlers.find(path) == _get_handlers.end()) {
		return false;
	}

	_get_handlers[path](con);
	return true;
}

bool LogicSystem::HandlePost(std::string path, std::shared_ptr<HttpConnection> con) {
	if (_post_handlers.find(path) == _post_handlers.end()) {
		return false;
	}

	_post_handlers[path](con);
	return true;
}