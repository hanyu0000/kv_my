#include "raft.h"
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <memory>
#include "config.h"
#include "util.h"

void Raft::AppendEntries1(const raftRpcProctoc::AppendEntriesArgs* args, raftRpcProctoc::AppendEntriesReply* reply) {}

void Raft::applierTicker() {}

bool Raft::CondInstallSnapshot(int lastIncludedTerm, int lastIncludedIndex, std::string snapshot) {}

void Raft::doElection() {}

void Raft::doHeartBeat() {}

void Raft::electionTimeOutTicker() {}

void Raft::InstallSnapshot(const raftRpcProctoc::InstallSnapshotRequest* args,
                           raftRpcProctoc::InstallSnapshotResponse* reply) {}

void Raft::pushMsgToKvServer(ApplyMsg msg) { applyChan->Push(msg); }

void Raft::leaderHearBeatTicker() {}

void Raft::leaderSendSnapShot(int server) {}

void Raft::leaderUpdateCommitIndex() {}

bool Raft::matchLog(int logIndex, int logTerm) {}

void Raft::persist() {}

void Raft::RequestVote(const raftRpcProctoc::RequestVoteArgs* args, raftRpcProctoc::RequestVoteReply* reply) {}

bool Raft::UpToDate(int index, int term) {}

void Raft::getLastLogIndexAndTerm(int* lastLogIndex, int* lastLogTerm) {}

int Raft::getLastLogIndex() {
  int lastLogIndex = -1;
  int _ = -1;
  getLastLogIndexAndTerm(&lastLogIndex, &_);
  return lastLogIndex;
}

int Raft::getLastLogTerm() {
  int _ = -1;
  int lastLogTerm = -1;
  getLastLogIndexAndTerm(&_, &lastLogTerm);
  return lastLogTerm;
}
