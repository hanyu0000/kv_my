基于本项目（Raft + KV + RPC + 协程）的代码实现

一、总体与架构
1) 你们系统要解决什么问题？为什么选 Raft？
A: 提供强一致、容错的分布式 KV 存储。
- 强一致：所有节点按相同顺序应用命令（日志复制 + 提交索引）。
- 容错：少数节点故障不影响对外可用性（多数派原则）。
- 选 Raft：
  - 易实现/讲解：Leader 驱动，状态机清晰；比 Paxos 更工程化。
  - 安全性：任期、投票、一致性检查（日志新旧度）确保不会出现“旧 leader 覆盖新日志”。
  - 快照：天然支持日志压缩。对应代码：`src/raftCore/raft.cpp`、`src/raftCore/include/raft.h`。

2) 架构分层？核心模块职责？
A: 三层：
- 应用/状态机：`KvServer`
  - 存储：跳表 `m_skipList`（插入/查找）。
  - 幂等：`m_lastRequestId`（ClientId→最后请求号）。
  - 日志应用：`ReadRaftApplyCommandLoop()` 拉取 `ApplyMsg`，`GetCommandFromRaft()` 反序列化 `Op` 并落地。
- 共识层：`Raft`
  - 领导者选举、日志复制、提交推进（`commitIndex`）。
  - 向上投递：通过 `applyChan` 通知状态机应用日志或安装快照。
- 基础设施：
  - RPC：`rpcprovider`（服务端注册），`mprpcchannel`（客户端调用）。
  - 协程/IO：`scheduler`、`iomanager`、`timer`、`hook` 提升并发与 IO 效率。

二、请求与数据流
3) 写请求（Put/Append）如何流转？
A: Clerk→KvServer RPC→封装 `Op`（`util.h` Boost 序列化）→`Raft::Start(op.asString())`→日志复制→提交后 `ApplyMsg` 进入 `applyChan`→`KvServer::ReadRaftApplyCommandLoop()` 取出→`GetCommandFromRaft()` 幂等校验→应用到跳表→唤醒等待 RPC 线程。

4) 读请求（Get）如何保证一致性？
A: Demo 版本直接查跳表。若严格线性一致读，可采用读索引或将读取经由日志（本项目以简化路径为主，面试可说明可扩展为 ReadIndex）。

三、存储与快照
5) 为什么用跳表？复杂度是什么？
A: 实现简单、随机化层级，平均 O(log n) 的插入/查找/删除，常用于内存 KV。
- 代码位置：`src/skipList/include/skipList.h`，接口：`insert_set_element()`、`search_element()`、`display_list()`。
- 与有序结构（红黑树等）性能接近，便于序列化/反序列化（本项目通过 dump/load 文件格式）。

6) 快照怎么做？恢复流程是什么？
A: 应用层定义格式，Raft 负责存取与分发。
- 制作：`KvServer::getSnapshotData()`
  - 将跳表序列化为中间字符串：`m_skipList.dump_file()` → `m_serializedKVData`。
  - 用 Boost.Serialization 将 `{m_serializedKVData, m_lastRequestId}` 打包到 snapshot。
- 触发：`IfNeedToSendSnapShotCommand(raftIndex, proportion)` 当 `m_maxRaftState` 达到阈值时，向 Raft 下发 snapshot 指令。
- 保存/读取：`Persister::Save(raftState, snapshot)`、`ReadSnapshot()`、`ReadRaftState()`（文件后端）。
- 恢复：`parseFromString()` 反序列化 `m_serializedKVData`，再 `m_skipList.load_file()` 重建内存结构，同时恢复 `m_lastRequestId`。
- 防重应用：`m_lastSnapShotRaftLogIndex` 防止对快照覆盖范围内的日志重复 apply。

四、RPC 与序列化
7) RPC 框架怎么做的？
A: 自研、类 gRPC 结构。
- Provider：服务发布，映射方法到 handler（`src/rpc/rpcprovider.cpp`）。
- Channel：客户端 stub 调用入口，封装序列化、发送与回包（`src/rpc/mprpcchannel.cpp`）。
- Controller：记录错误码、超时等（`src/rpc/mprpccontroller.cpp`）。
- 协议：protobuf 生成的头与数据体，附加自定义 `rpcheader` 作长度/方法路由（`src/rpc/rpcheader.proto`）。
- 示例：`example/rpcExample/` 中 caller/callee 展示服务发布与远程调用。

