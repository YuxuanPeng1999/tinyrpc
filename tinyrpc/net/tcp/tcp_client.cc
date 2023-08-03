#include <iostream>
#include <sstream>
#include <sys/socket.h>
#include <arpa/inet.h>
// #include "tinyrpc/comm/log.h"
#include "tinyrpc/coroutine/coroutine.h"
#include "tinyrpc/coroutine/coroutine_hook.h"
#include "tinyrpc/coroutine/coroutine_pool.h"
#include "tinyrpc/net/net_address.h"
#include "tinyrpc/net/tcp/tcp_client.h" 
#include "tinyrpc/comm/error_code.h"
#include "tinyrpc/net/timer.h"
#include "tinyrpc/net/fd_event.h"
#include "tinyrpc/net/http/http_codec.h"
#include "tinyrpc/net/tinypb/tinypb_codec.h"

namespace tinyrpc {

TcpClient::TcpClient(NetAddress::ptr addr, ProtocalType type): m_peer_addr(addr) {
    m_family = m_peer_addr->getFamily();
    // 创建客户端文件描述符(clientfd)
    m_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_fd == -1) {
        std::cout << "错误日志: [在TcpClient类构造函数中] socket函数执行出现错误, 返回值为-1, sys error = " <<
            strerror(errno) << std::endl;
    }
    std::cout << "调试日志: [在TcpClient类构造函数中] 创建文件描述符: " << m_fd << std::endl;
    m_local_addr = std::make_shared<tinyrpc::IPAddress>("127.0.0.1", 0);
    m_reactor = Reactor::GetReactor();

    if (type == Http_Protocal) {
        std::cout << "调试日志: [在TcpClient类构造函数中] 基于HTTP协议的RPC服务尚未实现, 当前程序退出" << std::endl;
    } else {
        m_codec = std::make_shared<TinyPbCodeC>();
    }
    m_connection = std::make_shared<TcpConnection>(this, m_reactor, m_fd, 128, m_peer_addr);
}

TcpClient::~TcpClient() {
    if (m_fd > 0) {
        FdEventContainer::GetFdContainer()->getFdEvent(m_fd)->unregisterFromReactor();
        close(m_fd);
        std::cout << "调试日志: [在TcpClinet类析构函数中] 文件描述符 " << m_fd << "被关闭" << std::endl;
    }
}

// 获取当前对象的TcpConnection
TcpConnection* TcpClient::getConnection() {
    if (!m_connection.get()) {  // 如果m_connection为空
        m_connection = std::make_shared<TcpConnection>(this, m_reactor, m_fd, 128, m_peer_addr);
    }
    return m_connection.get();
}

void TcpClient::resetFd() {
    tinyrpc::FdEvent::ptr fd_event = tinyrpc::FdEventContainer::GetFdContainer()->getFdEvent(m_fd);
    fd_event->unregisterFromReactor();
    close(m_fd);
    m_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_fd == -1) {
        std::cout << "错误日志: [在函数TcpClient::resetFd中] 调用socket函数出现错误, 返回值(fd) = -1" << 
            "sys error = " << "strerror(errno)" << std::endl;
    }
}

