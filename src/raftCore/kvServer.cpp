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

// 接收 Raft 提交的日志（ApplyMsg），反序列化为 Op 操作，并将其应用到本地 KV 存储中，同时通知前端 RPC 响应线程
void KvServer::GetCommandFromRaft(ApplyMsg message) {
  Op op;
  op.parseFromString(message.Command);  // 反序列化成 Op

  // 如果该日志条目早于最近的快照点，则跳过；避免重复 apply 快照中已经包含的数据
  if (message.CommandIndex <= m_lastSnapShotRaftLogIndex) {
    return;
  }

  if (!ifRequestDuplicate(op.ClientId, op.RequestId)) {
    if (op.Operation == "Put") {
      ExecutePutOpOnKVDB(op);
    }
    if (op.Operation == "Append") {
      mjb hnh ExecuteAppendOpOnKVDB(op);
    }
  }

  if (m_maxRaftState != -1) {
    IfNeedToSendSnapShotCommand(message.CommandIndex, 9);
  }

  SendMessageToWaitChan(op, message.CommandIndex);
}

// 幂等性检测函数，用于判断客户端的请求是否是重复提交
bool KvServer::ifRequestDuplicate(std::string ClientId, int RequestId) {
  std::lock_guard<std::mutex> lg(m_mtx);
  if (m_lastRequestId.find(ClientId) == m_lastRequestId.end()) {
    return false;
    // todo :不存在这个client就创建
  }
  return RequestId <= m_lastRequestId[ClientId];
}

// 处理客户端 Put/Append 请求的核心函数
void KvServer::PutAppend(const raftKVRpcProctoc::PutAppendArgs *args, raftKVRpcProctoc::PutAppendReply *reply) {
  Op op;
  op.Operation = args->op();
  op.Key = args->key();
  op.Value = args->value();
  op.ClientId = args->clientid();
  op.RequestId = args->requestid();
  int raftIndex = -1;
  int _ = -1;
  bool isleader = false;
  // Start() 是 Raft 提交命令接口
  m_raftNode->Start(op, &raftIndex, &_, &isleader);

  if (!isleader) {
    reply->set_err(ErrWrongLeader);
    return;
  }

  m_mtx.lock();

  if (waitApplyCh.find(raftIndex) == waitApplyCh.end())
    waitApplyCh.insert(std::make_pair(raftIndex, new LockQueue<Op>()));
  auto chForRaftIndex = waitApplyCh[raftIndex];

  m_mtx.unlock();  // 直接解锁，等待任务执行完成，不能一直拿锁等待

  Op raftCommitOp;
  if (!chForRaftIndex->timeOutPop(CONSENSUS_TIMEOUT, &raftCommitOp)) {
    if (ifRequestDuplicate(op.ClientId, op.RequestId))
      reply->set_err(OK);  // 超时了,但因为是重复的请求，返回ok，实际上就算没有超时，在真正执行的时候也要判断是否重复
    else
      reply->set_err(ErrWrongLeader);  // 这里返回这个的目的让clerk重新尝试

  } else {
    if (raftCommitOp.ClientId == op.ClientId && raftCommitOp.RequestId == op.RequestId)
      // 可能发生leader的变更导致日志被覆盖，因此必须检查
      reply->set_err(OK);
    else
      reply->set_err(ErrWrongLeader);
  }
  m_mtx.lock();

  auto tmp = waitApplyCh[raftIndex];
  waitApplyCh.erase(raftIndex);
  delete tmp;
  m_mtx.unlock();
}

// 持续监听 Raft 提交的命令并交给状态机处理
void KvServer::ReadRaftApplyCommandLoop() {
  while (true) {
    // 如果只操作applyChan不用拿锁，因为applyChan自己带锁
    // applyChan 是 Raft 层与状态机之间的通信桥梁
    auto message = applyChan->pop();  // 阻塞式获取 Raft 提交的日志

    DPrintf(
        "---------------tmp-------------[func-KvServer::ReadRaftApplyCommandLoop()-kvserver{%d}] 收到了下raft的消息",
        m_me);

    if (message.CommandValid) {
      GetCommandFromRaft(message);  // 日志处理
    }
    if (message.SnapchotValid) {
      GetSnapShotFromRaft(message);  // 快照处理
    }
  }
}

