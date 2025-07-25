#include "raft.h"
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <memory>
#include "config.h"
#include "util.h"

// Raft 算法中 Leader 向 Follower 发送 AppendEntries RPC 请求时，Follower 的处理函数
void Raft::AppendEntries1(const raftRpcProctoc::AppendEntriesArgs* args, raftRpcProctoc::AppendEntriesReply* reply) {
  std::lock_guard<std::mutex> locker(m_mtx);
  reply->set_appstate(AppNormal);  // 设置应用状态为正常（表明网络通畅）

  // 检查 Leader 的 term 合法性
  // 请求中的 term 比自己旧，说明这个 Leader 是“过期”的，直接拒绝
  // 不应该重置选举超时器，因为是过期 leader
  if (args->term() < m_currentTerm) {
    reply->set_success(false);
    reply->set_term(m_currentTerm);
    reply->set_updatenextindex(-100);
    return;
  }

  DEFER { persist(); }  // 由于这个局部变量创建在锁之后，因此执行persist的时候应该也是拿到锁的

  if (args->term() > m_currentTerm) {
    m_status = Follower;
    m_currentTerm = args->term();
    m_votedFor = -1;  // 清空 votedFor（允许重新投票）
  }

  myAssert(args->term() == m_currentTerm, format("assert {args.Term == rf.currentTerm} fail"));

  m_status = Follower;  // 即使 term 相同也要转为 follower
  m_lastResetElectionTime = now();

  if (args->prevlogindex() > getLastLogIndex()) {  // Leader 声称的前一条日志太新，Follower 无法接上 → 拒绝
    reply->set_success(false);
    reply->set_term(m_currentTerm);
    reply->set_updatenextindex(getLastLogIndex() + 1);
    return;
  } else if (args->prevlogindex() <
             m_lastSnapshotIncludeIndex) {  // Leader 的 prevLogIndex 太老，甚至已经被快照覆盖 → 无法匹配，拒绝
    reply->set_success(false);
    reply->set_term(m_currentTerm);
    reply->set_updatenextindex(m_lastSnapshotIncludeIndex() + 1);
  }

  if (matchLog(args->prevlogindex(), args->prevlogterm())) {
    for (int i = 0; i < args->entries_size(); i++) {
      auto log = args->entries(i);
      if (log.logindex() > getLastLogIndex()) {
        // 超过就直接添加日志
        m_logs.push_back(log);
      } else {
        // 已有日志项 → 检查 term 是否相同，不同则覆盖（不直接截断）
        if (m_logs[getSlicesIndexFromLogIndex(log.logindex())].logterm() == log.logterm() &&
            m_logs[getSlicesIndexFromLogIndex(log.logindex())].command() != log.command()) {
          myAssert(false, format("[func-AppendEntries-rf{%d}] 两节点logIndex{%d}和term{%d}相同，但是其command{%d:%d}   "
                                 " {%d:%d}却不同！！\n",
                                 m_me, log.logindex(), log.logterm(), m_me,
                                 m_logs[getSlicesIndexFromLogIndex(log.logindex())].command(), args->leaderid(),
                                 log.command()));
        }
        if (m_logs[getSlicesIndexFromLogIndex(log.logindex())].logterm() != log.logterm()) {
          // 不匹配就更新
          m_logs[getSlicesIndexFromLogIndex(log.logindex())] = log;
        }
      }
    }

    myAssert(
        getLastLogIndex() >= args->prevlogindex() + args->entries_size(),
        format("[func-AppendEntries1-rf{%d}]rf.getLastLogIndex(){%d} != args.PrevLogIndex{%d}+len(args.Entries){%d}",
               m_me, getLastLogIndex(), args->prevlogindex(), args->entries_size()));

    if (args->leadercommit() > m_commitIndex) {
      m_commitIndex = std::min(args->leadercommit(), getLastLogIndex());
      // commitIndex 只能“跟随” Leader 的 commitIndex，但不能超过自己最后一条日志
    }

    // 领导会一次发送完所有的日志
    myAssert(getLastLogIndex() >= m_commitIndex,
             format("[func-AppendEntries1-rf{%d}]  rf.getLastLogIndex{%d} < rf.commitIndex{%d}", m_me,
                    getLastLogIndex(), m_commitIndex));

    // 设置响应成功
    reply->set_success(true);
    reply->set_term(m_currentTerm);
    return;
  } else {
    // 如果日志不匹配，Follower 优化地返回 Leader 一个 updateNextIndex
    reply->set_updatenextindex(args->prevlogindex());
    for (int index = args->prevlogindex(); index >= m_lastSnapshotIncludeIndex; --index) {
      if (getLogTermFromLogIndex(index) != getLogTermFromLogIndex(args->prevlogindex())) {
        reply->set_updatenextindex(index + 1);
        break;
      }
    }
    reply->set_success(false);
    reply->set_term(m_currentTerm);
    return;
  }
}

