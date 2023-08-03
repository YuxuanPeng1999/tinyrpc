#ifndef TINYRPC_NET_TCP_TCP_CONNECTION_H
#define TINYRPC_NET_TCP_TCP_CONNECTION_H

#include <memory>
#include <vector>
#include <queue>
// #include "tinyrpc/comm/log.h"
#include "tinyrpc/net/fd_event.h"
#include "tinyrpc/net/reactor.h"
#include "tinyrpc/net/tcp/tcp_buffer.h"
#include "tinyrpc/coroutine/coroutine.h"
#include "tinyrpc/net/http/http_request.h"
#include "tinyrpc/net/tinypb/tinypb_codec.h"
#include "tinyrpc/net/tcp/io_thread.h"
#include "tinyrpc/net/tcp/tcp_connection_time_wheel.h"
#include "tinyrpc/net/tcp/abstract_slot.h"
#include "tinyrpc/net/net_address.h"
#include "tinyrpc/net/mutex.h"

namespace tinyrpc {

class TcpServer;
class TcpClient;
class IOThread;

enum TcpConnectionState {
    NotConnected = 1, // can do I/O
    Connected = 2,    // can do I/O
    HalfClosing  = 3, // 半关闭状态: 服务器已调用shutdown函数, 写入本关闭, 可以读, 但是不能写
    Closed = 4,       // can do I/O
}; 

class TcpConnection: public std::enable_shared_from_this<TcpConnection> {
public:
    typedef std::shared_ptr<TcpConnection> ptr;

public:
    enum ConnectionType {
        ServerConnection = 1,  // TCP服务端的Connection对象
        ClientConnection = 2,  // TCP客户端的Connection对象
    };

public:
    TcpConnection(tinyrpc::TcpServer* tcp_svr, tinyrpc::IOThread* io_thread, 
        int fd, int buff_size, NetAddress::ptr peer_addr);
    TcpConnection(tinyrpc::TcpClient* tcp_cli, tinyrpc::Reactor* reactor, 
        int fd, int buff_size, NetAddress::ptr peer_addr);
    ~TcpConnection();
    void initBuffer(int size);

public:
    void setUpClient();  // 将Client设为Connected
    void setUpServer();

public:
    void shutdownConnection();
    TcpConnectionState getState();
    void setState(const TcpConnectionState& state);
    TcpBuffer* getInBuffer();
    TcpBuffer* getOutBuffer();
    AbstractCodeC::ptr getCodec() const;
    bool getResPackageData(const std::string& msg_req, TinyPbStruct::pb_ptr& pb_struct);
    void registerToTimeWheel();
    Coroutine::ptr getCoroutine();

public:
    void MainServerLoopCorFunc();
    void input();
    void execute();
    void output();
    void setOverTimeFlag(bool value);
    bool getOverTimeFlag();
    void initServer();

private:
    void clearClient();

private:
    TcpServer* m_tcp_svr {nullptr};
    TcpClient* m_tcp_cli {nullptr};
    IOThread* m_io_thread {nullptr};
    Reactor* m_reactor {nullptr};

    int m_fd {-1};
    TcpConnectionState m_state {TcpConnectionState::Connected};
    ConnectionType m_connection_type {ServerConnection};

    NetAddress::ptr m_peer_addr;

    TcpBuffer::ptr m_read_buffer;  // 存放接收到的报文
    TcpBuffer::ptr m_write_buffer; // 存放要返回的报文(返回数据是根据接收数据执行函数execute得到的)

    Coroutine::ptr m_loop_cor;
    AbstractCodeC::ptr m_codec;  // 编解码器对象
    FdEvent::ptr m_fd_event;
    bool m_stop {false};
    bool m_is_over_time {false};
    std::map<std::string, std::shared_ptr<TinyPbStruct>> m_reply_datas;
    std::weak_ptr<AbstractSlot<TcpConnection>> m_weak_slot;
    RWMutex m_mutex;
};

}

#endif