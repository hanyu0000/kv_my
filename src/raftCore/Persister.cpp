#include "Persister.h"
#include "util.h"

// 同时保存 Raft 状态和快照，写入文件，清空旧数据
void Persister::Save(const std::string raftstate, const std::string snapshot) {
  std::lock_guard<std::mutex> lg(m_mtx);
  clearRaftStateAndSnapshot();
  // 将raftstate和snapshot写入本地文件
  m_raftStateOutStream << raftstate;
  m_snapshotOutStream << snapshot;
}

// 从文件中读取快照内容
std::string Persister::ReadSnapshot() {
  std::lock_guard<std::mutex> lg(m_mtx);
  if (m_snapshotOutStream.is_open()) m_snapshotOutStream.close();

  DEFER {
    m_snapshotOutStream.open(m_snapshotFileName);  // 默认是追加
  };
  std::fstream ifs(m_snapshotFileName, std::ios_base::in);
  if (!ifs.good()) {
    return "";
  }
  std::string snapshot;
  ifs >> snapshot;
  ifs.close();
  return snapshot;
}

// 单独保存 Raft 状态，更新状态大小
void Persister::SaveRaftState(const std::string &data) {
  std::lock_guard<std::mutex> lg(m_mtx);
  // 将raftstate和snapshot写入本地文件
  clearRaftState();
  m_raftStateOutStream << data;
  m_raftStateSize += data.size();
}

// 返回当前持久化的 Raft 状态数据大小
long long Persister::RaftStateSize() {
  std::lock_guard<std::mutex> lg(m_mtx);
  return m_raftStateSize;
}

// 从文件中读取 Raft 状态内容
std::string Persister::ReadRaftState() {
  std::lock_guard<std::mutex> lg(m_mtx);

  std::fstream ifs(m_raftStateFileName, std::ios_base::in);
  if (!ifs.good()) return "";

  std::string snapshot;
  ifs >> snapshot;
  ifs.close();
  return snapshot;
}

// 清空 Raft 状态对应的文件
void Persister::clearRaftState() {
  m_raftStateSize = 0;
  // 关闭文件流
  if (m_raftStateOutStream.is_open()) {
    m_raftStateOutStream.close();
  }
  // 重新打开文件流并清空文件内容
  m_raftStateOutStream.open(m_raftStateFileName, std::ios::out | std::ios::trunc);
}

// 清空快照文件
void Persister::clearSnapshot() {
  if (m_snapshotOutStream.is_open()) {
    m_snapshotOutStream.close();
  }
  m_snapshotOutStream.open(m_snapshotFileName, std::ios::out | std::ios::trunc);
}

// 同时清空 Raft 状态和快照
void Persister::clearRaftStateAndSnapshot() {
  clearRaftState();
  clearSnapshot();
}

// 构造函数:创建两个独立的文件来存储本节点的日志状态和快照
Persister::Persister(const int me)
    : m_raftStateFileName("raftstatePersist" + std::to_string(me) + ".txt"),
      m_snapshotFileName("snapshotPersist" + std::to_string(me) + ".txt"),
      m_raftStateSize(0) {
  // 检查文件状态并清空文件
  bool fileOpenFlag = true;
  std::fstream file(m_raftStateFileName, std::ios::out | std::ios::trunc);
  if (file.is_open()) {
    file.close();
  } else {
    fileOpenFlag = false;
  }

  file = std::fstream(m_snapshotFileName, std::ios::out | std::ios::trunc);

  if (file.is_open()) {
    file.close();
  } else {
    fileOpenFlag = false;
  }

  if (!fileOpenFlag) {
    DPrintf("[func-Persister::Persister] file open error");
  }
  // 绑定流
  m_raftStateOutStream.open(m_raftStateFileName);
  m_snapshotOutStream.open(m_snapshotFileName);
}

// 析构时关闭所有打开的文件流
Persister::~Persister() {
  if (m_raftStateOutStream.is_open()) {
    m_raftStateOutStream.close();
  }
  if (m_snapshotOutStream.is_open()) {
    m_snapshotOutStream.close();
  }
}