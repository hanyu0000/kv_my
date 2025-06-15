```shell
src/rpc/
├── include/           # 头文件目录
│   ├── mprpcconfig.h      # 配置管理类
│   ├── mprpcchannel.h     # RPC客户端通道
│   ├── mprpccontroller.h  # RPC控制器
│   ├── rpcprovider.h      # RPC服务提供者
│   └── rpcheader.pd.h     # 自动生成的protobuf头文件
├── rpcheader.proto        # protobuf协议定义文件
├── mprpcconfig.cpp        # 配置管理实现
├── mprpcchannel.cpp       # RPC客户端通道实现
├── mprpccontroller.cpp    # RPC控制器实现
├── rpcprovider.cpp        # RPC服务提供者实现
└── rpcheader.pb.cpp       # 自动生成的protobuf实现
```