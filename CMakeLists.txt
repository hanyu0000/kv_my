# 设置cmake的最低版本和项目名称
cmake_minimum_required(VERSION 3.22)
project(KVRaftCpp)

set(CMAKE_CXX_STANDARD 14)
# 生成debug版本，可以进行gdb调试
set(CMAKE_BUILD_TYPE "Debug")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# -------------------------- Build switches (simplify project) --------------------------
option(KV_BUILD_MINIMAL "Build minimal core only (no examples, fewer libs)" OFF)
option(KV_BUILD_EXAMPLES "Build example programs" ON)
if(KV_BUILD_MINIMAL)
  set(KV_BUILD_EXAMPLES OFF CACHE BOOL "Disable examples in minimal build" FORCE)
  message(STATUS "[KVRaftCpp] Minimal build is ON: examples disabled, core only.")
endif()

# 设置项目可执行文件输出的路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
# 设置项目库文件输出的路径
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

# 设置项目编译头文件搜索路径 -I
include_directories(${PROJECT_SOURCE_DIR}/src/common/include)
include_directories(${PROJECT_SOURCE_DIR}/src/rpc/include)
include_directories(${PROJECT_SOURCE_DIR}/example)
include_directories(${PROJECT_SOURCE_DIR}/src/raftCore/include)
include_directories(${PROJECT_SOURCE_DIR}/src/raftRpcPro/include)

# 设置项目库文件搜索路径 -L
link_directories(${PROJECT_SOURCE_DIR}/lib)

add_subdirectory(src)

if(KV_BUILD_EXAMPLES)
  add_subdirectory(example)
endif()