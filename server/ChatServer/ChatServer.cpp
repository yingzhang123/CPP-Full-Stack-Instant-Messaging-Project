// ChatServer.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "LogicSystem.h"
#include <csignal>
#include <thread>
#include <mutex>
#include "AsioIOServicePool.h"
#include "CServer.h"
#include "ConfigMgr.h"
#include "RedisMgr.h"
#include "ChatServiceImpl.h"

using namespace std;
bool bstop = false;
std::condition_variable cond_quit;
std::mutex mutex_quit;

int main()
{
    // 获取配置管理器单例实例
    auto& cfg = ConfigMgr::Inst();
    // 从配置中获取当前服务器名称
    auto server_name = cfg["SelfServer"]["Name"];
	
	try {
		// 获取Asio I/O服务池单例实例
		auto pool = AsioIOServicePool::GetInstance();
		// 初始化登录计数为0，并将其存储在Redis中
		RedisMgr::GetInstance()->HSet(LOGIN_COUNT, server_name,"0");

		//定义一个GrpcServer

		std::string server_address(cfg["SelfServer"]["Host"] + ":" + cfg["SelfServer"]["RPCPort"]);
		ChatServiceImpl service;
		grpc::ServerBuilder builder;
		// 监听端口和添加服务
		builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
		builder.RegisterService(&service);
		// 构建并启动gRPC服务器
		std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
		std::cout << "RPC Server listening on " << server_address << std::endl;

        // 启动一个单独的线程来运行gRPC服务器
        std::thread grpc_server_thread([&server]() {
            server->Wait();  // 等待直到服务器被要求关闭
        });

 		// 创建Boost.Asio的I/O上下文对象
		boost::asio::io_context  io_context;
		// 设置信号集以捕获SIGINT (Ctrl+C) 和 SIGTERM 信号
		boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
		// 当接收到信号时执行的回调函数
		signals.async_wait([&io_context, pool, &server](auto, auto) {
			io_context.stop();
			pool->Stop();
			server->Shutdown();
			});
		
       // 从配置中读取TCP端口号并启动CServer
        auto port_str = cfg["SelfServer"]["Port"];
        CServer s(io_context, atoi(port_str.c_str()));
        io_context.run();  // 运行I/O上下文

        // 清理工作：从Redis中删除登录计数键值对
        RedisMgr::GetInstance()->HDel(LOGIN_COUNT, server_name);
        // 关闭Redis连接
        RedisMgr::GetInstance()->Close();
        // 等待gRPC服务器线程退出
        grpc_server_thread.join();
	}
	catch (std::exception& e) {
        // 捕捉异常，打印错误信息，并进行清理
        std::cerr << "Exception: " << e.what() << endl;
        RedisMgr::GetInstance()->HDel(LOGIN_COUNT, server_name);
        RedisMgr::GetInstance()->Close();
	}
	    return 0;  // 主函数返回

}

