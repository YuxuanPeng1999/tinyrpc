#ifndef TINYRPC_NET_NET_ADDRESS_H
#define TINYRPC_NET_NET_ADDRESS_H

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <unistd.h>
#include <memory>

namespace tinyrpc {

class NetAddress {
public:
    typedef std::shared_ptr<NetAddress> ptr;
    virtual sockaddr* getSockAddr() = 0;
    virtual int getFamily() const = 0;
    virtual std::string toString() const = 0;
    virtual socklen_t getSockLen() const = 0;
};

// 对sockaddr_in类型进行封装
class IPAddress: public NetAddress {
public:
    IPAddress(const std::string& ip, uint16_t port);
    IPAddress(const std::string& addr);
    IPAddress(uint16_t port);
    IPAddress(sockaddr_in addr);

public:
    sockaddr* getSockAddr();
    int getFamily() const;
    socklen_t getSockLen() const;
    std::string toString() const;
    std::string getIP() const { return m_ip; }
    int getPort() const { return m_port; }

public:
    static bool CheckValidIPAddr(const std::string& addr);

private:
    std::string m_ip;  // 字符串形式的IP地址
    uint16_t m_port;   // 端口
    sockaddr_in m_addr;  // 要封装的sockaddr_in对象
};

}

#endif