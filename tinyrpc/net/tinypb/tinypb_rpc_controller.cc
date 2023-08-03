#include <google/protobuf/service.h>
#include <google/protobuf/stubs/callback.h>
#include "tinypb_rpc_controller.h"

namespace tinyrpc {

void TinyPbRpcController::Reset() {
    m_error_code = 0;
    m_error_info = "";
    m_msg_req = "";    // MsgID, 一次RPC通信的唯一标识
    m_is_failed = false;
    m_is_canceled = false;  // RPC调用是否已经被取消
    m_peer_addr = nullptr;  // 对端地址
    m_local_addr = nullptr; // 本机地址
    m_timeout = 5000;
    m_method_name = "";  // 客户端请求的方法的名称
    m_full_name = "";
}

bool TinyPbRpcController::Failed() const {
  return m_is_failed;
}

std::string TinyPbRpcController::ErrorText() const {
  return m_error_info;
}

void TinyPbRpcController::StartCancel() {}

// 如果要将m_is_failed设置为true, 还须设置对应的错误信息, 填写失败原因
void TinyPbRpcController::SetFailed(const std::string& reason) {
  m_is_failed = true;
  m_error_info = reason;
}

bool TinyPbRpcController::IsCanceled() const {
  return false;
}

void TinyPbRpcController::NotifyOnCancel(google::protobuf::Closure* callback) {

}

void TinyPbRpcController::SetErrorCode(const int error_code) {
  m_error_code = error_code;
}

int TinyPbRpcController::ErrorCode() const {
  return m_error_code; 
}

const std::string& TinyPbRpcController::MsgSeq() const {
  return m_msg_req;
}

void TinyPbRpcController::SetMsgReq(const std::string& msg_req) {
  m_msg_req = msg_req;
}

void TinyPbRpcController::SetError(const int err_code, const std::string& err_info) {
  SetFailed(err_info);
  SetErrorCode(err_code);
}

void TinyPbRpcController::SetPeerAddr(NetAddress::ptr addr) {
  m_peer_addr = addr;
}

void TinyPbRpcController::SetLocalAddr(NetAddress::ptr addr) {
  m_local_addr = addr;
}
NetAddress::ptr TinyPbRpcController::PeerAddr() {
  return m_peer_addr;
}
  
NetAddress::ptr TinyPbRpcController::LocalAddr() {
  return m_local_addr;
}

void TinyPbRpcController::SetTimeout(const int timeout) {
  m_timeout = timeout;
}
int TinyPbRpcController::Timeout() const {
  return m_timeout;
}

void TinyPbRpcController::SetMethodName(const std::string& name) {
  m_method_name = name;
}

std::string TinyPbRpcController::GetMethodName() {
  return m_method_name;
}

void TinyPbRpcController::SetMethodFullName(const std::string& name) {
  m_full_name = name;
}

std::string TinyPbRpcController::GetMethodFullName() {
  return m_full_name;
}

}