// Raft 协议中的 日志应用线程 applierTicker() 的实现
// 这个函数运行在一个独立线程中，作用是：将 Raft 日志中已经被 commit 的部分，通过 applyChan
// 传给上层状态机（如kvserver）进行应用
void Raft::applierTicker() {
  while (true) {
    // 必须拿锁，不拿锁的话如果调用多次applyLog函数，可能会导致应用的顺序不一样
    m_mtx.lock();
    auto applyMsgs = getApplyLogs();  // 获取未应用的已提交日志
    m_mtx.unlock();

    // 向状态机推送 applyMsg
    if (!applyMsgs.empty()) {
      DPrintf("[func- Raft::applierTicker()-raft{%d}] 向kvserver报告的applyMsgs长度为：{%d}", m_me, applyMsgs.size());
    }
    for (auto& message : applyMsgs) {
      applyChan->Push(message);
    }

    // 睡眠一段时间，避免 CPU 空转，避免 busy wait，提高系统性能
    sleepNMilliseconds(ApplyInterval);
  }
}

// 上层kvserver收到 Leader 发来的快照，它会调用此函数，询问 Raft 是否可以接受该快照并丢弃旧日志
bool Raft::CondInstallSnapshot(int lastIncludedTerm, int lastIncludedIndex, std::string snapshot) {
  std::lock_guard<std::mutex> lock(m_mtx);

  DPrintf("[CondInstallSnapshot-rf{%d}] Called with index %d, term %d. current commitIndex: %d, lastApplied: %d", m_me,
          lastIncludedIndex, lastIncludedTerm, m_commitIndex, m_lastApplied);

  // 快照落后，拒绝安装（幂等性防御）
  if (lastIncludedIndex <= m_commitIndex) {
    return false;
  }

  // 丢弃日志：如果快照 index 比现有日志最后 index 大，清空日志
  if (lastIncludedIndex > getLastLogIndex()) {
    m_logs.clear();
  } else {
    // 部分日志可以保留（保留 lastIncludedIndex 之后的部分）
    m_logs.erase(m_logs.begin() + getSlicesIndexFromLogIndex(lastIncludedIndex + 1), m_logs.end());
  }

  // 更新 snapshot 元信息
  m_lastSnapshotIncludeIndex = lastIncludedIndex;
  m_lastSnapshotIncludeTerm = lastIncludedTerm;

  // 关键更新：lastApplied 与 commitIndex 必须同步推进
  m_lastApplied = lastIncludedIndex;
  m_commitIndex = lastIncludedIndex;

  // 同步持久化（快照 + 状态）
  persistStateAndSnapshot(snapshot);

  return true;
}

// 选举逻辑——当节点不是 Leader 且选举定时器超时后，触发 doElection() 开始新一轮选举
void Raft::doElection() {
  std::lock_guard<std::mutex> g(m_mtx);

  if (m_status == Leader) {
    return;  // 如果已经是 Leader，就不用选举了
  }

  // 转为 Candidate 发起选举
  if (m_status != Leader) {
    DPrintf("[       ticker-func-rf(%d)              ]  选举定时器到期且不是leader，开始选举 \n", m_me);

    m_status = Candidate;
    m_currentTerm += 1;  // 新一轮选举，任期递增
    m_votedFor = m_me;   // 自己给自己投票
    persist();           // 保存当前 term 和 votedFor 状态到磁盘

    // 用 shared_ptr 包装 votedNum 是因为后续线程要共享修改它
    std::shared_ptr<int> votedNum = std::make_shared<int>(1);  // 自己已投一票，使用 make_shared 函数初始化
    m_lastResetElectionTime = now();                           // 重置选举定时器，防止马上再次超时触发选举

    // 向所有其他节点并发发送投票请求
    for (int i = 0; i < m_peers.size(); i++) {
      if (i == m_me) continue;  // 排除自己

      int lastLogIndex = -1, LastLogTerm = -1;
      getLastLogIndexAndTerm(&lastLogIndex, &LastLogTerm);

      std::shared_ptr<raftRpcProctoc::RequestVoteArgs> requestVoteArgs =
          std::make_shared<raftRpcProctoc::RequestVoteArgs>();

      requestVoteArgs->set_term(m_currentTerm);
      requestVoteArgs->set_candidateid(m_me);
      requestVoteArgs->set_lastlogindex(lastLogIndex);
      requestVoteArgs->set_lastlogterm(lastLogTerm);
      auto requestVoteReply = std::make_shared<raftRpcProctoc::RequestVoteReply>();

      /*
        this: 当前 Raft 实例；
        i: 对端节点编号；
        requestVoteArgs: 当前投票请求参数；
        requestVoteReply: 等待填充的回复；
        votedNum: 所有投票线程共享的已得票数（线程安全需小心！通常还需要 mutex 包装）。
    */
      std::thread t(&Raft::sendRequestVote, this, i, requestVoteArgs, requestVoteReply, votedNum);
      t.detach();
    }
  }
}

