#ifndef TINYRPC_NET_ABSTRACT_DISPATCHER_H
#define TINYRPC_NET_ABSTRACT_DISPATCHER_H

#include <memory>
#include <google/protobuf/service.h>
#include "tinyrpc/net/abstract_data.h"
#include "tinyrpc/net/tcp/tcp_connection.h"

namespace tinyrpc {

class TcpConnection;

class AbstractDispatcher {
public:
    typedef std::shared_ptr<AbstractDispatcher> ptr;

public:
    AbstractDispatcher() {}
    virtual ~AbstractDispatcher() {}

public:
    virtual void dispatch(AbstractData* data, TcpConnection* conn) = 0;  // 纯虚函数
};

}

#endif