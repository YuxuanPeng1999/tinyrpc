#include <iostream>
#include <sstream>
#include <google/protobuf/message.h>
#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>

#include "../abstract_dispatcher.h"
#include "../../comm/error_code.h"
#include "tinypb_data.h"
#include "tinypb_rpc_dispatcher.h"
#include "tinypb_rpc_controller.h"
#include "tinypb_rpc_closure.h"
#include "tinypb_codec.h"
#include "../../comm/msg_req.h"

namespace tinyrpc {

class TcpBuffer;

void TinyPbRpcDispatcher::registerService(service_ptr service) {
    std::string service_name = service->GetDescriptor()->full_name();
    m_service_map[service_name] = service;
    std::cout << "信息日志: [在函数TinyPbRpcDispatcher::registerService中] 成功将" << 
        service_name << "服务注册!" << std::endl;
}

void TinyPbRpcDispatcher::dispatch(AbstractData* data, TcpConnection* conn) {
    TinyPbStruct* tmp = dynamic_cast<TinyPbStruct*>(data);
    // 1.错误检查
    if (tmp == nullptr) {
        std::cout << "错误日志: [在函数TinyPbRpcDispatcher::dispatch中] 输入的TinyPb RPC包为nullptr, 本函数退出" << std::endl;
        return;
    }

    // 2.记录当前协程及线程正在处理的RPC通信的编号
    // 设置当前协程的RunTime对象m_run_time的m_msg_no属性
    Coroutine::GetCurrentCoroutine()->getRunTime()->m_msg_no = tmp->msg_req;
    // 把当前协程的RunTime对象m_run_time设为当前线程的RunTime对象t_cur_run_time
    setCurrentRunTime(Coroutine::GetCurrentCoroutine()->getRunTime());
    std::cout << "信息日志: [在函数TinyPbRpcDispatcher::dispatch中] 开始分发客户端发来的tinypb请求包, msgno = "
        << tmp->msg_req << std::endl;

    // 3.解析本次RPC通信中客户端请求的服务名和方法名, 如果无法解析, 则直接返回
    std::string service_name;
    std::string method_name;

    TinyPbStruct reply_pk;  // tinypb回包
    reply_pk.service_full_name = tmp->service_full_name;
    reply_pk.msg_req = tmp->msg_req;
    if (reply_pk.msg_req.empty()) {
        reply_pk.msg_req = MsgReqUtil::genMsgNumber();
    }

    if (!parseServiceFullName(tmp->service_full_name, service_name, method_name)) {
        // 将错误信息打印到日志中
        std::cout << "错误日志: [在函数TinyPbRpcDispatcher::dispatch中] MsgId[" << reply_pk.msg_req <<
            "] | 解析服务名" << tmp->service_full_name << "出错" << std::endl;
        // 设置回包reply_pk中的错误信息和错误码
        reply_pk.err_code = ERROR_PARSE_SERVICE_NAME;
        std::stringstream ss;
        ss << "无法解析服务名: [" << tmp->service_full_name << "]";
        reply_pk.err_info = ss.str();
        // 令conn的编码器对reply_pk进行编码
        conn->getCodec()->encode(conn->getOutBuffer(), dynamic_cast<AbstractData*>(&reply_pk));
        std::cout << "信息日志: [在函数TinyPbRpcDispatcher::dispatch中] 当前函数结束, MsgId =" << tmp->msg_req << std::endl;
        return;
    }

    // 4.根据服务名, 在m_service_map中找到对应的服务类对象
    Coroutine::GetCurrentCoroutine()->getRunTime()->m_interface_name = tmp->service_full_name;
    auto it = m_service_map.find(service_name);
    // 错误检查: 如果m_service_map中没有这个服务(即该服务没有注册), 
    // 或者是该服务名对应的服务类对象为空, 则要报错, 并直接返回
    if (it == m_service_map.end() || !((*it).second)) {
        // 将错误信息打印到日志中
        std::stringstream ss;
        ss << "未能找到服务名: [" << service_name << "]";
        std::cout << "错误日志: [在函数TinyPbRpcDispatcher::dispatch中] MsgId[" << reply_pk.msg_req <<
            "] | " << ss.str() << std::endl;
        // 设置回包reply_pk中的错误信息和错误码
        reply_pk.err_code = ERROR_SERVICE_NOT_FOUND;
        reply_pk.err_info = ss.str();
        // 令conn的编码器对reply_pk进行编码
        conn->getCodec()->encode(conn->getOutBuffer(), dynamic_cast<AbstractData*>(&reply_pk));
        std::cout << "信息日志: [在函数TinyPbRpcDispatcher::dispatch中] 当前函数结束, MsgId =" << tmp->msg_req << std::endl;
        return;
    }
    service_ptr service = (*it).second;

    // 5.根据服务对象和方法名, 找到method_name对应的方法
    const google::protobuf::MethodDescriptor* method = service->GetDescriptor()->FindMethodByName(method_name);
    if (!method) {  // 如果method是nullptr, 即没有找到method_name对应的method
        // 将错误信息打印到日志中
        std::stringstream ss;
        ss << "未能找到方法名: [" << method_name << "]";
        std::cout << "错误日志: [在函数TinyPbRpcDispatcher::dispatch中] MsgId[" << reply_pk.msg_req <<
            "] | " << ss.str() << std::endl;
        // 设置回包reply_pk中的错误信息和错误码
        reply_pk.err_code = ERROR_METHOD_NOT_FOUND;
        reply_pk.err_info = ss.str();
        // 令conn的编码器对reply_pk进行编码
        conn->getCodec()->encode(conn->getOutBuffer(), dynamic_cast<AbstractData*>(&reply_pk));
        std::cout << "信息日志: [在函数TinyPbRpcDispatcher::dispatch中] 当前函数结束, MsgId =" << tmp->msg_req << std::endl;
        return;
    }

    // 6.构造request对象, 即method_name对应方法的输入. 具体方法是根据TinyPb请求包中的
    // pb_data(被序列化成字节流的request类对象)构造
    // 6-1 新构造一个request对象
    google::protobuf::Message* request = service->GetRequestPrototype(method).New();
    std::cout << "信息日志: [在函数TinyPbRpcDispatcher::dispatch中] MsgId[" << reply_pk.msg_req <<
        "] | request.name = " << request->GetDescriptor()->full_name() << std::endl;
    // 6-2 使用pb_data为request赋值, 如果失败, 则设置错误码和错误信息, 并返回
    if (!request->ParseFromString(tmp->pb_data)) {
        // 将错误信息打印到日志中
        std::stringstream ss;
        ss << "解析请求数据失败, 请求名: [" << request->GetDescriptor()->full_name() << "]";
        std::cout << "错误日志: [在函数TinyPbRpcDispatcher::dispatch中] MsgId[" << reply_pk.msg_req <<
            "] | " << ss.str() << std::endl;
        // 设置回包reply_pk中的错误信息和错误码
        reply_pk.err_code = ERROR_FAILED_SERIALIZE;
        reply_pk.err_info = ss.str();
        // 删除request
        delete request;
        // 令conn的编码器对reply_pk进行编码
        conn->getCodec()->encode(conn->getOutBuffer(), dynamic_cast<AbstractData*>(&reply_pk));
        std::cout << "信息日志: [在函数TinyPbRpcDispatcher::dispatch中] 当前函数结束, MsgId =" << tmp->msg_req << std::endl;
        return;
    }
    // 如果成功, 也输出信息
    std::cout << "============================================================" << std::endl;
    std::cout << "信息日志: [在函数TinyPbRpcDispatcher::dispatch中] MsgId[" << reply_pk.msg_req <<
        "] | " << "获取到客户端的请求数据: " << request->ShortDebugString() << std::endl;
    std::cout << "============================================================" << std::endl;

    // 7.调用method方法, 获取应答类对象
    // 7-1 在堆区创建一个空的应答类对象
    google::protobuf::Message* response = service->GetResponsePrototype(method).New();
    std::cout << "信息日志: [在函数TinyPbRpcDispatcher::dispatch中] MsgId[" << reply_pk.msg_req <<
        "] | response.name = " << response->GetDescriptor()->full_name() << std::endl;
    // 7-2 调用客户端请求的参数
    // 7-2-1 准备CallMethod的第二个参数
    TinyPbRpcController rpc_controller;
    rpc_controller.SetMsgReq(reply_pk.msg_req);
    rpc_controller.SetMethodName(method_name);
    rpc_controller.SetMethodFullName(tmp->service_full_name);
    // 7-2-2 准备CallMethod的第五个参数
    std::function<void()> reply_package_func = [](){};  // 空的lambda函数
    TinyPbRpcClosure closure(reply_package_func);
    // 参数含义: (1)要调用的方法, (2)设置RPC服务过程中的相关参数, 
    // (3)method方法的一个输入参数: 客户端发送的请求类对象
    // (4)传出参数, method方法的返回值: 要回传给客户端的应答类对象
    // (5)本参数用于在RPC服务结束后调用指定的回调函数, 但本项目中没有用到, 本参数保存的回调函数为空
    service->CallMethod(method, &rpc_controller, request, response, &closure);
    std::cout << "信息日志: [在函数TinyPbRpcDispatcher::dispatch中] 成功调用[" << 
        reply_pk.service_full_name << "], 接下来向客户端发送回包" << std::endl;

    // 8.将reponse序列化, 存入reply_pk.pb_data当中
    // response指向google::protobuf::Message类对象, 该类有成员函数SerializeToString, 
    // 参数为传出参数, 为将本对象序列化后存储的目标地址, 现令response调用该函数, 将自己序列化, 
    // 并存入reply_pk的pb_data成员中
    if (!( response->SerializeToString(&(reply_pk.pb_data)) )) {  // 如果序列化失败
        reply_pk.pb_data = "";
        std::cout << "错误日志: [在函数TinyPbRpcDispatcher::dispatch中] MsgId[" << reply_pk.msg_req <<
            "] | 构造回包时出现错误, 序列化应答类对象失败!" << std::endl;
        reply_pk.err_code = ERROR_FAILED_SERIALIZE;
        reply_pk.err_info = "序列化应答类对象失败";
    } else {  // 如果序列化成功
        std::cout << "============================================================" << std::endl;
        std::cout << "信息日志: [在函数TinyPbRpcDispatcher::dispatch中] MsgId[" << reply_pk.msg_req <<
            "] | " << "服务端设置应答数据: " << response->ShortDebugString() << std::endl;
        std::cout << "============================================================" << std::endl;
    }

    // 9.删除堆区指针, 并将构造的reply_pk对象返回给conn对象的m_write_buffer中, 
    // 后续, output函数会将m_write_buffer中的内容发送到客户端
    // 9-1 删除堆区指针
    if (request) {
        delete request;
        request = nullptr;
    }
    if (response) {
        delete response;
        response = nullptr;
    }
    // 9-2 将构造的reply_pk对象返回给conn对象的m_write_buffer中
    conn->getCodec()->encode(conn->getOutBuffer(), dynamic_cast<AbstractData*>(&reply_pk));
}

bool TinyPbRpcDispatcher::parseServiceFullName(const std::string& full_name, 
    std::string& service_name, std::string& method_name){
    // 1.错误检查
    if (full_name.empty()) {
        std::cout << "错误日志: [在函数TinyPbRpcDispatcher::parseServiceFullName中] 传入的service_full_name为空, 本函数失败" << std::endl;
        return false;
    }
    // 2.开始解析
    // 2-1 找到点号分隔符
    std::size_t i = full_name.find(".");  // 找到点号在full_name中的索引
    if (i == full_name.npos) {  // 错误检查
        std::cout << "错误日志: [在函数TinyPbRpcDispatcher::parseServiceFullName中] 未能找到分隔符[.], 本函数失败" << std::endl;
        return false;
    }
    // 2-2 根据点号位置, 将full_name前后拆分为service_name和method_name
    service_name = full_name.substr(0, i);
    std::cout << "调试日志: [在函数TinyPbRpcDispatcher::parseServiceFullName中] service_name = " << 
        service_name << std::endl;
    method_name = full_name.substr(i+1, full_name.length() - i - 1);
    std::cout << "调试日志: [在函数TinyPbRpcDispatcher::parseServiceFullName中] method_name = " << 
        method_name << std::endl;

    return true;
}

}