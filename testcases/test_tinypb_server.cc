#include <iostream>
#include <google/protobuf/service.h>
#include <sstream>
#include <atomic>

#include "tinyrpc/net/tcp/tcp_server.h"
#include "tinyrpc/net/net_address.h"
#include "tinyrpc/net/mutex.h"
#include "tinyrpc/net/tinypb/tinypb_rpc_dispatcher.h"
// #include "tinyrpc/comm/log.h"
#include "tinyrpc/comm/start.h"
#include "test_tinypb_server.pb.h"

static int i = 0;
tinyrpc::CoroutineMutex g_cor_mutex;

class QueryServiceImpl: public QueryService {
public:
    QueryServiceImpl() {}
    ~QueryServiceImpl() {}

public:
    void query_name(google::protobuf::RpcController* controller, const ::queryNameReq* request, 
    ::queryNameRes* response, ::google::protobuf::Closure* done) 
    {
        // 1.打印请求类对象request的内容
        std::cout << "AppInfoLog: [在函数QueryServiceImpl::query_name中] 输入参数request = {" << 
            request->ShortDebugString() << "}" << std::endl;

        // 2.本函数核心: 实现query_name, 具体内容就是设置应答类的属性
        response->set_id(request->id());
        response->set_name("Peng Yuxuan");

        // 3.答应应答类对象response的内容
        std::cout << "AppInfoLog: [在函数QueryServiceImpl::query_name中] 输入参数response = {" << 
            response->ShortDebugString() << "}" << std::endl;

        // 4.执行后处理回调函数
        if (done) {
            done->Run();
        }
    }

    void query_age(google::protobuf::RpcController* controller, const ::queryAgeReq* request,
    ::queryAgeRes* response, ::google::protobuf::Closure* done) 
    {
        // 1.打印请求类对象request的内容
        std::cout << "AppInfoLog: [在函数QueryServiceImpl::query_age中] 输入参数request = {" << 
            request->ShortDebugString() << "}" << std::endl;

        // 2.本函数的核心: 实现query_age, 具体内容就是设置应答类的属性
        response->set_ret_code(0);
        response->set_res_info("OK");
        response->set_req_no(request->req_no());
        response->set_id(request->id());
        response->set_age(100100111);

        // 似乎是一个测试
        /*
        g_cor_mutex.lock();
        std::cout << "AppDebugLog: [在函数QueryServiceImpl::query_age中] begin i = " << i << std::endl;
        sleep(1);
        i++;
        std::cout << "AppDebugLog: [在函数QueryServiceImpl::query_age中] end i = " << i << std::endl;
        g_cor_mutex.unlock();
        */

        // 3.打印应答类对象response的内容
        std::cout << "AppInfoLog: [在函数QueryServiceImpl::query_age中] 输出参数response = {" << 
            response->ShortDebugString() << "}" << std::endl;

        // 4.执行后处理回调函数
        if(done) {
            done->Run();
        }
    }
};

int main(int argc, char* argv[]) {
    // 1.参数检查
    if (argc != 2) {
        printf("启动TinyRPC服务器出现错误, 输入的参数数量不是2!\n");
        printf("请按如下写法启动TinyRPC服务器: \n");
        printf("./server a.xml\n");
        return 0;
    }

    // 2.启动
    std::cout << "信息日志: [在文件test_tinypb_server.cc中][在函数main中] 开始尝试启动RPC服务器" << std::endl;
    tinyrpc::InitConfig(argv[1]);  // 读取xml配置文件中的配置数据, 存储到gRpcConfig对象中, 备用
    std::cout << "信息日志: [在文件test_tinypb_server.cc中][在函数main中] 配置RPC服务器的参数" << std::endl;
    REGISTER_SERVICE(QueryServiceImpl);  // 注册上面定义的QueryServiceImpl服务
    std::cout << "信息日志: [在文件test_tinypb_server.cc中][在函数main中] 注册QueryServiceImpl服务" << std::endl;
    tinyrpc::StartRpcServer();  // 启动服务器
    std::cout << "信息日志: [在文件test_tinypb_server.cc中][在函数main中] 成功启动RPC服务器" << std::endl;

    return 0;
}