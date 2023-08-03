#ifndef TINYRPC_COMM_START_H
#define TINYRPC_COMM_START_H

#include <google/protobuf/service.h>
#include <memory>
#include <stdio.h>
#include <functional>
// #include "tinyrpc/comm/log.h"
#include "tinyrpc/net/tcp/tcp_server.h"
#include "tinyrpc/net/timer.h"
#include "tinyrpc/comm/config.h"

namespace tinyrpc {

#define REGISTER_SERVICE(service) \
    do { \
        if (!tinyrpc::GetServer()->registerService(std::make_shared<service>())) { \
            printf("启动TinyRPC服务器出错, 原因: 注册protobuf服务出错, 请查看RPC日志以获得更多信息!\n"); \
            exit(0); \
        } \
    } while(0) \

void InitConfig(const char* file);

void StartRpcServer();

TcpServer::ptr GetServer();

int GetIOThreadPoolSize();

Config::ptr GetConfig();

void AddTimerEvent(TimerEvent::ptr event);

}

#endif