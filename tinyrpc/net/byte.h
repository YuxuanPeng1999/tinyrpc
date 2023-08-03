#ifndef TINYRPC_NET_BYTE_H
#define TINYRPC_NET_BYTE_H

#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>

namespace tinyrpc {

// 从buf开始, 取出4字节, 作为unsigned int型变量
// 将该变量从网络字节序, 转换为本机字节序.
int32_t getInt32FromNetByte(const char* buf) {
    int32_t tmp;
    memcpy(&tmp, buf, sizeof(tmp));
    return ntohl(tmp);
}

}

#endif