8) 命令如何序列化？
A: `src/common/include/util.h` 的 `class Op`
- 字段：`Operation/Key/Value/ClientId/RequestId`
- 接口：`asString()`（text_oarchive）/`parseFromString()`（text_iarchive）
- 嵌入：Raft 层 `Start()` 走 `std::string` 载体，便于网络传输与持久化。

五、协程与 IO
9) 为什么协程？调度如何实现？
A: 
- 轻量：创建/切换成本低；适配大量并发连接。
- 调度：`scheduler` 维护可运行队列；`iomanager` 基于 epoll 管理读写事件，触发协程恢复；`timer` 处理定时任务；`hook` 将阻塞 IO 改造成非阻塞配合调度。
- 参考：`src/fiber/`（`iomanager.cpp`、`scheduler.cpp`、`hook.cpp`、`timer.cpp`）。

10) 上下文切换和线程模型？
A: 用户态上下文切换（ucontext/汇编）、N:M 模型（多线程 + 每线程多个协程）。
- 优势：减少内核态切换；遇到 IO 等待时不阻塞线程，由 `iomanager` 统一唤醒。
- 注意：避免长时间 CPU 任务阻塞协程，建议切分任务或下放线程池。

六、一致性与并发
11) 幂等如何保证？
A: 
- 结构：`std::unordered_map<std::string,int> m_lastRequestId`。
- 路径：`GetCommandFromRaft()` 中先判重（`ifRequestDuplicate`），再执行 `ExecutePutOpOnKVDB/ExecuteAppendOpOnKVDB`。
- 细节：无论成功或失败，只要成为日志并 commit，就会记录最新 `RequestId`，避免重放写入。

12) 并发安全怎么做？
A: 
- 细粒度互斥：对 `m_skipList`、`waitApplyCh`、`m_lastRequestId` 操作加锁。
- 通信解耦：`LockQueue<ApplyMsg>` 串行化 apply；`timeOutPop` 支持超时等待，避免死锁。
- 读路径：Get 直接查跳表，避免进入 Raft 热路径（若需强读，采用 ReadIndex）。

七、故障与恢复
13) 节点宕机如何恢复？网络分区如何处理？
A: 
- 恢复：重启后读取 `ReadRaftState()` 恢复 term、vote 等元数据；读取 `ReadSnapshot()` 恢复状态机，再回放快照点之后的日志。
- 分区：少数派无法赢得选举；Leader 与 Follower 以 term 对齐，冲突日志按“任期+索引”裁剪修复。

14) 脑裂如何避免？
A: 
- 多数派原则 + 任期号单调递增。
- 客户端写入仅发送给自认为的 leader；若返回 `ErrWrongLeader`，Clerk 会轮询下一节点重试（`raftClerk/clerk.cpp`）。

八、性能与优化
15) 性能瓶颈与优化思路？
A: 
- 快照：
  - 触发阈值合理化（`m_maxRaftState`），避免频繁/过大；
  - 异步生成与分块写；
  - 压缩与去重（视序列化格式）。
- 复制：
  - AppendEntries 流水线化；
  - 批量发送，减少系统调用与网络拥塞。
- 读取：
  - ReadIndex 强读避免写日志；
  - 热点缓存（如只读快路径）。
- 观测性：提交延迟、复制滞后、失败率监控，驱动自适应调整。

九、代码细节可被问到的点（对应本仓库变更）
16) 你做了哪些健壮性修复？
A: 统一 `m_lastRequestId` 命名，修复 `exist` 判断拼写，移除异常字符，删除未用成员 `m_kvDB`，修正 CMake 聚合库与未定义变量引用，新增最小化构建开关，拆分示例可选构建。

十、追问与延伸
17) 如何实现线性一致读？
A: 增加 ReadIndex：leader 读取自身 commitIndex 并经心跳确认已被多数派知晓，随后直接从状态机读取返回。

18) 如何支持成员变更/分片？
A: 成员变更可用 Joint Consensus；分片需在 KV 层引入 shard/路由与多 Raft group 协调。

十九、你可以快速定位到的关键代码
- 快照打包/恢复：`src/raftCore/include/kvServer.h` 中 `getSnapshotData()/parseFromString()`
- 应用命令：`src/raftCore/kvServer.cpp` 中 `ReadRaftApplyCommandLoop()/GetCommandFromRaft()`
- 落盘：`src/raftCore/Persister.cpp` 中 `Save()/ReadSnapshot()/ReadRaftState()`
- 命令序列化：`src/common/include/util.h` 中 `class Op`

准备面试的小提示：
- 口述一遍写/读/快照/恢复全链路；
- 预判追问（ReadIndex、成员变更、快照开销）并给出工程化方案；
- 用本仓库函数名回答，能快速建立可信度。