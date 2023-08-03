#ifndef TINYRPC_NET_TINYPB_TINYPB_DATA_H
#define TINYRPC_NET_TINYPB_TINYPB_DATA_H

#include <stdint.h>
#include <vector>
#include <string>
#include "tinyrpc/net/abstract_data.h"
// #include "tinyrpc/comm/log.h"

namespace tinyrpc {

class TinyPbStruct: public AbstractData {
public:
    typedef std::shared_ptr<TinyPbStruct> pb_ptr;

public:
    TinyPbStruct() = default;
    ~TinyPbStruct() = default;

    TinyPbStruct(const TinyPbStruct&) = default;
    TinyPbStruct& operator=(const TinyPbStruct&) = default;

    TinyPbStruct(TinyPbStruct&&) = default;
    TinyPbStruct& operator=(TinyPbStruct&&) = default;

public:
    // char start;                     // 开始符, 标示TinyPB报文的结束
    int32_t pk_len {0};                // 整包长度
    int32_t msg_req_len {0};           // MsgID长度(Request ID长度)
    std::string msg_req;               // MsgID, 一次RPC通信的唯一标识. 一次RPC通信的请求包和响应包的MsgID应当一致
    int32_t service_name_len {0};      // 方法名长度
    std::string service_full_name {0}; // 方法的完整名, 例如QueryService.query_name
    int32_t err_code {0};              // 错误码. 若RPC调用过程中发生框架级错误, 则对此错误码进行相应设置, 正常为0
    int32_t err_info_len {0};          // 错误信息长度
    std::string err_info;              // 错误信息
    std::string pb_data;               // Protobuf所序列化的数据: 使用Protobuf库将Message对象序列化后的结果
    int32_t check_num {-1};            // 整包校验码, 防止篡改
    // char end;                       // 结束符, 标示TinyPB包的结束
};

}

#endif