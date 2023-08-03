cmake_minimum_required(VERSION 3.0)
project(tinyrpc)

# 预定义变量
set(PATH_BIN bin)
set(PATH_TESTCASES testcases)

# 支持汇编语言编译
enable_language(ASM)

# 设置头文件搜索的起始路径
include_directories(${PROJECT_SOURCE_DIR})

# 找到protobuf库
find_package(Protobuf REQUIRED)
if (${PROTOBUF_FOUND})
    message("protobuf found")
else ()
    message(FATAL_ERROR "Cannot find Protobuf")
endif ()

# 找到tinyxml库
set(tinyxml /usr/lib/libtinyxml.a)

# 设置库
set(LIBS ${Protobuf_LIBRARY} ${tinyxml} dl)

# 源文件列表
aux_source_directory(${PROJECT_SOURCE_DIR}/tinyrpc/comm COMM)
aux_source_directory(${PROJECT_SOURCE_DIR}/tinyrpc/coroutine COR)
aux_source_directory(${PROJECT_SOURCE_DIR}/tinyrpc/net NET)
aux_source_directory(${PROJECT_SOURCE_DIR}/tinyrpc/net/tcp TCP)
aux_source_directory(${PROJECT_SOURCE_DIR}/tinyrpc/net/tinypb TINYPB)
set(COXTX ${PROJECT_SOURCE_DIR}/tinyrpc/coroutine/coctx_swap.S)

# 设置二进制文件存放位置
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/${PATH_BIN})

# 设置编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -std=c++11 -Wall -Wno-deprecated -Wno-unused-but-set-variable -pthread")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -std=c++11 -Wall -Wno-deprecated -Wno-unused-but-set-variable -pthread -E")

# 编译testcases/mytest_coroutine01.cc
# add_executable(mytest_coroutine01 ${PATH_TESTCASES}/mytest_coroutine01.cc ${COMM} ${COR} ${COXTX})

# 编译testcases/mytest_coroutine02.cc
# add_executable(mytest_coroutine02 ${PATH_TESTCASES}/mytest_coroutine02.cc ${COMM} ${COR} ${COXTX})

# 编译testcases/test_tinypb_server.cc
add_executable(
    test_tinypb_server 
    ${PATH_TESTCASES}/test_tinypb_server.cc 
    ${PATH_TESTCASES}/test_tinypb_server.pb.cc 
    ${COMM} ${COR} ${COXTX} ${NET} ${TCP} ${TINYPB})
target_link_libraries(test_tinypb_server ${LIBS})

# 编译testcases/test_tinypb_server_client.cc
add_executable(
    test_tinypb_server_client 
    ${PATH_TESTCASES}/test_tinypb_server_client.cc 
    ${PATH_TESTCASES}/test_tinypb_server.pb.cc 
    ${COMM} ${COR} ${COXTX} ${NET} ${TCP} ${TINYPB})
target_link_libraries(test_tinypb_server_client ${LIBS})


