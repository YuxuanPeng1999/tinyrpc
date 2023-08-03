#include <iostream>
#include <sys/socket.h>
#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include "tinyrpc/net/tcp/tcp_server.h"
#include "tinyrpc/net/tcp/tcp_connection.h"
#include "tinyrpc/net/tcp/io_thread.h"
#include "tinyrpc/net/tcp/tcp_connection_time_wheel.h"
#include "tinyrpc/coroutine/coroutine.h"
#include "tinyrpc/coroutine/coroutine_hook.h"
#include "tinyrpc/coroutine/coroutine_pool.h"
#include "tinyrpc/comm/config.h"
#include "tinyrpc/net/http/http_codec.h"
#include "tinyrpc/net/http/http_dispatcher.h"
#include "tinyrpc/net/tinypb/tinypb_rpc_dispatcher.h"
#include "tinyrpc/comm/log.h"

namespace tinyrpc {

extern tinyrpc::Config::ptr gRpcConfig;

TcpAcceptor::TcpAcceptor(NetAddress::ptr net_addr): m_local_addr(net_addr) {
    m_family = m_local_addr->getFamily();
}

void TcpAcceptor::init() {
    // 1.创建监听套接字
    m_fd = socket(m_local_addr->getFamily(), SOCK_STREAM, 0);
    if (m_fd < 0) {
        std::cout << "错误日志: [在函数TcpAcceptor::init中] 启动服务器出现错误. socket函数出错, sys error = " << strerror(errno) << std::endl;
        Exit(0);
    }
    std::cout << "调试日志: [在函数TcpAcceptor::init中] 成功创建监听文件描述符, listenfd = " << m_fd << std::endl;

    // 2.设置端口复用
    int val = 1;
    if (setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0) {
        std::cout << "错误日志: [在函数TcpAcceptor::init中] 设置端口复用出现错误" << std::endl;
    }
    std::cout << "调试日志: [在函数TcpAcceptor::init中] 成功设置端口复用" << std::endl;

    // 3.绑定
    socklen_t len = m_local_addr->getSockLen();
    int rt = bind(m_fd, m_local_addr->getSockAddr(), len);
    if (rt != 0) {
        perror("bind");
        Exit(0);
    }

    // 4.开始监听
    rt = listen(m_fd, 10);
    if (rt != 0) {
        perror("listen");
        Exit(0);
    }
}

TcpAcceptor::~TcpAcceptor() {
    FdEvent::ptr fd_event = FdEventContainer::GetFdContainer()->getFdEvent(m_fd);
    fd_event->unregisterFromReactor();
    if (m_fd != -1) {
        close(m_fd);
    }
}

/*
只会在如下几种情况下返回-1: 
- 协议族不是AF_INET
- 调用accept_hook的是主协程
*/
int TcpAcceptor::toAccept() {
    socklen_t len = 0;
    int rt = 0;
    if (m_family == AF_INET) {
        sockaddr_in cli_addr;
        memset(&cli_addr, 0, sizeof(cli_addr));
        len = sizeof(cli_addr);
        rt = accept_hook(m_fd, reinterpret_cast<sockaddr*>(&cli_addr), &len);
        if (rt == -1) {
            perror("accept_hook");
            return -1;
        }
        // std::cout << "信息日志: [在函数TcpAcceptor::toAccept中] 成功接受了一个新的客户端! 端口号: " << cli_addr.sin_port << std::endl;
        m_peer_addr = std::make_shared<IPAddress>(cli_addr);
    } else if (m_family == AF_UNIX) {
        std::cout << "调试日志: [在函数TcpAcceptor::toAccept中] 接收到一个UNIX本地域协议族地址, 暂时略去此种情况的处理" << std::endl;
        return -1;
    } else {
        std::cout << "错误日志: [在函数TcpAcceptor::toAccept中] 协议类型未知!" << std::endl;
        return -1;
    }

    std::cout << "信息日志: [在函数TcpAcceptor::toAccept中] 成功接受新的客户端! fd = [" << rt << "], addr: " 
        << m_peer_addr->toString() << "]" << std::endl;
    
    return rt;
}

TcpServer::TcpServer(NetAddress::ptr addr, ProtocalType type): m_addr(addr) {
    // 创建m_iothread_num个IOThread, 放入IOThreadPool当中
    m_io_pool = std::make_shared<IOThreadPool>(gRpcConfig->m_iothread_num);
    if (type == Http_Protocal) {
        std::cout << "错误日志: [在TcpServer类的构造函数中] 目前尚不支持HTTP协议, TCP服务器创建失败" << std::endl;
        return;
    } else {
        m_dispatcher = std::make_shared<TinyPbRpcDispatcher>();
        m_codec = std::make_shared<TinyPbCodeC>();
        m_protocal_type = TinyPb_Protocal;
    }

    // 初始化main reactor
    m_main_reactor = tinyrpc::Reactor::GetReactor();
    m_main_reactor->setReactorType(MainReactor);

    m_time_wheel = std::make_shared<TcpTimeWheel>(m_main_reactor, 
        gRpcConfig->m_timewheel_bucket_num, gRpcConfig->m_timewheel_inteval);
    
    // 注册客户端超时清除事件
    m_clear_client_timer_event = std::make_shared<TimerEvent>(10000, true, 
        std::bind(&TcpServer::ClearClientTimerFunc, this));
    m_main_reactor->getTimer()->addTimerEvent(m_clear_client_timer_event);

    std::cout << "信息日志: [在TcpServer类的构造函数中] TcpServer启动了, 地址: " << m_addr->toString() << "]" << std::endl;
}

void TcpServer::start() {
    // 1.设置TcpAcceptor协程
    // 创建TcpAcceptor对象
    m_acceptor.reset(new TcpAcceptor(m_addr));
    // TcpAcceptor对象初始化(监听准备四部曲: 创建listenfd, 端口复用, 绑定当前地址, 开始listen)
    m_acceptor->init();
    // 创建本线程的主协程、协程池(有协程池必有协程内存池), 
    // 为TcpAcceptor对象分配一个协程, 命名为m_accept_cor
    m_accept_cor = GetCoroutinePool()->getCoroutineInstance();
    // 设置m_accept_cor协程的回调函数
    m_accept_cor->setCallBack(std::bind(&TcpServer::MainAcceptCorFunc, this));

    // 启动TcpAcceptor协程
    std::cout << "信息日志: [在函数TcpServer::start中] 从主协程切换到「接受协程(m_accept_cor)」" << std::endl;
    tinyrpc::Coroutine::Resume(m_accept_cor.get()); // 切换到子协程: 只有当accept_hook接收到新连接, Resume函数才会返回

    // 2.恐怕子协程m_accept_cor执行到accept_hook就会Yield, 返回这里
    // 启动IOThreadPool中的所有线程, IOThread即SubReactor, 启动线程后所有SubReactor的loop函数均开始运行
    m_io_pool->start();
    // 运行MainReactor的主协程的loop函数
    m_main_reactor->loop();
}

TcpServer::~TcpServer() {
    GetCoroutinePool()->returnCoroutine(m_accept_cor);
    std::cout << "调试日志: [在TcpServer类析构函数中] ~TcpServer" << std::endl;
}

void TcpServer::MainAcceptCorFunc() {
    while (!m_is_stop_accept) {
        // 1.接受新连接, 并获取socket文件描述符
        int fd = m_acceptor->toAccept();
        if (fd == -1) {  // 因为toAccept调用的是accept_hook, 因此只有协议族出错, 或者调用该函数的不是主协程才返回-1
            std::cout << "错误日志: [在函数TcpServer::MainAcceptCorFunc中] " << 
                "m_accept_cor->toAccept()执行错误, 返回-1, 当前函数返回, " << 
                "m_accept_cor执行yield, 切换回主协程" << std::endl;
            Coroutine::Yield();
            continue;
        }
        // 2.创建新连接并交给某个I/O线程
        // 2-1 获取一个线程
        IOThread *io_thread = m_io_pool->getIOThread();
        // 2-2 创建一个TCP连接对象, 并初始化
        TcpConnection::ptr conn = addClient(io_thread, fd);
        // 把自己的成员函数MainServerLoopCorFunc设置自己保存的协程m_loop_cor的协程回调函数
        conn->initServer();
        // 2-3 将这个连接对应的协程注册到I/O线程对应的SubReactor上
        // 实际上就是让Reactor对这个协程执行一个Resume动作
        std::cout << "调试日志: [在函数TcpServer::MainAcceptCorFunc中] 将一个m_loop_cor提交给I/O线程" << std::endl;
        io_thread->getReactor()->addCoroutine(conn->getCoroutine());
        // 2-4 更新相关信息
        m_tcp_counts++;
        std::cout << "调试日志: [在函数TcpServer::MainAcceptCorFunc中] 当前TCP连接数为[" << 
            m_tcp_counts << "]" << std::endl;
    }
}

bool TcpServer::registerService(std::shared_ptr<google::protobuf::Service> service) {
    if (m_protocal_type == TinyPb_Protocal) {
        if (service) {
            dynamic_cast<TinyPbRpcDispatcher*>(m_dispatcher.get())->registerService(service);
        } else {
            std::cout << "错误日志: [TcpServer::registerService] 注册服务时出错, 服务的指针为nullptr" << std::endl;
            return false;
        }
    } else {
        std::cout << "错误日志: [TcpServer::registerService] 注册服务时出错, 只有TinyPB协议的server需要注册服务" << std::endl;
        return false;
    }
    return true;
}

bool TcpServer::registerHttpServlet(const std::string& url_path, HttpServlet::ptr servlet) {
    std::cout << "错误日志: [TcpServer::registerHttpServlet] 本函数尚未实现" << std::endl;
    return false;
}

// 向m_clients添加新的TcpClient对象
TcpConnection::ptr TcpServer::addClient(IOThread* io_thread, int fd) {
    auto it = m_clients.find(fd);
    if (it != m_clients.end()) {  // 如果fd对应的TcpConnection对象已经存在
        std::cout << "调试日志: [在函数TcpServer::addClient中] 文件描述符 " << fd << 
            " 已经存在了, 将其对应的TcpConnection对象重新创建, 并替换原来的对象" << std::endl;
        it->second = std::make_shared<TcpConnection>(this, io_thread, fd, 128, getPeerAddr());
    } else {  // 如果fd对应的TcpConnection对象还不存在, 则创建之, 并返回该对象
        std::cout << "调试日志: [在函数TcpServer::addClient中] 文件描述符 " << fd << 
            " 不存在, 创建其对应的TcpConnection对象并返回" << std::endl;
        TcpConnection::ptr conn = std::make_shared<TcpConnection>(this, io_thread, fd, 128, getPeerAddr());
        return conn;
    }
}

// TcpConnectionSlot即AbstracSlot<TcpConnection>
// 让本类的成员变量m_time_wheel对某个TcpConnectionSlot::ptr(即一个slot, 或者说一个TcpConnection)
// 执行执行fresh函数
void TcpServer::freshTcpConnection(TcpTimeWheel::TcpConnectionSlot::ptr slot) {
    auto cb = [slot, this]() mutable {
        this->getTimeWheel()->fresh(slot);  // fresh(slot)就是把slot加到时间轮最后(而无需管slot的销毁)
        slot.reset();  // 这句似无必要, 该lambda函数结束后, slot所管理的对象的引用计数自会-1
    };
    m_main_reactor->addTask(cb);
}

// 删除m_clients中所有连接, 注意: 只有该链接非空, 且其use_count()返回值大于0, 
// 且其状态为Close, 该连接才能被删除.
void TcpServer::ClearClientTimerFunc() {
    for (auto& i: m_clients) {
        if (i.second && i.second.use_count() > 0 && i.second->getState() == Closed) {
            std::cout << "调试日志: [在函数TcpServer::ClearClientTimeFunc中] " << 
                "TcpConnection对象(对应fd: " << i.first << ")被删除, 该对象的状态为: " <<
                i.second->getState() << std::endl;
            (i.second).reset();
        }
    }
}

NetAddress::ptr TcpServer::getPeerAddr() {
    return m_acceptor->getPeerAddr();
}

NetAddress::ptr TcpServer::getLocalAddr() {
    return m_addr;
}

TcpTimeWheel::ptr TcpServer::getTimeWheel() {
    return m_time_wheel;
}

IOThreadPool::ptr TcpServer::getIOThreadPool() {
    return m_io_pool;
}

AbstractDispatcher::ptr TcpServer::getDispatcher() {
    return m_dispatcher;
}

AbstractCodeC::ptr TcpServer::getCodec() {
    return m_codec;
}

}