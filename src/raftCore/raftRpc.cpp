#include "raftRpc.h"
#include <mprpcchannel.h>
#include <mprpccontroller.h>

// 日志追加/心跳
bool RaftRpc::AppendEntries(raftRpcProctoc::AppendEntriesArgs *args, raftRpcProctoc::AppendEntriesReply *response) {
  MprpcController controller;
  stub_->AppendEntries(&controller, args, response, nullptr);
  return !controller;
}

// 快照同步
bool RaftRpc::InstallSnapshot(raftRpcProctoc::InstallSnapshotRequest *args,
                              raftRpcProctoc::InstallSnapshotResponse *response) {
  MprpcController controller;
  stub_->InstallSnapshot(&controller, args, response, nullptr);
  return !controller.Failed();
}

// 选举请求
bool RaftRpc::RequestVote(raftRpcProctoc::RequestVoteArgs *args, raftRpcProctoc::RequestVoteReply *response) {
  MprpcController controller;
  stub_->RequestVote(&controller, args, response, nullptr);
  return !controller.Failed();
}

RaftRpc::RaftRpc(std::string ip, short port) {  // 构建一个 RPC 连接到目标节点 (ip, port)
  stub_ = new raftRpcProctoc::raftRpc_Stub(new MprpcChannel(ip, port, true));
}

// 释放创建的 stub_，防止内存泄漏
RaftRpc::~RaftRpc() { delete stub_; }