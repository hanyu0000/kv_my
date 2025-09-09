set(SRC_LIST raftKvDB.cpp)

add_executable(raftCoreRun ${SRC_LIST})
target_link_libraries(raftCoreRun rpc_lib protobuf muduo_net muduo_base pthread )

set(SRC_LIST2 caller.cpp)
add_executable(callerMain ${src_raftClerk} ${SRC_LIST2}  ${src_common})
target_link_libraries(callerMain protobuf boost_serialization )