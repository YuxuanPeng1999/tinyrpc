#ifndef TINYRPC_NET_TINYPB_TINYPB_RPC_CONRTOLLER_H
#define TINYRPC_NET_TINYPB_TINYPB_RPC_CONRTOLLER_H

#include <google/protobuf/service.h>
#include <google/protobuf/stubs/callback.h>
#include <stdio.h>
#include <memory>
#include "../net_address.h"

namespace tinyrpc {

class TinyPbRpcController: public google::protobuf::RpcController {
public:
    typedef std::shared_ptr<TinyPbRpcController> ptr;

public:  // Client-side methods
    TinyPbRpcController() = default;
    ~TinyPbRpcController() = default;
    void Reset() override;        // 将RpcController对象的各属性设置为默认值, 以便该对象被其他调用过程复用
    bool Failed() const override; // 查询当前RPC调用是否成功

public: // Server-side methods
    std::string ErrorText() const override;  // 如果失败, 返回错误信息
    void StartCancel() override;
    void SetFailed(const std::string& reason) override;
    bool IsCanceled() const override;
    void NotifyOnCancel(google::protobuf::Closure* callback) override;  // 

public: // common methods
    int ErrorCode() const;
    void SetErrorCode(const int error_code);

    const std::string& MsgSeq() const;
    void SetMsgReq(const std::string& msg_req);

    void SetError(const int err_code, const std::string& err_info);

    void SetPeerAddr(NetAddress::ptr addr);
    void SetLocalAddr(NetAddress::ptr addr);
    NetAddress::ptr PeerAddr();
    NetAddress::ptr LocalAddr();

    void SetTimeout(const int timeout);
    int Timeout() const;

    void SetMethodName(const std::string& name);
    std::string GetMethodName();

    void SetMethodFullName(const std::string& name);
    std::string GetMethodFullName();

private:
    // 下面这三个对应RPC协议中的字段(即TinyPbStruct的成员变量)
    int m_error_code {0};
    std::string m_error_info;
    std::string m_msg_req;    // MsgID, 一次RPC通信的唯一标识
    // 
    bool m_is_failed {false};
    bool m_is_canceled {false};  // RPC调用是否已经被取消
    // 
    NetAddress::ptr m_peer_addr;  // 对端地址
    NetAddress::ptr m_local_addr; // 本机地址
    // 
    int m_timeout {5000};  // RPC调用超时时间, 单位: ms
    std::string m_method_name;  // 客户端请求的方法的名称
    std::string m_full_name;    // 客户端请求的方法的全名, 例如server.method_name, 前者是服务类名, 后者是方法名
};

}

#endif