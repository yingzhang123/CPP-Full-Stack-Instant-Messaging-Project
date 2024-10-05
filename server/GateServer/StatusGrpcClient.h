#pragma once
#include "const.h"
#include "Singleton.h"
#include "ConfigMgr.h"
#include <grpcpp/grpcpp.h> 
#include "message.grpc.pb.h"
#include "message.pb.h"

using grpc::Channel;
using grpc::Status;
using grpc::ClientContext;

using message::GetChatServerReq;
using message::GetChatServerRsp;
using message::LoginRsp;
using message::LoginReq;
using message::StatusService;

// StatusConPool 类：gRPC 连接池，用于管理多个 StatusService::Stub 连接
class StatusConPool {
public:
    /**
     * @brief 构造函数，初始化连接池，创建指定数量的 gRPC 连接并存入队列
     * 
     * @param poolSize 连接池的大小
     * @param host gRPC 服务器的主机地址
     * @param port gRPC 服务器的端口
     */
    StatusConPool(size_t poolSize, std::string host, std::string port)
        : poolSize_(poolSize), host_(host), port_(port), b_stop_(false) {
        
        // 创建 poolSize_ 个 gRPC 连接并将其加入连接队列
        for (size_t i = 0; i < poolSize_; ++i) {
            // 创建 gRPC 连接，使用不安全的通道凭证（InsecureChannelCredentials）
            std::shared_ptr<Channel> channel = grpc::CreateChannel(host + ":" + port,
                grpc::InsecureChannelCredentials());

            // 将 gRPC 服务的 Stub 对象存入连接队列
            connections_.push(StatusService::NewStub(channel));
        }
    }

    /**
     * @brief 析构函数，确保在对象销毁时关闭连接池
     */
    ~StatusConPool() {
        // 加锁以确保线程安全
        std::lock_guard<std::mutex> lock(mutex_);
        Close();  // 关闭连接池
        // 清空连接队列
        while (!connections_.empty()) {
            connections_.pop();
        }
    }

    /**
     * @brief 从连接池中获取一个 gRPC 连接
     * 
     * @return std::unique_ptr<StatusService::Stub> 获取到的 gRPC Stub 对象
     */
    std::unique_ptr<StatusService::Stub> getConnection() {
        std::unique_lock<std::mutex> lock(mutex_);
        // 等待连接池中有可用连接，或连接池已停止
        cond_.wait(lock, [this] {
            if (b_stop_) {
                return true;
            }
            return !connections_.empty();  // 只要有可用连接就不再等待
        });
        // 如果连接池已停止，直接返回空指针
        if (b_stop_) {
            return nullptr;
        }
        // 获取一个 gRPC 连接并从队列中移除
        auto context = std::move(connections_.front());
        connections_.pop();
        return context;
    }

    /**
     * @brief 将 gRPC 连接归还到连接池
     * 
     * @param context 要归还的 gRPC Stub 对象
     */
    void returnConnection(std::unique_ptr<StatusService::Stub> context) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (b_stop_) {
            return;  // 如果连接池已停止，不再归还连接
        }
        // 将连接重新放入队列
        connections_.push(std::move(context));
        cond_.notify_one();  // 通知等待中的线程有新的连接可用
    }

    /**
     * @brief 关闭连接池，停止所有连接操作
     */
    void Close() {
        b_stop_ = true;           // 标记连接池已停止
        cond_.notify_all();       // 唤醒所有等待中的线程
    }

private:
    std::atomic<bool> b_stop_;   // 标记连接池是否已停止
    size_t poolSize_;            // 连接池大小
    std::string host_;           // gRPC 服务器的主机地址
    std::string port_;           // gRPC 服务器的端口
    std::queue<std::unique_ptr<StatusService::Stub>> connections_; // 存储 gRPC 连接的队列
    std::mutex mutex_;           // 互斥锁，确保线程安全
    std::condition_variable cond_; // 条件变量，用于线程同步
};


class StatusGrpcClient :public Singleton<StatusGrpcClient>
{
	friend class Singleton<StatusGrpcClient>;
public:
	~StatusGrpcClient() {

	}
	GetChatServerRsp GetChatServer(int uid);
	LoginRsp Login(int uid, std::string token);
private:
	StatusGrpcClient();
	std::unique_ptr<StatusConPool> pool_;
	
};



