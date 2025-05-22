#ifndef SKIP_LIST_ON_RAFT_CLERK_H
#define SKIP_LIST_ON_RAFT_CLERK_H

#include <arpa/inet.h>
#include <netinet/in.h>
#include <raftServerRpcUtil.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <cerrno>
#include <string>
#include <vector>

#include "kvServerRPC.pb.h"
#include "mprpcconfig.h"

class Clerk {
 private:
  // 保存所有raft节点的RPC连接
  std::vector<std::shared_ptr<raftServerRpcUtil>> m_servers;
  std::string m_clientId;  // 客户端唯一标识
  int m_requestId;         // 请求序列号，用于线性一致性
  int m_recentLeaderId;    // 最近已知的leader节点ID（可能不是最新的）

  // 生成UUID作为客户端ID
  std::string Uuid() {
    return std::to_string(rand()) + std::to_string(rand()) + std::to_string(rand()) + std::to_string(rand());
  }
  
  void PutAppend(std::string key, std::string value, std::string op);

 public:
  // 对外暴露的三个功能和初始化
  void Init(std::string configFileName);
  std::string Get(std::string key);

  void Put(std::string key, std::string value);
  void Append(std::string key, std::string value);

 public:
  Clerk();
};

#endif  // SKIP_LIST_ON_RAFT_CLERK_H