/*
raft会与persist层交互，kvserver层也会，因为kvserver层开始的时候需要恢复kvdb的状态
关于快照raft层与persist的交互：保存kvserver传来的snapshot；生成leaderInstallSnapshot RPC的时候也需要读取snapshot；
因此snapshot的具体格式是由kvserver层来定的，raft只负责传递这个东西
snapShot里面包含kvserver需要维护的persist_lastRequestId 以及kvDB真正保存的数据persist_kvdb

是 KVServer 从快照恢复状态的函数，用于从 Raft/persister 提供的 snapshot 数据中恢复出 KV 数据和客户端请求记录
快照中包含的内容：
map<string, string> kvDB;                  // 实际的键值数据库
map<string, int> lastRequestId;            // 每个客户端的最大请求编号
*/
void KvServer::ReadSnapShotToInstall(std::string snapshot) {
  if (snapshot.empty()) {
    return;
  }
  parseFromString(snapshot);
}

// 向等待通道 waitApplyCh 中投递 Raft 日志提交后的命令，以便唤醒正在等待这个日志被 commit 的 RPC 处理线程
bool KvServer::SendMessageToWaitChan(const Op &op, int raftIndex) {
  std::lock_guard<std::mutex> lg(m_mtx);  // 自动加锁并在作用域结束时自动释放锁
  DPrintf(
      "[RaftApplyMessageSendToWaitChan--> raftserver{%d}] , Send Command --> Index:{%d} , ClientId {%d}, RequestId "
      "{%d}, Opreation {%v}, Key :{%v}, Value :{%v}",
      m_me, raftIndex, &op.ClientId, op.RequestId, &op.Operation, &op.Key, &op.Value);
  // 多个线程可能同时访问 waitApplyCh
  if (waitApplyCh.find(raftIndex) == waitApply.end()) {
    return false;
  }
  waitApplyCh[raftIndex]->Push(op);
  DPrintf(
      "[RaftApplyMessageSendToWaitChan--> raftserver{%d}] , Send Command --> Index:{%d} , ClientId {%d}, RequestId "
      "{%d}, Opreation {%v}, Key :{%v}, Value :{%v}",
      m_me, raftIndex, &op.ClientId, op.RequestId, &op.Operation, &op.Key, &op.Value);
  return true;
}

// 判断是否需要生成快照（snapshot），并触发 Raft 的快照机制
void KvServer::IfNeedToSendSnapShotCommand(int raftIndex, int proportion) {
  /*
  m_maxRaftState：允许的最大 Raft 状态大小（通常由上层用户配置）。
  GetRaftStateSize()：返回当前 Raft 状态机占用的存储空间（包括日志、持久化状态等）。
  如果当前 Raft 状态超过最大值的一定比例（默认是 1/10），就需要 压缩日志、生成快照。*/
  if (m_raftNode->GetRaftStateSize() > m_maxRaftState / 10.0) {
    auto snapshot = MakeSnapShot();
    m_raftNode->Snapshot(raftIndex, snapshot);
  }
}

// 处理 Raft 层传来的快照消息
void KvServer::GetSnapShotFromRaft(ApplyMsg message) {
  std::lock_guard<std::mutex> lg(m_mtx);

  if (m_raftNode->CondInstallSnapshot(message.SnapshotTerm, message.SnapshotIndex, message.Snapshot)) {
    ReadSnapShotToInstall(message.Snapshot);
    m_lastSnapShotRaftLogIndex = message.SnapshotIndex;
  }
}

// 生成当前系统状态快照（snapshot）数据的函数
std::string KvServer::MakeSnapShot() {
  std::lock_guard<std::mutex> lg(m_mtx);

  std::string snapshotData = getSnapshotData();
  return snapshotData;
}

// 作为 gRPC 框架调用时的服务接口入口，将网络请求转发给内部逻辑函数执行，并在完成后通知 RPC 系统调用完成
void KvServer::PutAppend(google::protobuf::RpcController *controller, const ::raftKVRpcProctoc::PutAppendArgs *request,
                         ::raftKVRpcProctoc::PutAppendReply *response, ::google::protobuf::Closure *done) {
  KvServer::PutAppend(request, response);
  done->Run();
}

