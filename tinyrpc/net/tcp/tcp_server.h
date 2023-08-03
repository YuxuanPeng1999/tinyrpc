#ifndef TINYRPC_NET_TCP_TCP_SERVER_H
#define TINYRPC_NET_TCP_TCP_SERVER_H

#include <map>
#include <google/protobuf/service.h>
#include "tinyrpc/net/reactor.h"
#include "tinyrpc/net/fd_event.h"
#include "tinyrpc/net/timer.h"
#include "tinyrpc/net/net_address.h"
#include "tinyrpc/net/tcp/tcp_connection.h"
#include "tinyrpc/net/tcp/io_thread.h"
#include "tinyrpc/net/tcp/tcp_connection_time_wheel.h"
#include "tinyrpc/net/abstract_codec.h"
#include "tinyrpc/net/abstract_dispatcher.h"
#include "tinyrpc/net/http/http_dispatcher.h"
#include "tinyrpc/net/http/http_servlet.h"

namespace tinyrpc {

class TcpAcceptor {
public:
    typedef std::shared_ptr<TcpAcceptor> ptr;
    TcpAcceptor(NetAddress::ptr net_addr);
    void init();  // 创建监听套接字、设置端口复用、绑定、开始监听
    int toAccept();  // 封装调用accept_hook函数的过程
    ~TcpAcceptor();  // 关闭listenfd

public:
    NetAddress::ptr getLocalAddr() {
        return m_local_addr;
    }

    NetAddress::ptr getPeerAddr() {
        return m_peer_addr;
    }

private:
    int m_family {-1};
    int m_fd {-1};  // listenfd(lfd)
    NetAddress::ptr m_local_addr {nullptr}; 
    NetAddress::ptr m_peer_addr {nullptr};  // 通过accept_hook函数, 得到的clientfd(cfd)
};

class TcpServer {
public: 
    typedef std::shared_ptr<TcpServer> ptr;
    TcpServer(NetAddress::ptr addr, ProtocalType type = TinyPb_Protocal);
    ~TcpServer();

public:
    void start();
    void addCoroutine(tinyrpc::Coroutine::ptr cor);
    bool registerService(std::shared_ptr<google::protobuf::Service> service);
    bool registerHttpServlet(const std::string& url_path, HttpServlet::ptr servlet);
    TcpConnection::ptr addClient(IOThread* io_thread, int fd);
    void freshTcpConnection(TcpTimeWheel::TcpConnectionSlot::ptr slot);

public:
    AbstractDispatcher::ptr getDispatcher();
    AbstractCodeC::ptr getCodec();
    NetAddress::ptr getPeerAddr();
    NetAddress::ptr getLocalAddr();
    IOThreadPool::ptr getIOThreadPool();
    TcpTimeWheel::ptr getTimeWheel();

private:
    void MainAcceptCorFunc();    // 
    void ClearClientTimerFunc(); // 清除超市客户端事件的回调函数

private:
    NetAddress::ptr m_addr;  // 监听地址
    TcpAcceptor::ptr m_acceptor;  // 用于接受的对象
    int m_tcp_counts {0}; 
    Reactor* m_main_reactor {nullptr};
    bool m_is_stop_accept {nullptr};
    Coroutine::ptr m_accept_cor {nullptr};  // 子协程, 用来运行m_acceptor(主协程运行epoll_wait)

private:  // RPC中需要用到的属性
    AbstractDispatcher::ptr m_dispatcher;
    AbstractCodeC::ptr m_codec;
    IOThreadPool::ptr m_io_pool;
    ProtocalType m_protocal_type {TinyPb_Protocal};

    TcpTimeWheel::ptr m_time_wheel;
    std::map<int, std::shared_ptr<TcpConnection>> m_clients;
    TimerEvent::ptr m_clear_client_timer_event {nullptr};
};

}

#endif