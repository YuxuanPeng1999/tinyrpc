#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <sstream>
#include "net_address.h"
// #include "../comm/log.h"

namespace tinyrpc {

IPAddress::IPAddress(const std::string& ip, uint16_t port): m_ip(ip), m_port(port) {
    // 1.将m_addr内存都置为0
    memset(&m_addr, 0, sizeof(m_addr));
    // 2.设置协议族、IP地址、端口
    m_addr.sin_family = AF_INET;
    m_addr.sin_addr.s_addr = inet_addr(m_ip.c_str());
    m_addr.sin_port = htons(m_port);
    // 3.打印日志
    std::cout << "调试日志: [在IPAddress类构造函数中] 成功创建IPv4地址[" << toString() << "]" << std::endl;
}

IPAddress::IPAddress(sockaddr_in addr): m_addr(addr) {
    // std::cout << "调试日志: [在IPAddress类构造函数中] ip[" << m_ip << "], port[" << 
    //     addr.sin_port << "]" << std::endl;
    m_ip = std::string(inet_ntoa(m_addr.sin_addr));
    m_port = ntohs(m_addr.sin_port);
    std::cout << "调试日志: [在IPAddress类构造函数中] ip[" << m_ip << "], port[" << 
        m_port << "]" << std::endl;
}

IPAddress::IPAddress(const std::string& addr) {
  size_t i = addr.find_first_of(":");
  if (i == addr.npos) {
    std::cout << "错误日志: [在IPAddress类构造函数中] 无效的地址[" << addr << "]" << std::endl;
    return;
  }
  m_ip = addr.substr(0, i);
  m_port = std::atoi(addr.substr(i + 1, addr.size() - i - 1).c_str());

  memset(&m_addr, 0, sizeof(m_addr));
  m_addr.sin_family = AF_INET;
  m_addr.sin_addr.s_addr = inet_addr(m_ip.c_str());
  m_addr.sin_port = htons(m_port);
  std::cout << "调试日志: [在IPAddress类构造函数中] 成功创建IPv4地址[" << toString() << "]" << std::endl;
}

IPAddress::IPAddress(uint16_t port)  : m_port(port) {
  memset(&m_addr, 0, sizeof(m_addr));
  m_addr.sin_family = AF_INET;
  m_addr.sin_addr.s_addr = INADDR_ANY;
  m_addr.sin_port = htons(m_port);
 
  std::cout << "调试日志: [在IPAddress类构造函数中] 成功创建IPv4地址[" << toString() << "]" << std::endl;
}

sockaddr* IPAddress::getSockAddr() {
    return reinterpret_cast<sockaddr*>(&m_addr);
}

int IPAddress::getFamily() const {
    return m_addr.sin_family;
}

socklen_t IPAddress::getSockLen() const {
    return sizeof(m_addr);
}
std::string IPAddress::toString() const {
    std::stringstream ss;
    ss << m_ip << ": " << m_port;
    return ss.str();
}

}