// Leader 节点定期发送心跳或日志
// 如果当前节点是 Leader，就向所有 Follower 节点发送 AppendEntries RPC，表示“我还活着”或附带日志同步信息
void Raft::doHeartBeat() {
  std::lock_guard<std::mutex> g(m_mtx);

  if (m_status == Leader) {
    DPrintf("[func-Raft::doHeartBeat()-Leader: {%d}] Leader的心跳定时器触发了且拿到mutex，开始发送AE\n", m_me);
    auto appendNums = std::make_shared<int>(1);  // appendNums 用于记录收到成功响应的节点数

    // 遍历所有 Follower，构造并发送AE
    for (int i = 0; i < m_peers.size(); i++) {
      if (i == m_me) continue;

      DPrintf("[func-Raft::doHeartBeat()-Leader: {%d}] Leader的心跳定时器触发了 index:{%d}\n", m_me, i);
      myAssert(m_nextIndex[i] >= 1, format("rf.nextIndex[%d] = {%d}", i, m_nextIndex[i]));

      // 判断是否要发送快照
      if (m_nextIndex[i] <= m_lastSnapshotIncludeIndex) {
        std::thread t(&Raft::leaderSendSnapShot, this, i);  // 创建新线程并执行b函数，并传递参数
        t.detach();
        continue;
      }

      // 构造发送值
      int preLogIndex = -1;
      int PrevLogTerm = -1;
      getPrevLogInfo(i, &preLogIndex, &PrevLogTerm);
      std::shared_ptr<raftRpcProctoc::AppendEntriesArgs> appendEntriesArgs =
          std::make_shared<raftRpcProctoc::AppendEntriesArgs>();
      appendEntriesArgs->set_term(m_currentTerm);
      appendEntriesArgs->set_leaderid(m_me);
      appendEntriesArgs->set_prevlogindex(preLogIndex);
      appendEntriesArgs->set_prevlogterm(PrevLogTerm);
      appendEntriesArgs->clear_entries();
      appendEntriesArgs->set_leadercommit(m_commitIndex);

      if (preLogIndex != m_lastSnapshotIncludeIndex) {
        for (int j = getSlicesIndexFromLogIndex(preLogIndex) + 1; j < m_logs.size(); ++j) {
          raftRpcProctoc::LogEntry* sendEntryPtr = appendEntriesArgs->add_entries();
          *sendEntryPtr = m_logs[j];  //=是可以点进去的，可以点进去看下protobuf如何重写这个的
        }
      } else {
        for (const auto& item : m_logs) {
          raftRpcProctoc::LogEntry* sendEntryPtr = appendEntriesArgs->add_entries();
          *sendEntryPtr = item;  //=是可以点进去的，可以点进去看下protobuf如何重写这个的
        }
      }
      int lastLogIndex = getLastLogIndex();
      // leader对每个节点发送的日志长短不一，但是都保证从prevIndex发送直到最后
      myAssert(appendEntriesArgs->prevlogindex() + appendEntriesArgs->entries_size() == lastLogIndex,
               format("appendEntriesArgs.PrevLogIndex{%d}+len(appendEntriesArgs.Entries){%d} != lastLogIndex{%d}",
                      appendEntriesArgs->prevlogindex(), appendEntriesArgs->entries_size(), lastLogIndex));
      // 构造返回值
      const std::shared_ptr<raftRpcProctoc::AppendEntriesReply> appendEntriesReply =
          std::make_shared<raftRpcProctoc::AppendEntriesReply>();
      appendEntriesReply->set_appstate(Disconnected);

      std::thread t(&Raft::sendAppendEntries, this, i, appendEntriesArgs, appendEntriesReply,
                    appendNums);  // 创建新线程并执行b函数，并传递参数
      t.detach();
    }
    m_lastResetHearBeatTime = now();  // leader发送心跳，就不是随机时间了
  }
}

