#include <iostream>
#include <google/protobuf/service.h>
#include "tinyrpc/net/tinypb/tinypb_rpc_channel.h"
#include "tinyrpc/net/tinypb/tinypb_rpc_async_channel.h"
#include "tinyrpc/net/tinypb/tinypb_rpc_controller.h"
#include "tinyrpc/net/tinypb/tinypb_rpc_closure.h"
#include "tinyrpc/net/net_address.h"
#include "test_tinypb_server.pb.h"

void test_client() {
    tinyrpc::IPAddress::ptr addr = std::make_shared<tinyrpc::IPAddress>("127.0.0.1", 39999);

    // 按protobuf官方文件调用RPC服务
    tinyrpc::TinyPbRpcChannel channel(addr);
    QueryService_Stub stub(&channel);

    tinyrpc::TinyPbRpcController rpc_controller;
    rpc_controller.SetTimeout(5000);

    queryAgeReq rpc_req;
    queryAgeRes rpc_res;

    std::cout << "向tinyrpc服务器["<< addr->toString() << "]发送请求对象: " << 
        rpc_req.ShortDebugString() << std::endl;
    stub.query_age(&rpc_controller, &rpc_req, &rpc_res, NULL);

    // 打印RPC服务调用结果
    if (rpc_controller.ErrorCode() != 0) {
        std::cout << "调用RPC服务失败, 错误码: " << rpc_controller.ErrorCode() << 
            ", 错误信息: " << rpc_controller.ErrorText() << std::endl;
        return;
    }

    std::cout << "成功调用RPC服务, 从RPC服务器["<< addr->toString() << "]获取应答对象: " << 
        rpc_res.ShortDebugString() << std::endl; 
}

int main(int argc, char* argv[]) {
    test_client();
    return 0;
}