void KvServer::Get(google::protobuf::RpcController *controller, const ::raftKVRpcProctoc::GetArgs *request,
                   ::raftKVRpcProctoc::GetReply *response, ::google::protobuf::Closure *done) {
  KvServer::Get(request, response);
  done->Run();
}

/*
构造函数:实现了一个 基于 Raft 的分布式 KV 存储节点的完整初始化流程
me                  : 当前节点编号
maxraftstate        : 日志状态最大上限（超过触发 snapshot）
nodeInforFileName   : 配置文件路径（包含所有节点 IP/端口）
port                : 当前节点监听端口号
*/
KvServer::KvServer(int me, int maxraftstate, std::string nodeInforFileName, short port) : m_skipList(6) {
  std::shared_ptr<Persister> persister = std::make_shared<Persister>(me);

  m_me = me;
  m_maxRaftState = maxraftstate;
  applyChan = std::make_shared<LockQueue<ApplyMsg> >();
  m_raftNode = std::make_shared<Raft>();

  // 启动 RPC 服务（并注册两个服务：KVServer 和 Raft）
  std::thread t([this, port]() -> void {
    // provider是一个rpc网络服务对象。把UserService对象发布到rpc节点上
    RpcProvider provider;
    provider.NotifyService(this);
    provider.NotifyService(
        this->m_raftNode.get());  // todo：这里获取了原始指针，后面检查一下有没有泄露的问题 或者 shareptr释放的问题
    // 启动一个 rpc 监听
    provider.Run(m_me, port);
  });
  t.detach();  // 线程监听

  // 开启rpc远程调用能力，必须要保证所有节点都开启rpc接受功能之后才能开启rpc远程调用能力
  // 这里使用睡眠来保证
  sleep(6);
  // 获取所有raft节点ip、port ，并进行连接 ,要排除自己
  MprpcConfig config;
  config.LoadConfigFile(nodeInforFileName.c_str());

  std::vector<std::pair<std::string, short> > ipPortVt;
  for (int i = 0; i < INT_MAX - 1; ++i) {
    std::string node = "node" + std::to_string(i);

    std::string nodeIp = config.Load(node + "ip");
    std::string nodePortStr = config.Load(node + "port");
    if (nodeIp.empty()) break;

    ipPortVt.emplace_back(nodeIp, atoi(nodePortStr.c_str()));
  }
  std::vector<std::shared_ptr<RaftRpcUtil> > servers;
  // 进行连接
  for (int i = 0; i < ipPortVt.size(); ++i) {
    if (i == m_me) {
      servers.push_back(nullptr);
      continue;
    }
    std::string otherNodeIp = ipPortVt[i].first;
    short otherNodePort = ipPortVt[i].second;
    auto *rpc = new RaftRpcUtil(otherNodeIp, otherNodePort);
    servers.push_back(std::shared_ptr<RaftRpcUtil>(rpc));

    std::cout << "node" << m_me << " 连接node" << i << "success!" << std::endl;
  }
  sleep(ipPortVt.size() - me);                            // 等待所有节点相互连接成功
  m_raftNode->init(servers, m_me, persister, applyChan);  // 再启动raft

  // kv的server直接与raft通信，但kv不直接与raft通信，所以需要把ApplyMsg的chan传递下去用于通信，两者的persist也是共用的
  m_skipList;
  waitApplyCh;
  m_lastRequestId;
  m_lastSnapShotRaftLogIndex = 0;

  // 如果 persister 中存在快照（说明曾经运行过），恢复 kvDB 和 lastRequestId
  auto snapshot = persister->ReadSnapshot();
  if (!snapshot.empty()) ReadSnapShotToInstall(snapshot);

  // 启动一个新线程 t2，让它执行当前对象的 ReadRaftApplyCommandLoop() 成员函数
  std::thread t2(&KvServer::ReadRaftApplyCommandLoop, this);  // 马上向其他节点宣告自己就是leader
  t2.join();                                                  // 阻塞
}