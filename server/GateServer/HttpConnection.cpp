#include "HttpConnection.h"
#include "LogicSystem.h"
HttpConnection::HttpConnection(boost::asio::io_context& ioc)
	: _socket(ioc) {
}

// 开始监听此连接上的数据接收请求，并启动异步 HTTP 请求读取
void HttpConnection::Start()
{
    // 获取当前对象的 shared_ptr，以保证对象在异步操作期间不会被销毁
    auto self = shared_from_this();

    // 异步读取 HTTP 请求
    // _socket：用于与客户端通信的 socket
    // _buffer：缓冲区，用于存储读取的数据
    // _request：用于保存解析的 HTTP 请求消息
    // 回调函数：当读取操作完成或出错时调用
    http::async_read(
        _socket,           // 用于通信的 socket
        _buffer,           // 缓冲区，用于接收数据
        _request,          // 保存读取到的 HTTP 请求数据
        [self](beast::error_code ec, std::size_t bytes_transferred) {
            // 异常处理：如果出现错误，输出错误信息并返回
            try {
                // 如果在读取过程中发生错误，打印错误信息并返回
                if (ec) {
                    std::cout << "http read err is " << ec.what() << std::endl;
                    return;  // 直接返回，不再处理请求
                }

                // 处理成功读取的数据，忽略实际传输的字节数（可能不需要处理）
				// 忽略未使用的 bytes_transferred 参数（读取的字节数）。在一些情况下，字节数可能不需要处理，因此使用 boost::ignore_unused() 忽略这个参数以避免编译器警告。
                boost::ignore_unused(bytes_transferred);

                // 处理接收到的 HTTP 请求，解析并生成响应         调用 HandleReq() 函数处理接收到的 HTTP 请求。这个函数会解析请求并生成相应的响应消息。
                self->HandleReq();

                // 检查是否有超时，关闭未及时响应的连接
                self->CheckDeadline();
            }
            catch (std::exception& exp) {
                // 如果在处理请求或检查超时过程中发生异常，捕获并输出异常信息
                std::cout << "exception is " << exp.what() << std::endl;
            }
        }
    );
}


//char 转为16进制
unsigned char ToHex(unsigned char x)
{
	return  x > 9 ? x + 55 : x + 48;
}

//16进制转为char
unsigned char FromHex(unsigned char x)
{
	unsigned char y;
	if (x >= 'A' && x <= 'Z') y = x - 'A' + 10;
	else if (x >= 'a' && x <= 'z') y = x - 'a' + 10;
	else if (x >= '0' && x <= '9') y = x - '0';
	else assert(0);
	return y;
}

std::string UrlEncode(const std::string& str)
{
	std::string strTemp = "";
	size_t length = str.length();
	for (size_t i = 0; i < length; i++)
	{
		//判断是否仅有数字和字母构成
		if (isalnum((unsigned char)str[i]) ||
			(str[i] == '-') ||
			(str[i] == '_') ||
			(str[i] == '.') ||
			(str[i] == '~'))
			strTemp += str[i];
		else if (str[i] == ' ') //为空字符
			strTemp += "+";
		else
		{
			//其他字符需要提前加%并且高四位和低四位分别转为16进制
			strTemp += '%';
			strTemp += ToHex((unsigned char)str[i] >> 4);
			strTemp += ToHex((unsigned char)str[i] & 0x0F);
		}
	}
	return strTemp;
}

std::string UrlDecode(const std::string& str)
{
	std::string strTemp = "";
	size_t length = str.length();
	for (size_t i = 0; i < length; i++)
	{
		//还原+为空
		if (str[i] == '+') strTemp += ' ';
		//遇到%将后面的两个字符从16进制转为char再拼接
		else if (str[i] == '%')
		{
			assert(i + 2 < length);
			unsigned char high = FromHex((unsigned char)str[++i]);
			unsigned char low = FromHex((unsigned char)str[++i]);
			strTemp += high * 16 + low;
		}
		else strTemp += str[i];
	}
	return strTemp;
}

