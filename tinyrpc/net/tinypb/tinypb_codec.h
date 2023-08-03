#ifndef TINYRPC_NET_TINYPB_TINYPB_CODEC_H
#define TINYRPC_NET_TINYPB_TINYPB_CODEC_H

#include <stdint.h>
#include "tinyrpc/net/abstract_codec.h"
#include "tinyrpc/net/abstract_data.h"
#include "tinyrpc/net/tinypb/tinypb_data.h"

namespace tinyrpc {

class TinyPbCodeC: public AbstractCodeC {

public:
    TinyPbCodeC();
    ~TinyPbCodeC();

public:  // override
    // 将data(动态类型为TinyPbStruct)转换为字节流, 写入到buf
    void encode(TcpBuffer* buf, AbstractData* data);
    // 将buf中的字节流转换为data对象
    void decode(TcpBuffer* buf, AbstractData* data);
    virtual ProtocalType getProtocalType();

public:
    const char* encodePbData(TinyPbStruct* data, int& len);
};

} 


#endif