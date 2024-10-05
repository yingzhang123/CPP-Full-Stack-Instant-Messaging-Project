#pragma once
#include <string>
#include <iostream>
#include <memory>
#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include "const.h"
#include "Singleton.h"
#include "ConfigMgr.h"
using grpc::Channel;
using grpc::Status;
using grpc::ClientContext;

using message::GetVarifyReq;
using message::GetVarifyRsp;
using message::VarifyService;

class RPConPool {
public:
    // 构造函数，初始化连接池，创建指定数量的 gRPC 连接并存入队列
    RPConPool(size_t poolSize, std::string host, std::string port)
        : poolSize_(poolSize), host_(host), port_(port), b_stop_(false) {
        
        // 创建 poolSize_ 个 gRPC 连接并将其加入连接队列
        for (size_t i = 0; i < poolSize_; ++i) {
            // 创建 gRPC 连接，使用 insecure channel（非加密）
            std::shared_ptr<Channel> channel = grpc::CreateChannel(host + ":" + port,
                grpc::InsecureChannelCredentials());
            
            // 将 gRPC 服务的 Stub 对象存入队列中
            connections_.push(VarifyService::NewStub(channel));
        }
    }

    // 析构函数，确保在对象销毁时关闭连接池
    ~RPConPool() {
        // 加锁以确保线程安全
        std::lock_guard<std::mutex> lock(mutex_);
        Close();  // 关闭连接池
        // 清空连接队列
        while (!connections_.empty()) {
            connections_.pop();
        }
    }

    // 从连接池中获取一个 gRPC 连接
    std::unique_ptr<VarifyService::Stub> getConnection() {
        std::unique_lock<std::mutex> lock(mutex_);
        // 等待连接池中有可用连接，如果连接池已关闭，则直接返回
        cond_.wait(lock, [this] {
            if (b_stop_) {
                return true;
            }
            return !connections_.empty();  // 只要有可用连接就不再等待
        });
        // 如果连接池已停止，返回空指针
        if (b_stop_) {
            return nullptr;
        }
        // 获取一个 gRPC 连接
        auto context = std::move(connections_.front());
        connections_.pop();
        return context;
    }

    // 将 gRPC 连接归还到连接池
    void returnConnection(std::unique_ptr<VarifyService::Stub> context) {
        // 加锁确保线程安全
        std::lock_guard<std::mutex> lock(mutex_);
        if (b_stop_) {
            return;  // 如果连接池已停止，不再归还连接
        }
        // 将连接重新放入队列
        connections_.push(std::move(context));
        cond_.notify_one();  // 通知等待中的线程有新的连接可用
    }

    // 关闭连接池，停止所有连接操作
    void Close() {
        b_stop_ = true;  // 标记连接池已停止
        cond_.notify_all();  // 唤醒所有等待中的线程
    }

private:
    atomic<bool> b_stop_;   // 用于标记连接池是否已停止
    size_t poolSize_;       // 连接池大小
    std::string host_;      // gRPC 服务器的主机地址
    std::string port_;      // gRPC 服务器的端口
    std::queue<std::unique_ptr<VarifyService::Stub>> connections_;     // 存储 gRPC 连接的队列
    std::mutex mutex_;                         // 互斥锁，确保线程安全
    std::condition_variable cond_;             // 条件变量，用于线程同步
};

class VerifyGrpcClient:public Singleton<VerifyGrpcClient>
{
	friend class Singleton<VerifyGrpcClient>;
public:
	~VerifyGrpcClient() {
		
	}
    GetVarifyRsp GetVarifyCode(std::string email) {
        ClientContext context;           // 创建 gRPC 客户端上下文
        GetVarifyRsp reply;              // 用于存储 gRPC 调用的响应
        GetVarifyReq request;            // 用于存储 gRPC 调用的请求数据
        request.set_email(email);        // 设置请求中的 email 字段

        // 从连接池中获取一个 gRPC 连接
        auto stub = pool_->getConnection();
        if (!stub) { // 检查是否成功获取连接
            GetVarifyRsp error_reply;
            error_reply.set_error(ErrorCodes::RPCFailed); // 设置错误代码
            return error_reply;
        }

        // 调用 gRPC 服务的 GetVarifyCode 方法                                  此处使用的是同步读写
        Status status = stub->GetVarifyCode(&context, request, &reply);

        if (status.ok()) {
            // 如果调用成功，将连接返回到连接池
            pool_->returnConnection(std::move(stub));
            return reply;  // 返回调用结果
        }
        else {
            // 如果调用失败，将连接返回到连接池，并设置错误码
            pool_->returnConnection(std::move(stub));
            reply.set_error(ErrorCodes::RPCFailed); // 设置错误代码为 RPC 失败
            return reply;
        }
    }



private:
	VerifyGrpcClient();

	std::unique_ptr<RPConPool> pool_;       // 指向连接池的智能指针，用于管理 gRPC 连接
};


