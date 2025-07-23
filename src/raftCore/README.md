### 用 C++ 实现的 Raft 共识算法 

### CMakeLists.txt
项目的构建配置文件，用于定义编译规则、依赖关系、生成可执行文件或库。

### include 目录
- ApplyMsg.h

定义 Raft 状态机应用层的消息结构。Raft Leader 将日志条目提交给状态机时，会通过这种消息传递。

- kvServer.h

可能是一个基于 Raft 实现的键值存储服务器的接口声明，负责处理客户端请求，维护状态机。

- Persister.h

持久化模块的接口定义，负责保存 Raft 节点的持久状态（如日志、当前任期等）到稳定存储。

- raft.h

Raft 算法的核心接口和数据结构声明，如节点状态、日志条目、RPC 方法等。

- raftRpc.h

Raft 相关的远程过程调用（RPC）接口定义，用于节点之间通信（AppendEntries、RequestVote等）。

### 源代码文件
- kvServer.cpp

实现基于 Raft 的键值服务器逻辑，处理客户端读写请求，调用 Raft 模块达成共识。

- Persister.cpp

持久化逻辑的具体实现，保存和加载 Raft 状态数据。

- raft.cpp

Raft 算法的具体实现，包括状态机逻辑、领导选举、日志复制等。

- raftRpc.cpp

Raft RPC 接口实现，负责网络通信部分（可能基于 RPC 框架或自定义协议）。

这个项目整体是一个 Raft 共识算法的实现，并基于它构建了一个简单的键值存储服务器。它支持节点间通过 RPC 通信达成日志一致性，持久化状态以保证可靠性，并且通过 ApplyMsg 机制将日志应用到状态机。