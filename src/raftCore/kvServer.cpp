#include "kvServer.h"
#include <rpcprovider.h>
#include "mprpcconfig.h"

// 当 Debug 模式开启时，加锁并打印跳表内容
void KvServer::DprintKVDB() {
  if (!Debug) return;
  std::lock_guard<std::mutex> lg(m_mtx);  // 加锁,保证线程安全
  m_skipList.display_list();              // 打印跳表内容
}

void KvServer::ExecuteAppendOpOnKVDB(Op op) {
  {
    std::lock_guard<std::mutex> lg(m_mtx);            // 加锁
    m_skipList.insert_set_element(op.Key, op.Value);  // 向跳表中插入/更新键值
    m_lastRequestID[op.ClientId] = op.RequestId;      // 记录客户端最近请求的 ID
  }

  DprintKVDB();  // 已释放锁，避免死锁
}

// 处理客户端的读取操作,从跳表中查找键并返回对应的值与是否存在标志
void KvServer::ExecuteGetOpOnKVDB(Op op, std::string *value, bool *exist) {
  m_mtx.lock();

  *value = "";
  *exist = false;
  if (m_skipList.search_element(op.Key, *value)) *exist = true;

  // 记录该客户端最后一次请求的 RequestId，方便幂等或重复请求检查
  m_lastRequestID[op.ClientId] = op.RequestId;

  m_mtx.unlock();

  if (*exit) {  // 打印调试信息：键存在
  } else {      // 打印调试信息：键不存在
  }

  DprintKVDB();
}

// Put
void KvServer::ExecutePutOpOnKVDB(Op op) {
  m_mtx.lock();

  m_skipList.insert_set_element(op.Key, op.Value);
  m_lastRequestID[op.ClientId] = op.RequestId;

  m_mtx.unlock();

  DprintKVDB();
}

// 处理来自客户端（Clerk）的 Get 请求（RPC）
// 分布式一致性 KV 存储系统中典型的一段“线性一致读”逻辑，利用了 Raft 协议进行同步
void KvServer::Get(const raftKVRpcProctoc::GetArgs *args, raftKVRpcProctoc::GetReply *reply) {
  Op op;
  op.Operation = "Get";
  op.key = args->key();
  op.Value = "";
  op.ClientId = args->clientid();
  op.RequestId = args->requestid();

  int raftIndex = -1;
  int _ = -1;
  bool isLeader = false;
  m_raftNode->Start(op, &raftIndex, &_, &isLeader);  // 如果是当前节点是 leader，则 Start 会返回isLeader = true
  // raftIndex：raft预计的logIndex
  // 虽然是预计，但是正确情况下是准确的，op的具体内容对raft来说 是隔离的

  if (!isLeader) {
    reply->set_err(ErrWrongLeader);
    return;
  }

  m_mtx.lock();

  if (waitApplyCh.find(raftIndex) == waitApplyCh.end()) {
    waitApplyCh.insert(std::make_pair(raftIndex, new LockQueue<Op>()));
  }
  auto chForRaftIndex = waitApplyCh[raftIndex];

  m_mtx.unlock();  // 直接解锁

  // 超时等待 Raft 应用该条日志，若失败走超时处理分支
  Op raftCommitOp;
  if (!chForRaftIndex->timeOutPop(CONSENSUS_TIMEOUT, &raftCommitOp)) {
    int _ = -1;
    bool isLeader = false;
    m_raftNode->GetState(&_, &isLeader);

    if (ifRequestDuplicate(op.ClientId, op.RequestId) && isLeader) {
      // 当前节点仍是 leader 且请求已被处理过
      std::string value;
      bool exist = false;
      ExecuteGetOpOnKVDB(op, &value, &exist);

      if (exist) {
        reply->set_err(OK);
        reply->set_value(value);
      } else {
        reply->set_err(ErrNoKey);
        reply->set_value("");
      }

    } else {
      reply->set_err(ErrWrongLeader);  // 返回这个，其实就是让clerk换一个节点重试
    }
  } else {
    // raft已经提交了该command（op），可以正式开始执行了
    // todo 这里还要再次检验的原因：感觉不用检验，因为leader只要正确的提交了，那么这些肯定是符合的
    if (raftCommitOp.ClientId == op.ClientId && raftCommitOp.RequestId == op.RequestId) {
      std::string value;
      bool exist = false;
      ExecuteGetOpOnKVDB(op, &value, &exist);

      if (exist) {
        reply->set_err(OK);
        reply->set_value(value);
      } else {
        reply->set_err(ErrNoKey);
        reply->set_value("");
      }

    } else {
      reply->set_err(ErrWrongLeader);
    }
  }

  m_mtx.lock();

  auto tmp = waitApplyCh[raftIndex];
  waitApplyCh.erase(raftIndex);
  delete tmp;

  m_mtx.unlock();
}

void KvServer::GetCommandFromRaft(ApplyMsg message) {}