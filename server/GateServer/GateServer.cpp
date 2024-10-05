// GateServer.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>
#include "const.h"
#include "CServer.h"
#include "ConfigMgr.h"
#include "hiredis.h"
#include "RedisMgr.h"
#include "MysqlMgr.h"
#include "AsioIOServicePool.h"

void TestRedis() {
	//连接redis 需要启动才可以进行连接
//redis默认监听端口为6387 可以再配置文件中修改
	redisContext* c = redisConnect("127.0.0.1", 6380);
	if (c->err)
	{
		printf("Connect to redisServer faile:%s\n", c->errstr);
		redisFree(c);        return;
	}
	printf("Connect to redisServer Success\n");

	std::string redis_password = "123456";
	redisReply* r = (redisReply*)redisCommand(c, "AUTH %s", redis_password.c_str());
	 if (r->type == REDIS_REPLY_ERROR) {
		 printf("Redis认证失败！\n");
	}else {
		printf("Redis认证成功！\n");
		 }

	//为redis设置key
	const char* command1 = "set stest1 value1";

	//执行redis命令行
    r = (redisReply*)redisCommand(c, command1);

	//如果返回NULL则说明执行失败
	if (NULL == r)
	{
		printf("Execut command1 failure\n");
		redisFree(c);        return;
	}

	//如果执行失败则释放连接
	if (!(r->type == REDIS_REPLY_STATUS && (strcmp(r->str, "OK") == 0 || strcmp(r->str, "ok") == 0)))
	{
		printf("Failed to execute command[%s]\n", command1);
		freeReplyObject(r);
		redisFree(c);        return;
	}

	//执行成功 释放redisCommand执行后返回的redisReply所占用的内存
	freeReplyObject(r);
	printf("Succeed to execute command[%s]\n", command1);

	const char* command2 = "strlen stest1";
	r = (redisReply*)redisCommand(c, command2);

	//如果返回类型不是整形 则释放连接
	if (r->type != REDIS_REPLY_INTEGER)
	{
		printf("Failed to execute command[%s]\n", command2);
		freeReplyObject(r);
		redisFree(c);        return;
	}

	//获取字符串长度
	int length = r->integer;
	freeReplyObject(r);
	printf("The length of 'stest1' is %d.\n", length);
	printf("Succeed to execute command[%s]\n", command2);

	//获取redis键值对信息
	const char* command3 = "get stest1";
	r = (redisReply*)redisCommand(c, command3);
	if (r->type != REDIS_REPLY_STRING)
	{
		printf("Failed to execute command[%s]\n", command3);
		freeReplyObject(r);
		redisFree(c);        return;
	}
	printf("The value of 'stest1' is %s\n", r->str);
	freeReplyObject(r);
	printf("Succeed to execute command[%s]\n", command3);

	const char* command4 = "get stest2";
	r = (redisReply*)redisCommand(c, command4);
	if (r->type != REDIS_REPLY_NIL)
	{
		printf("Failed to execute command[%s]\n", command4);
		freeReplyObject(r);
		redisFree(c);        return;
	}
	freeReplyObject(r);
	printf("Succeed to execute command[%s]\n", command4);

	//释放连接资源
	redisFree(c);

}

void TestRedisMgr() {
	assert(RedisMgr::GetInstance()->Set("blogwebsite","llfc.club"));
	std::string value="";
	assert(RedisMgr::GetInstance()->Get("blogwebsite", value) );
	assert(RedisMgr::GetInstance()->Get("nonekey", value) == false);
	assert(RedisMgr::GetInstance()->HSet("bloginfo","blogwebsite", "llfc.club"));
	assert(RedisMgr::GetInstance()->HGet("bloginfo","blogwebsite") != "");
	assert(RedisMgr::GetInstance()->ExistsKey("bloginfo"));
	assert(RedisMgr::GetInstance()->Del("bloginfo"));
	assert(RedisMgr::GetInstance()->Del("bloginfo"));
	assert(RedisMgr::GetInstance()->ExistsKey("bloginfo") == false);
	assert(RedisMgr::GetInstance()->LPush("lpushkey1", "lpushvalue1"));
	assert(RedisMgr::GetInstance()->LPush("lpushkey1", "lpushvalue2"));
	assert(RedisMgr::GetInstance()->LPush("lpushkey1", "lpushvalue3"));
	assert(RedisMgr::GetInstance()->RPop("lpushkey1", value));
	assert(RedisMgr::GetInstance()->RPop("lpushkey1", value));
	assert(RedisMgr::GetInstance()->LPop("lpushkey1", value));
	assert(RedisMgr::GetInstance()->LPop("lpushkey2", value)==false);
}

void TestMysqlMgr() {
	int id = MysqlMgr::GetInstance()->RegUser("wwc","secondtonone1@163.com","123456",": / res / head_1.jpg");
	std::cout << "id  is " << id << std::endl;
}

int main()
{
	try
	{
		// 初始化 MySQL 和 Redis 管理器
		MysqlMgr::GetInstance();   
		RedisMgr::GetInstance();

		// 加载配置文件，获取网关端口
		auto & gCfgMgr = ConfigMgr::Inst();
		std::string gate_port_str = gCfgMgr["GateServer"]["Port"];
		unsigned short gate_port = atoi(gate_port_str.c_str());

		// 创建 io_context 并初始化信号处理器
		net::io_context ioc{ 1 };
		boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);

        // 处理系统终止信号，优雅停止服务器
        signals.async_wait([&ioc](const boost::system::error_code& error, int signal_number) {
            if (!error) {
                ioc.stop();  // 停止 io_context 的运行
            }
        });

		// std::make_shared<CServer>(ioc, gate_port)->Start();
		auto server = std::make_shared<CServer>(ioc, gate_port);
        server->Start();

		std::cout << "Gate Server listen on port: " << gate_port << std::endl;

		// 运行 I/O 服务
        ioc.run();

		// 关闭 Redis 连接
		RedisMgr::GetInstance()->Close();
	}
    catch (std::exception const& e) {
        std::cerr << "Error: " << e.what() << std::endl;

        // 确保 Redis 连接在异常时也被正确关闭
        RedisMgr::GetInstance()->Close();
        return EXIT_FAILURE;
    }

 	return EXIT_SUCCESS;
}


/*
异步信号处理：Boost.Asio 提供了异步信号处理的机制，不会阻塞主线程的执行。这意味着服务器可以继续处理其他任务（如 gRPC 请求），而不是因为等待信号而停滞。

优雅关闭：通过异步信号处理，可以在不强制终止程序的情况下处理信号。在接收到 SIGINT 或 SIGTERM 后，gRPC 服务器可以完成所有正在进行的请求，然后优雅地关闭，避免数据丢失或不完整操作。

在这段代码中，Boost.Asio 的作用是处理异步信号等待，使得程序可以在收到 SIGINT 或 SIGTERM 信号时优雅地关闭 gRPC 服务器，并且不会因为等待信号而阻塞主线程的执行。

*/
