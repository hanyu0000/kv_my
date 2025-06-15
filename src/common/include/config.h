#ifndef CONFIG_H
#define CONFIG_H

const bool Debug = true;
// 时间单位系数：用于调整不同网络环境下的时间参数,不同网络环境RPC速度不同，因此需要乘以一个系数来调整
const int debugMul = 1;
const int HeartBeatTimeout = 25 * debugMul;  // 心跳时间一般要比选举超时小一个数量级
const int ApplyInterval = 10 * debugMul;

// 最小随机选举超时时间：Raft算法中的随机选举超时范围
const int minRandomizedElectionTime = 300 * debugMul;
// 最大随机选举超时时间：Raft算法中的随机选举超时范围
const int maxRandomizedElectionTime = 500 * debugMul;
// 共识超时时间：达成共识的最大等待时间
const int CONSENSUS_TIMEOUT = 500 * debugMul;

// 协程相关设置
const int FIBER_THREAD_NUM = 1;              // 协程库中线程池大小
const bool FIBER_USE_CALLER_THREAD = false;  // 是否使用caller_thread执行调度任务

#endif  // CONFIG_H