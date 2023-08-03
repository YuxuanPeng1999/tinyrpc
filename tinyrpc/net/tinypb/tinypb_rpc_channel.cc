#include <iostream>
#include <memory>
#include <google/protobuf/service.h>
#include <google/protobuf/message.h>
#include <google/protobuf/descriptor.h>
#include "tinyrpc/net/net_address.h"
#include "tinyrpc/comm/error_code.h"
#include "tinyrpc/net/tcp/tcp_client.h"
#include "tinyrpc/net/tinypb/tinypb_rpc_channel.h"
#include "tinyrpc/net/tinypb/tinypb_rpc_controller.h"
#include "tinyrpc/net/tinypb/tinypb_codec.h"
#include "tinyrpc/net/tinypb/tinypb_data.h"
// #include "tinyrpc/comm/log.h"
#include "tinyrpc/comm/msg_req.h"
#include "tinyrpc/comm/run_time.h"

namespace tinyrpc {

TinyPbRpcChannel::TinyPbRpcChannel(NetAddress::ptr addr): m_addr(addr) {}

/*
参数说明: 
- response: 传出参数, 应答类对象
*/
void TinyPbRpcChannel::CallMethod(const google::protobuf::MethodDescriptor* method, 
    google::protobuf::RpcController* controller, const google::protobuf::Message* request, 
    google::protobuf::Message* response, google::protobuf::Closure* done)
{
    // 1.创建请求报文结构体pb_struct, 以及rpc_controller
    TinyPbStruct pb_struct;
    TinyPbRpcController* rpc_controller = dynamic_cast<TinyPbRpcController*>(controller);
    if (!rpc_controller) {
        std::cout << "错误日志: [在函数TinyPbRpcChannel::TinyPbRpcChannel中] 调用失败, RpcController向TinyPbRcpController转换失败" << std::endl;
        return;
    }

    TcpClient::ptr m_client = std::make_shared<TcpClient>(m_addr);
    rpc_controller->SetLocalAddr(m_client->getLocalAddr());
    rpc_controller->SetPeerAddr(m_client->getPeerAddr());

    // 2.构造请求报文结构体
    // 2-1 设置pb_struct.full_name
    pb_struct.service_full_name = method->full_name();
    std::cout << "调试日志: [在函数TinyPbRpcChannel::TinyPbRpcChannel中] 调用服务: " <<
        pb_struct.service_full_name << std::endl;
    
    // 2-2 设置pb_struct.request
    if (!request->SerializeToString(&(pb_struct.pb_data))) {
        std::cout << "错误日志: [在函数TinyPbRpcChannel::TinyPbRpcChannel中] 序列化请求类对象出错" << std::endl;
        return;
    }

    // 2-3 设置pb_struct.msg_req, 即RPC通信识别号(MsgID)
    if (!rpc_controller->MsgSeq().empty()) {  // 如果rpc_controller中保存的MsgID已经非空, 则直接用
        pb_struct.msg_req = rpc_controller->MsgSeq();
    } else {  // 如果rpc_controller中的MsgID为空
        RunTime* run_time = getCurrentRunTime();
        if (run_time != NULL && !run_time->m_msg_no.empty()) {
            pb_struct.msg_req = run_time->m_msg_no;
            std::cout << "调试日志: [在函数TinyPbRpcChannel::TinyPbRpcChannel中] 成功从RunTime获得MsgID = " << 
                pb_struct.msg_req << std::endl;
        } else {
            pb_struct.msg_req = MsgReqUtil::genMsgNumber();
            std::cout << "调试日志: [在函数TinyPbRpcChannel::TinyPbRpcChannel中] 从RunTime获取MsgID出错, 直接生成新的MsgID = " << 
                pb_struct.msg_req << std::endl;
        }
        rpc_controller->SetMsgReq(pb_struct.msg_req);  // 既然生成了新的MsgID, 就把rpc_controller中保存的也相应修改
    }

    // 3.序列化pb_struct, 存入m_write_buffer
    AbstractCodeC::ptr m_codec = m_client->getConnection()->getCodec(); // 获取编码器
    m_codec->encode(m_client->getConnection()->getOutBuffer(), &pb_struct); // 序列化pb_struct, 存入m_write_buffer中
    if (!pb_struct.encode_succ) {  // 如果编码失败, 如何处理: 
        rpc_controller->SetError(ERROR_FAILED_ENCODE, "encode tinypb data error");
        return;
    }
    // 如果序列化成功, 就打印日志信息
    std::cout << "============================================================" << std::endl;
    std::cout << "信息日志: [在函数TinyPbRpcChannel::TinyPbRpcChannel中] MsgID[" << 
        pb_struct.msg_req << "] | " << rpc_controller->PeerAddr()->toString() << 
        " |. 客户端发送如下数据: " << request->ShortDebugString() << std::endl;
    std::cout << "============================================================" << std::endl;

    // 4.设置发送超时时间
    m_client->setTimeout(rpc_controller->Timeout());

    // 5.调用sendAndRecvTinyPb函数, 即发送请求报文, 解析应答报文, 得到应答报文结构体
    // 具体执行内容如下: 
    // 5-1 如果尚未和RPC服务器建立连接, 则先建立连接
    // 5-2 使用output发送请求包(请求报文)
    // 5-3 执行input, 获取回包
    // 5-4 执行execute, 先解码回包(应答报文)得到TinyPbStruct, 
    //    然后执行客户端连接的操作: 将TinyPbStruct存入m_reply_datas中
    // 5-5 最终从sendAndRecvTinyPb的传出参数res, 获得res_data, 即应答报文
    TinyPbStruct::pb_ptr res_data;  // 回包对应的TinyPbStruct对象
    int rt = m_client->sendAndRecvTinyPb(pb_struct.msg_req, res_data);
    if (rt != 0) {  // 错误处理
        rpc_controller->SetError(rt, m_client->getErrInfo());  // 先把错误信息填入client的m_err_info
        std::cout << "错误日志: [在函数TinyPbRpcChannel::TinyPbRpcChannel中] MsgId[" << 
            pb_struct.msg_req << "] | 调用RPC出现错误, service_full_name = " <<
            pb_struct.service_full_name << ", err_code = " << rt << 
            "err_info = " << m_client->getErrInfo() << std::endl;  // 然后将错误信息打印
        return;
    }

    // 6.将TinyPb对象中保存的pb_data(序列化的应答类对象)反序列化, 即: 
    // response调用google::protobuf::Message类成员函数, 从TinyPb协议结构体中存储的
    // 应答类序列化结果中, 为自己赋值, 如果失败
    if (!response->ParseFromString(res_data->pb_data)) {
        rpc_controller->SetError(ERROR_FAILED_DESERIALIZE, "failed to deserialize data from server");
        std::cout << "错误日志: [在函数TinyPbRpcChannel::TinyPbRpcChannel中] MsgID[" << 
            pb_struct.msg_req << "] | 反序列化数据失败" << std::endl;
        return;
    }

    // 最后检查有无出错
    if (res_data->err_code != 0) {
        std::cout << "错误日志: [在函数TinyPbRpcChannel::TinyPbRpcChannel中] MsgID[" << 
            pb_struct.msg_req << "] | 服务器应答报文的error_code = " << res_data->err_code << 
            ", err_info = " << res_data->err_info << std::endl;
        rpc_controller->SetError(res_data->err_code, res_data->err_info);
        return;
    }

    // 7.成功之后, 打印信息
    std::cout << "============================================================" << std::endl;
    std::cout << "信息日志: [在函数TinyPbRpcChannel::TinyPbRpcChannel中] MsgID[" << 
        pb_struct.msg_req << "] | 成功调用RPC服务[" << pb_struct.service_full_name << 
        "]. 获取得到服务器的应答数据: " << response->ShortDebugString() << std::endl;
    std::cout << "============================================================" << std::endl;

    // 8.后处理, 在本项目中为空
    if (done) {
        done->Run();
    }
}

}