int TcpClient::sendAndRecvTinyPb(const std::string& msg_no, TinyPbStruct::pb_ptr& res) {
    // 1.向当前对象的m_reactor的定时器注册一个定时事件, 
    // 到时后is_timeout被置为true, 当前协程又开始运行.
    bool is_timeout = false;
    tinyrpc::Coroutine* cur_cor = tinyrpc::Coroutine::GetCurrentCoroutine();
    auto timer_cb = [this, &is_timeout, cur_cor]() {
        std::cout << "调试日志: [在lambda函数timer_cb中(在函数TcpClient::sendAndRecvTinyPb中定义)] " << 
            "TcpClient的定时器到时事件发生了" << std::endl;
        is_timeout = true;
        this->m_connection->setOverTimeFlag(true);
        tinyrpc::Coroutine::Resume(cur_cor);
    };
    TimerEvent::ptr event = std::make_shared<TimerEvent>(m_max_timeout, false, timer_cb);
    m_reactor->getTimer()->addTimerEvent(event);
    std::cout << "调试日志: [在函数TcpClient::sendAndRecvTinyPb中] 已添加RPC定时器事件, 定时器将在" <<
        event->m_arrive_time << "到时" << std::endl;
    
    // 2.通过connect_hook函数, 尝试连接服务器端
    while (!is_timeout) {  // 在定时器到时之前(超时之前), 即is_timeout=false时, 做这个连接的工作
        std::cout << "调试日志: [在函数TcpClient::sendAndRecvTinyPb中] 开始连接" << std::endl;
        if (m_connection->getState() != Connected) {  // 如果是未连接状态, 才连接, 否则直接break出while循环
            // 执行连接
            int rt = connect_hook(m_fd, reinterpret_cast<sockaddr*>(m_peer_addr->getSockAddr()), 
                m_peer_addr->getSockLen());
            if (rt == 0) {  // 如果连接成功, 进行相应处理
                std::cout << "调试日志: [在函数TcpClient::sendAndRecvTinyPb中] 成功连接服务器[" << 
                    m_peer_addr->toString() << "]!" << std::endl;  // 打印成功信息
                m_connection->setUpClient();  // 将m_connection的状态置为Connected
                break;
            }
            // 执行完connect_hook, 就取消第一步中注册的event(不管连接成功与否), 
            // 因为这个等待超时的事件时一次性的, 不需要设置成周期的,
            // 执行connect_hook后成功就成功, 不成功做错误处理
            resetFd();
            // 错误处理, 针对各种可能进行处理
            if (is_timeout) {  // 如果还没连接上, 定时器超时了
                std::cout << "信息日志: [在函数TcpClient::sendAndRecvTinyPb中] connection timeout, break" << std::endl;
                goto err_deal;
            }
            if (errno == ECONNREFUSED) {
                std::stringstream ss; 
                ss << "连接出现错误, 对端[" << m_peer_addr->toString() << "]已经关闭.";
                m_err_info = ss.str();
                std::cout << "错误日志: [在函数TcpClient::sendAndRecvTinyPb中] 取消overtime事件, 错误信息 = "
                    << m_err_info << std::endl;
                m_reactor->getTimer()->delTimerEvent(event);
                return ERROR_PEER_CLOSED;
            }
            if (errno == EAFNOSUPPORT) {
                std::stringstream ss; 
                ss << "connect cur sys ror, errinfo is" << std::string(strerror(errno)) << "] closed.";
                m_err_info = ss.str();
                std::cout << "错误日志: [在函数TcpClient::sendAndRecvTinyPb中] 取消overtime事件, 错误信息 = "
                    << m_err_info << std::endl;
                m_reactor->getTimer()->delTimerEvent(event);
                return ERROR_CONNECT_SYS_ERR;
            }
        } else {
            break;
        }
    }

    if (m_connection->getState() != Connected) {
        std::string ss = "连接对端[" + m_peer_addr->toString() + "]出现错误, sys error = " + std::string(strerror(errno));
        m_err_info = ss;
        m_reactor->getTimer()->delTimerEvent(event);
        return ERROR_FAILED_CONNECT;
    }

    // 3.调用output, 将m_write_buffer中的内容发送到服务器端
    m_connection->setUpClient();  // 将当前客户端的状态置为'Connected'
    m_connection->output();  // 将m_write_buffer中的内容发送到服务器端
    if (m_connection->getOverTimeFlag()) {  // 如果m_is_over_time=true
        std::cout << "信息日志: [在函数TcpClient::sendAndRecvTinyPb中] 发送数据超时" << std::endl;
        is_timeout = true;
        goto err_deal;
    }

    // 4.发送编号为msg_no的请求包之后, 客户端就期待收到服务器的回包, 因此
    // 循环执行检查编号为msg_no的请求报的回包是否收到了, 若没收到, 就执行
    // input读取回包; input返回之后, 执行execute将回包填入m_reply_datas
    while (!m_connection->getResPackageData(msg_no, res)) {
        std::cout << "调试日志: [在函数TcpClient::sendAndRecvTinyPb中] 重新执行getResPackageData函数" << std::endl;
        m_connection->input();  // 将从sockfd缓冲区中读入的回包写入到m_read_buffer中

        if (m_connection->getOverTimeFlag()) {
            std::cout << "调试日志: [在函数TcpClient::sendAndRecvTinyPb中] 读数据超时" << std::endl;
            is_timeout = true;
            goto err_deal;
        }
        if (m_connection->getState() == Closed) {
            std::cout << "调试日志: [在函数TcpClient::sendAndRecvTinyPb中] 对端已关闭" << std::endl;
            goto err_deal;
        }

        m_connection->execute();  // 将m_read_buffer中的回包解码(反序列化), 存入m_reply_datas
    }

    // 5.全都完成后, 就可以停止监听超时事件了
    m_reactor->getTimer()->delTimerEvent(event);
    m_err_info = "";
    return 0;

err_deal:  // 如果出现连接错误如何处理, goto语句之前是报出错误信息, 这里是做出响应处理
    // 1.应该关闭当前客户端使用的文件描述符m_fd, 并使用socket重新打开一个新的文件描述符
    FdEventContainer::GetFdContainer()->getFdEvent(m_fd)->unregisterFromReactor();
    close(m_fd);  // 该文件描述符是在本类构造函数中通过socket函数开启的
    m_fd = socket(AF_INET, SOCK_STREAM, 0);
    std::stringstream ss;
    // 2.然后更新m_err_info, 并返回
    if (is_timeout) {  // 如果是因为超时不成能成功连接
        ss << "调用RPC失败, 已经超过" << m_max_timeout << "毫秒(ms)";
        m_err_info = ss.str();
        m_connection->setOverTimeFlag(false);
        return ERROR_RPC_CALL_TIMEOUT;
    } else {
        ss << "调用RPC失败, 对端[" << m_peer_addr->toString() << "]已关闭";
        m_err_info = ss.str();
        return ERROR_PEER_CLOSED;
    }
}

void TcpClient::stop() {
    if (!m_is_stop) {
        m_is_stop = true;
        m_reactor->stop();
    }
}

}