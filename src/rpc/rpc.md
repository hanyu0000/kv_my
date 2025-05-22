# RPC框架学习指南

### 概念理解

#### 1.1 理解RPC的基本概念
- **什么是RPC**：远程过程调用，让远程调用看起来像本地调用
- **为什么需要RPC**：分布式系统中不同机器需要相互通信
- **RPC vs 本地调用**：网络通信 vs 直接内存访问

#### 1.2 理解核心组件
- **MprpcChannel**：客户端网络通道，负责发送请求和接收响应
- **RpcProvider**：服务端提供者，管理服务注册和请求处理
- **MprpcController**：控制器，管理RPC调用状态和错误信息
- **Stub**：代理对象，客户端通过它调用远程服务

### 源码分析

#### 3.1 从简单到复杂
1. **先看MprpcController**：最简单的组件
2. **再看MprpcConfig**：配置管理
3. **然后看MprpcChannel**：客户端通信
4. **最后看RpcProvider**：服务端管理

#### 3.2 重点关注的文件
```
src/rpc/mprpccontroller.cpp    # 控制器实现（最简单）
src/rpc/mprpcconfig.cpp        # 配置管理
src/rpc/mprpcchannel.cpp       # 客户端网络通信
src/rpc/rpcprovider.cpp        # 服务端管理（最复杂）
```

#### 3.3 分析顺序
1. **理解消息格式**：`rpcheader.proto`
2. **理解配置管理**：如何读取配置文件
3. **理解客户端**：如何发送请求和接收响应
4. **理解服务端**：如何接收请求和调用业务方法

### 动手实践

#### 4.1 创建简单的RPC服务
```cpp
// 1. 定义proto文件
syntax = "proto3";
package hello;

service HelloService {
    rpc SayHello(HelloRequest) returns (HelloResponse);
}

message HelloRequest {
    string name = 1;
}

message HelloResponse {
    string message = 1;
}
```

#### 4.2 实现服务端
```cpp
class HelloServiceImpl : public hello::HelloService {
    void SayHello(google::protobuf::RpcController* controller,
                  const hello::HelloRequest* request,
                  hello::HelloResponse* response,
                  google::protobuf::Closure* done) override {
        
        std::string name = request->name();
        response->set_message("Hello, " + name + "!");
        done->Run();
    }
};

// 启动服务
RpcProvider provider;
HelloServiceImpl service;
provider.NotifyService(&service);
provider.Run(0, 8080);
```

#### 4.3 实现客户端
```cpp
MprpcChannel channel("127.0.0.1", 8080, true);
hello::HelloService_Stub stub(&channel);

hello::HelloRequest request;
hello::HelloResponse response;
request.set_name("World");

MprpcController controller;
stub.SayHello(&controller, &request, &response, nullptr);

if (!controller.Failed()) {
    std::cout << response.message() << std::endl;
}
```

### 深入理解

#### 5.1 网络通信细节
- **TCP连接管理**：连接建立、断开、重连
- **消息序列化**：protobuf的使用
- **消息格式**：header + body的结构
- **错误处理**：网络异常、业务异常

#### 5.2 性能优化
- **连接池**：复用TCP连接
- **异步调用**：非阻塞的RPC调用
- **负载均衡**：多个服务端的选择
- **超时处理**：请求超时的处理

#### 5.3 可靠性保证
- **重试机制**：网络失败时的重试
- **熔断机制**：服务不可用时的保护
- **监控告警**：RPC调用的监控

## 实践项目

### 项目1：简单计算器服务
创建一个支持加减乘除的RPC服务。

### 项目2：文件传输服务
创建一个支持文件上传下载的RPC服务。

### 项目3：聊天服务
创建一个支持多用户聊天的RPC服务。

## 常见问题

### Q1: 如何理解protobuf？
A: protobuf是Google的数据序列化格式，类似于JSON但更高效。它定义了消息的结构，然后生成对应的C++代码。

### Q2: 为什么需要序列化？
A: 网络传输只能传输字节流，而程序中的对象需要转换为字节流才能传输。

### Q3: Stub是什么？
A: Stub是代理对象，客户端通过它来调用远程服务，就像调用本地方法一样。

### Q4: 如何处理网络异常？
A: 通过重连机制、超时设置、错误处理等方式来处理网络异常。