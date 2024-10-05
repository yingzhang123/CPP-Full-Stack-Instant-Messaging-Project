const grpc = require('@grpc/grpc-js') // 引入 gRPC 库
const message_proto = require('./proto') // 引入生成的 gRPC 消息协议文件
const const_module = require('./const') // 引入常量模块
const { v4: uuidv4 } = require('uuid'); // 引入用于生成唯一ID的库
const emailModule = require('./email'); // 引入用于发送邮件的模块
const redis_module = require('./redis') // 引入 Redis 操作模块

/**
 * GetVarifyCode grpc响应获取验证码的服务
 * @param {*} call 为grpc请求，包含客户端发送的请求数据
 * @param {*} callback 为grpc回调，用于返回响应结果
 * @returns
 */

async function GetVarifyCode(call, callback) {
    console.log("email is ", call.request.email) // 打印客户端请求的邮箱地址

    try {
        // 从 Redis 中获取验证码，前缀为常量 `code_prefix`，key 是用户的邮箱地址
        let query_res = await redis_module.GetRedis(const_module.code_prefix + call.request.email);
        console.log("query_res is ", query_res) // 打印 Redis 查询结果

        let uniqueId = query_res; // 从 Redis 中查找到的验证码
        if (query_res == null) {
            // 如果 Redis 中没有验证码，则生成一个新的唯一验证码
            uniqueId = uuidv4(); // 生成 UUID
            if (uniqueId.length > 4) {
                uniqueId = uniqueId.substring(0, 4); // 将验证码限制为 4 位
            }
            // 将生成的验证码存储到 Redis 中，并设置有效期为 600 秒（10 分钟）
            let bres = await redis_module.SetRedisExpire(const_module.code_prefix + call.request.email, uniqueId, 600);
            if (!bres) {
                // 如果存储到 Redis 失败，返回 Redis 错误
                callback(null, {
                    email: call.request.email,
                    error: const_module.Errors.RedisErr // 设置错误码为 Redis 错误
                });
                return; // 终止函数执行
            }
        }

        console.log("uniqueId is ", uniqueId) // 打印生成的验证码
        let text_str = '您的验证码为' + uniqueId + '请三分钟内完成注册'; // 设置邮件内容

        // 设置邮件选项，包含发件人、收件人、主题和邮件正文
        let mailOptions = {
            from: 'secondtonone1@163.com', // 发件人的邮箱地址
            to: call.request.email, // 收件人的邮箱地址（客户端提供）
            subject: '验证码', // 邮件主题
            text: text_str, // 邮件正文，包含验证码
        };

        // 发送邮件
        let send_res = await emailModule.SendMail(mailOptions);
        console.log("send res is ", send_res) // 打印邮件发送结果

        // 成功发送邮件，返回成功的响应结果
        callback(null, {
            email: call.request.email,
            error: const_module.Errors.Success // 设置错误码为成功
        });

    } catch (error) {
        // 捕获执行过程中可能出现的异常，并返回异常错误码
        console.log("catch error is ", error)
        callback(null, {
            email: call.request.email,
            error: const_module.Errors.Exception // 设置错误码为异常
        });
    }
}

/**
 * main 函数用于启动 gRPC 服务器
 */
function main() {
    var server = new grpc.Server() // 创建一个 gRPC 服务器实例
    // 添加服务，指定服务接口为 VarifyService，具体方法为 GetVarifyCode
    server.addService(message_proto.VarifyService.service, { GetVarifyCode: GetVarifyCode })
    // 绑定服务器到指定地址和端口，使用非安全连接，服务器启动后打印信息
    server.bindAsync('0.0.0.0:50051', grpc.ServerCredentials.createInsecure(), () => {
        server.start() // 启动 gRPC 服务器
        console.log('varify server started') // 打印服务器启动成功的日志信息
    })
}

main() // 执行 main 函数启动服务器

/*
1. 接收 gRPC 请求
   |
   v
2. 从 Redis 查询验证码
   |
   v
   - 如果有：使用 Redis 中的验证码
   - 如果无：生成新的4位验证码
     |
     v
     保存新验证码到 Redis (600 秒有效期)
   |
   v
3. 构造邮件内容，发送验证码
   |
   v
4. 返回成功或失败响应 (含邮箱及状态码)
   |
   v
5. gRPC 服务器启动，监听请求

*/ 