#ifndef TINYRPC_NET_TINYPB_TINYPB_RPC_DISPATCHER_H
#define TINYRPC_NET_TINYPB_TINYPB_RPC_DISPATCHER_H

#include <google/protobuf/message.h>
#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>
#include <map>
#include <memory>

#include "tinyrpc/net/abstract_dispatcher.h"
#include "tinyrpc/net/tinypb/tinypb_data.h"

namespace tinyrpc {

class TinyPbRpcDispatcher: public AbstractDispatcher {
public:
    typedef std::shared_ptr<google::protobuf::Service> service_ptr;

public:
    TinyPbRpcDispatcher() = default;
    ~TinyPbRpcDispatcher() = default;

public:
    void registerService(service_ptr service);
    void dispatch(AbstractData* data, TcpConnection* conn);
    bool parseServiceFullName(const std::string& full_name, std::string& service_name, std::string& method_name);

public:
    // 进程开始之前, 所有服务都应被注册到此处
    // key: service_name
    std::map<std::string, service_ptr> m_service_map;
};

}

#endif