// 如果一个 Raft 节点太久没收到 Leader 的心跳（AppendEntries RPC），就会触发该函数，从而 发起选举成为 Candidate
void Raft::electionTimeOutTicker() {
  while (true) {
    while (m_statue == Leader) {
      usleep(HeartBeatTimeout);
    }

    // 计算距离选举超时还有多长时间
    std::chrono::duration<signed long int, std::ratio<1, 1000000000>> suitableSleepTime{};
    std::chrono::system_clock::time_point wakeTime{};
    {
      m_mtx.lock();
      wakeTime = now();
      suitableSleepTime = getRandomizedElectionTimeout() + m_lastResetElectionTime - wakeTime;
      m_mtx.unlock();
    }

    if (std::chrono::duration<double, std::milli>(suitableSleepTime).count > 1) {
      auto start = std::chrono::steady_clock::now();
      usleep(std::chrono::duration_cast<std::chrono::microseconds>(suitableSleepTime).count());
      auto end = std::chrono::steady_clock::now();
      std::chrono::duration<double, std::milli> duration = end - start;
      // 使用ANSI控制序列将输出颜色修改为紫色
      std::cout << "\033[1;35m electionTimeOutTicker();函数设置睡眠时间为: "
                << std::chrono::duration_cast<std::chrono::milliseconds>(suitableSleepTime).count() << " 毫秒\033[0m"
                << std::endl;
      std::cout << "\033[1;35m electionTimeOutTicker();函数实际睡眠时间为: " << duration.count() << " 毫秒\033[0m"
                << std::endl;
    }
    // 如果这期间收到了 Leader 的心跳，则取消选举
    if (std::chrono::duration<double, std::milli>(m_lastResetElectionTime - wakeTime).count() > 0) continue;
    doElection();
  }
}

std::vector<ApplyMsg> Raft::getApplyLogs() {
  std::vector<ApplyMsg> applyMsgs;
  myAssert(m_commitIndex <= getLastLogIndex(), format("[func-getApplyLogs-rf{%d}] commitIndex{%d} >getLastLogIndex{%d}",
                                                      m_me, m_commitIndex, getLastLogIndex()));

  while (m_lastApplied < m_commitIndex) {
    m_lastApplied++;
    myAssert(m_logs[getSlicesIndexFromLogIndex(m_lastApplied)].logindex() == m_lastApplied,
             format("rf.logs[rf.getSlicesIndexFromLogIndex(rf.lastApplied)].LogIndex{%d} != rf.lastApplied{%d} ",
                    m_logs[getSlicesIndexFromLogIndex(m_lastApplied)].logindex(), m_lastApplied));

    ApplyMsg applyMsg;
    applyMsg.CommandValid = true;
    applyMsg.SnapshotValid = false;
    applyMsg.Command = m_logs[getSlicesIndexFromLogIndex(m_lastApplied)].command();
    applyMsg.CommandIndex = m_lastApplied;
    applyMsgs.emplace_back(applyMsg);
  }
  return applyMsgs;
}

// 获取新命令应该分配的Index
int Raft::getNewCommandIndex() {
  // 如果len(logs)==0,就为快照的index+1，否则为log最后一个日志+1
  auto lastLogIndex = getLastLogIndex();
  return lastLogIndex + 1;
}

// leader调用，传入：服务器index，传出：发送的AE的preLogIndex和PrevLogTerm
void Raft::getPrevLogInfo(int server, int* preIndex, int* preTerm) {
  // logs长度为0返回0,0，不是0就根据nextIndex数组的数值返回
  if (m_nextIndex[server] == m_lastSnapshotIncludeIndex + 1) {
    // 要发送的日志是第一个日志，因此直接返回m_lastSnapshotIncludeIndex和m_lastSnapshotIncludeTerm
    *preIndex = m_lastSnapshotIncludeIndex;
    *preTerm = m_lastSnapshotIncludeTerm;
    return;
  }
  auto nextIndex = m_nextIndex[server];
  *preIndex = nextIndex - 1;
  *preTerm = m_logs[getSlicesIndexFromLogIndex(*preIndex)].logterm();
}

void Raft::GetState(int* term, bool* isLeader) {
  m_mtx.lock();
  DEFER {
    // todo 暂时不清楚会不会导致死锁
    m_mtx.unlock();
  };

  // Your code here (2A).
  *term = m_currentTerm;
  *isLeader = (m_status == Leader);
}

