option(KV_BUILD_FIBER_EXAMPLES "Build fiber subsystem examples" ON)
option(KV_BUILD_RPC_EXAMPLES "Build rpc examples (requires protobuf & muduo)" OFF)
option(KV_BUILD_RAFT_EXAMPLES "Build raft+kv examples (requires muduo/protobuf)" OFF)

if(KV_BUILD_FIBER_EXAMPLES)
  add_subdirectory(fiberExample)
endif()

if(KV_BUILD_RPC_EXAMPLES)
  add_subdirectory(rpcExample)
endif()

if(KV_BUILD_RAFT_EXAMPLES)
  add_subdirectory(raftCoreExample)
endif()