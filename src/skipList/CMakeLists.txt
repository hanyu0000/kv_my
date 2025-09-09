get_filename_component(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}" ABSOLUTE)

# 头文件库（跳表为头文件实现）
add_library(skiplist_lib INTERFACE)
target_include_directories(skiplist_lib INTERFACE ${SRC_DIR}/include)