// 当 follower 收到 leader 发送的快照（snapshot）数据时如何处理
void Raft::InstallSnapshot(const raftRpcProctoc::InstallSnapshotRequest* args,
                           raftRpcProctoc::InstallSnapshotResponse* reply) {
  m_mtx.lock();
  DEFER { m_mtx.unlock(); };

  // 判断 term 是否过时
  if (args->term() < m_currentTerm) {
    reply->set_term(m_currentTerm);
    return;
  }

  if (args->term() > m_currentTerm) {
    // 后面两种情况都要接收日志
    m_currentTerm = args->term();
    m_votedFor = -1;
    m_status = Follower;
    persist();  // 表示将当前状态持久化到磁盘（防止崩溃丢失）
  }

  // 重置选举计时器并转换为 follower
  m_status = Follower;
  m_lastResetElectionTime = now();

  // 快照过旧则不处理
  if (args->lastsnapshotincludeindex() <= m_lastSnapshotIncludeIndex) return;

  // 删除旧日志
  auto lastLogIndex = getLastLogIndex();
  if (lastLogIndex > args->lastsnapshotincludeindex())
    m_logs.erase(m_logs.begin(), m_logs.begin() + getSlicesIndexFromLogIndex(args->lastsnapshotincludeindex()) + 1);
  else
    m_logs.clear();

  m_commitIndex = std::max(m_commitIndex, args->lastsnapshotincludeindex());
  m_lastApplied = std::max(m_lastApplied, args->lastsnapshotincludeindex());
  m_lastSnapshotIncludeIndex = args->lastsnapshotincludeindex();
  m_lastSnapshotIncludeTerm = args->lastsnapshotincludeterm();

  // 返回当前 term 给 leader
  reply->set_term(m_currentTerm);
  // 异步通知状态机使用新快照
  ApplyMsg msg;
  msg.SnapshotValid = true;
  msg.Snapshot = args->data();
  msg.SnapshotTerm = args->lastsnapshotincludeterm();
  msg.SnapshotIndex = args->lastsnapshotincludeindex();
  std::thread t(&Raft::pushMsgToKvServer, this, msg);  // 创建新线程并执行b函数，并传递参数
  t.detach();
  // 持久化
  m_persister->Save(persistData(), args->data());
}

void Raft::pushMsgToKvServer(ApplyMsg msg) { applyChan->Push(msg); }

// Leader 定时发送心跳（heartbeat） 的一个后台线程函数
void Raft::leaderHearBeatTicker() {
  while (true) {
    while (m_status != Leader) {
      usleep(1000 * HeartBeatTimeout);
    }
    static std::atomic<int32_t> atomicCount = 0;
    std::chrono::duration<signed long int, std::ratio<1, 1000000000>> suitableSleepTime{};
    std::chrono::system_clock::time_point wakeTime{};
    {
      std::lock_guard<std::mutex> lock(m_mtx);
      wakeTime = now();
      suitableSleepTime = std::chrono::milliseconds(HeartBeatTimeout) + m_lastResetHearBeatTime - wakeTime;
    }

    if (std::chrono::duration<double, std::milli>(suitableSleepTime).count() > 1) {
      std::cout << atomicCount << "\033[1;35m leaderHearBeatTicker();函数设置睡眠时间为: "
                << std::chrono::duration_cast<std::chrono::milliseconds>(suitableSleepTime).count() << " 毫秒\033[0m"
                << std::endl;
      // 获取当前时间点
      auto start = std::chrono::steady_clock::now();
      usleep(std::chrono::duration_cast<std::chrono::microseconds>(suitableSleepTime).count());
      // 获取函数运行结束后的时间点
      auto end = std::chrono::steady_clock::now();
      // 计算时间差并输出结果（单位为毫秒）
      std::chrono::duration<double, std::milli> duration = end - start;

      // 使用ANSI控制序列将输出颜色修改为紫色
      std::cout << atomicCount << "\033[1;35m leaderHearBeatTicker();函数实际睡眠时间为: " << duration.count()
                << " 毫秒\033[0m" << std::endl;
      ++atomicCount;
    }

    if (std::chrono::duration<double, std::milli>(m_lastResetHearBeatTime - wakeTime).count() > 0)
      continue;  // 睡眠的这段时间有重置定时器，没有超时，再次睡眠
    // 真正发送心跳
    doHeartBeat();
  }
}

void Raft::leaderSendSnapShot(int server){}