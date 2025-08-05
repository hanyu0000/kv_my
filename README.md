基于Raft的k-v存储数据库
```shell
.
├── bin 生成的可执行文件存放地
├── build 项目编译目录
├── example  范例代码存放地
│   ├── fiberExample  协程相关代码
│   ├── raftCoreExample raft核心代码
│   └── rpcExample rpc相关代码
├── lib  项目编译后的库文件存放地
├── src 【重点】项目源代码存放地，按照子模块组织
│   ├── common  子模块共用的，一般是一些util，日志，配置文件
│   ├── fiber  协程相关代码
│   ├── raftClerk raft客户端代码
│   ├── raftCore raft核心代码
│   ├── raftRpcPro raft中rpc涉及的protoc文件
│   ├── rpc  rpc库相关代码
│   └── skipList 跳表（上层状态机）相关代码
.
```