void HttpConnection::PreParseGetParam() {
    // 提取 URI，从请求中获取目标路径（包含查询参数）
    auto uri = _request.target();

    // 查找 '?' 的位置，表示查询字符串的开始。如果找不到 '?'，说明没有查询参数
    auto query_pos = uri.find('?');
    
    // 如果没有查询字符串，直接将 URI 赋给 _get_url，表示请求的路径部分
    if (query_pos == std::string::npos) {
        _get_url = uri;  // 保存完整的 URI 作为请求的 URL
        return;  // 没有查询参数，返回
    }

    // 分离出 URL 的路径部分（即 '?' 之前的部分）
    _get_url = uri.substr(0, query_pos);

    // 获取查询字符串部分（即 '?' 之后的部分）
    std::string query_string = uri.substr(query_pos + 1);
    std::string key;
    std::string value;
    size_t pos = 0;

    // 解析查询字符串，按照 '&' 分割参数对
    while ((pos = query_string.find('&')) != std::string::npos) {
        // 取出一个键值对（直到第一个 '&' 的位置）
        auto pair = query_string.substr(0, pos);
        size_t eq_pos = pair.find('=');  // 查找 '=' 分隔符，区分 key 和 value
        
        // 如果找到了 '='，将键值对分别保存
        if (eq_pos != std::string::npos) {
            // 假设有 UrlDecode 函数处理 URL 中的编码转换
            key = UrlDecode(pair.substr(0, eq_pos));  // 解码 key
            value = UrlDecode(pair.substr(eq_pos + 1));  // 解码 value
            _get_params[key] = value;  // 将键值对存入 _get_params
        }

        // 移除已处理的键值对，继续处理剩下的查询字符串
        query_string.erase(0, pos + 1);
    }

    // 处理查询字符串中最后一个键值对（如果剩下的部分没有 '&' 分隔符）
    if (!query_string.empty()) {
        size_t eq_pos = query_string.find('=');  // 查找 '=' 分隔符
        if (eq_pos != std::string::npos) {
            key = UrlDecode(query_string.substr(0, eq_pos));  // 解码 key
            value = UrlDecode(query_string.substr(eq_pos + 1));  // 解码 value
            _get_params[key] = value;  // 将键值对存入 _get_params
        }
    }
}


// 处理 HTTP 请求
void HttpConnection::HandleReq() {
    // 设置 HTTP 响应的版本号，与请求的 HTTP 版本保持一致
    _response.version(_request.version());
    
    // 设置为短连接，即处理完请求后不保持连接（HTTP/1.0 默认短连接）
    _response.keep_alive(false);
    
    // 处理 GET 请求
    if (_request.method() == http::verb::get) {
        // 解析 GET 请求的 URL 参数
        PreParseGetParam();
        
        // 处理 GET 请求逻辑，通过 LogicSystem 执行相应的处理
        // LogicSystem::HandleGet 函数根据请求的 URL 处理具体业务逻辑
        bool success = LogicSystem::GetInstance()->HandleGet(_get_url, shared_from_this());

        // 如果处理失败，返回 404 Not Found 错误
        if (!success) {
            _response.result(http::status::not_found);  // 设置响应状态为 404
            _response.set(http::field::content_type, "text/plain");  // 设置响应内容类型为纯文本
            beast::ostream(_response.body()) << "url not found\r\n";  // 响应内容为 "url not found"
            WriteResponse();  // 发送响应
            return;  // 结束处理
        }

        // 如果处理成功，返回 200 OK
        _response.result(http::status::ok);  // 设置响应状态为 200 OK
        _response.set(http::field::server, "GateServer");  // 设置服务器标识
        WriteResponse();  // 发送响应
        return;  // 结束处理
    }

    // 处理 POST 请求
    if (_request.method() == http::verb::post) {
        // 处理 POST 请求逻辑，通过 LogicSystem 执行相应的处理
        // LogicSystem::HandlePost 函数根据请求的 URL 处理具体业务逻辑
        bool success = LogicSystem::GetInstance()->HandlePost(_request.target(), shared_from_this());

        // 如果处理失败，返回 404 Not Found 错误
        if (!success) {
            _response.result(http::status::not_found);  // 设置响应状态为 404
            _response.set(http::field::content_type, "text/plain");  // 设置响应内容类型为纯文本
            beast::ostream(_response.body()) << "url not found\r\n";  // 响应内容为 "url not found"
            WriteResponse();  // 发送响应
            return;  // 结束处理
        }

        // 如果处理成功，返回 200 OK
        _response.result(http::status::ok);  // 设置响应状态为 200 OK
        _response.set(http::field::server, "GateServer");  // 设置服务器标识
        WriteResponse();  // 发送响应
        return;  // 结束处理
    }
}


void HttpConnection::CheckDeadline() {
    // 获取当前对象的 shared_ptr，以防止对象在异步操作期间被销毁
    auto self = shared_from_this();

    // 异步等待定时器触发事件
    deadline_.async_wait(
        // 回调函数：在定时器触发时执行
        [self](beast::error_code ec) {
            // 检查定时器是否触发时没有错误发生
            if (!ec) {
                // 如果定时器到期且没有取消，关闭 socket，取消任何未完成的操作
                self->_socket.close(ec);
            }
        }
    );
}


void HttpConnection::WriteResponse() {
    // 获取当前对象的 shared_ptr，确保对象在异步操作期间不会被销毁
    auto self = shared_from_this();

    // 设置 HTTP 响应内容的长度，将响应体的大小（字节数）设置为 Content-Length
    _response.content_length(_response.body().size());

    // 异步发送 HTTP 响应
    http::async_write(
        _socket,  // 用于通信的 socket
        _response,  // 要发送的 HTTP 响应消息
        // 回调函数：写操作完成后执行
        [self](beast::error_code ec, std::size_t) {
            // 关闭 socket 的写端，表示不再发送数据（即发送操作完成）
            self->_socket.shutdown(tcp::socket::shutdown_send, ec);
            
            // 取消定时器，防止继续检查超时（连接已处理完毕）
            self->deadline_.cancel();
        });
}


