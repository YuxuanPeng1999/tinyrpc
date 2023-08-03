#include <google/protobuf/service.h>
#include "tinyrpc/comm/start.h"
// #include "tinyrpc/comm/log.h"
#include "tinyrpc/comm/config.h"
#include "tinyrpc/net/tcp/tcp_server.h"
#include "tinyrpc/coroutine/coroutine_hook.h"

namespace tinyrpc {

tinyrpc::Config::ptr gRpcConfig;
tinyrpc::TcpServer::ptr gRpcServer;

static int g_init_config = 0;

// file: 配置文件路径
void InitConfig(const char* file) {
    std::cout << "调试日志: [在函数InitConfig中] 函数InitConfig开始" << std::endl;
    tinyrpc::SetHook(true);
    if (g_init_config == 0) {  // 如果还没配置
        gRpcConfig = std::make_shared<tinyrpc::Config>(file);  // 构造config对象
        gRpcConfig->readConf();  // 把配置文件中存储的配置数据, 赋值到gRpcConfig的属性中
        g_init_config = 1;
    }
    std::cout << "调试日志: [在函数InitConfig中] 函数InitConfig结束" << std::endl;
}

TcpServer::ptr GetServer() {
    return gRpcServer;
}

void StartRpcServer() {
    gRpcServer->start();
}

int GetIOThreadPoolSize() {
    return gRpcServer->getIOThreadPool()->getIOThreadPoolSize();
}

Config::ptr GetConfig() {
    return gRpcConfig;
}

void AddTimerEvent(TimerEvent::ptr event) {}

}