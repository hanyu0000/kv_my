#include "raftRpc.h"

#include <mprpcchannel.h>
#include <mprpccontroller.h>

// 封装 Raft 协议中的 RPC 调用接口
bool RaftRpc::AppendEntries(raftRpcProctoc::AppendEntriesArgs *args, raftRpcProctoc::AppendEntriesReply *response) {
  MprpcController controller;
  stub_->AppendEntries(&controller, args, response, nullptr);
  return !controller;
}

bool RaftRpc::InstallSnapshot(raftRpcProctoc::InstallSnapshotRequest *args,
                              raftRpcProctoc::InstallSnapshotResponse *response) {
  MprpcController controller;
  stub_->InstallSnapshot(&controller, args, response, nullptr);
  return !controller.Failed();
}

bool RaftRpc::RequestVote(raftRpcProctoc::RequestVoteArgs *args, raftRpcProctoc::RequestVoteReply *response) {
  MprpcController controller;
  stub_->RequestVote(&controller, args, response, nullptr);
  return !controller.Failed();
}

RaftRpc::RaftRpc(std::string ip, short port) {  // 发送rpc设置
  stub_ = new raftRpcProctoc::raftRpc_Stub(new MprpcChannel(ip, port, true));
}

RaftRpc::~RaftRpc() { delete stub_; }