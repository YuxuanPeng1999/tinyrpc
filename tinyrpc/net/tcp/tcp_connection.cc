#include <iostream>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include "tinyrpc/net/tcp/tcp_connection.h"
#include "tinyrpc/net/tcp/tcp_server.h"
#include "tinyrpc/net/tcp/tcp_client.h"
#include "tinyrpc/net/tinypb/tinypb_codec.h"
#include "tinyrpc/net/tinypb/tinypb_data.h"
#include "tinyrpc/coroutine/coroutine_hook.h"
#include "tinyrpc/coroutine/coroutine_pool.h"
#include "tinyrpc/net/tcp/tcp_connection_time_wheel.h"
#include "tinyrpc/net/tcp/abstract_slot.h"
#include "tinyrpc/net/timer.h"

namespace tinyrpc {

// 初始化用于TCP服务端的Connnection对象
TcpConnection::TcpConnection(tinyrpc::TcpServer* tcp_svr, tinyrpc::IOThread* io_thread, 
    int fd, int buff_size, NetAddress::ptr peer_addr): 
    m_io_thread(io_thread),
    m_fd(fd),
    m_state(Connected),
    m_connection_type(ServerConnection),
    m_peer_addr(peer_addr) {
    // 构造函数开始
    std::cout << "调试日志: [在TcpConnection构造函数中] TcpConnection构造函数开始" << std::endl;
    m_reactor = m_io_thread->getReactor();
    m_tcp_svr = tcp_svr;
    m_codec = m_tcp_svr->getCodec();
    m_fd_event = FdEventContainer::GetFdContainer()->getFdEvent(fd);
    m_fd_event->setReactor(m_reactor);
    initBuffer(buff_size);
    m_loop_cor = GetCoroutinePool()->getCoroutineInstance();
    m_state = Connected;
    std::cout << "调试日志: [在TcpConnection构造函数中] TcpConnection构造函数结束. 成功创建了一个TcpConenction实例" << std::endl;
}

// 初始化用于TCP客户端的Connnection对象
TcpConnection::TcpConnection(tinyrpc::TcpClient* tcp_cli, tinyrpc::Reactor* reactor, 
    int fd, int buff_size, NetAddress::ptr peer_addr): 
    m_fd(fd),
    m_state(NotConnected),
    m_connection_type(ClientConnection),
    m_peer_addr(peer_addr) {
    m_reactor = reactor;
    m_tcp_cli = tcp_cli;
    m_codec = m_tcp_cli->getCodeC();
    m_fd_event = FdEventContainer::GetFdContainer()->getFdEvent(fd);
    m_fd_event->setReactor(m_reactor);
    initBuffer(buff_size);
    std::cout << "调试日志: [在TcpConnection类构造函数(TcpClient版本)中] 成功创建TCP连接对象(但是尚未连接服务端)" 
        << std::endl;
}

// 初始化缓冲区大小
void TcpConnection::initBuffer(int size) {
    m_write_buffer = std::make_shared<TcpBuffer>(size);
    m_read_buffer = std::make_shared<TcpBuffer>(size);
}

void TcpConnection::initServer() {
    std::cout << "调试日志: [在TcpConnection::initServer构造函数中] 本函数开始" << std::endl;
    registerToTimeWheel();
    m_loop_cor->setCallBack(std::bind(&TcpConnection::MainServerLoopCorFunc, this));
    std::cout << "调试日志: [在TcpConnection::initServer构造函数中] 本函数结束" << std::endl;
}

void TcpConnection::registerToTimeWheel() {
    // 1.新建一个槽
    auto cb = [](TcpConnection::ptr conn) {
        conn->shutdownConnection();
    };  // 槽的回调函数
    TcpTimeWheel::TcpConnectionSlot::ptr tmp = 
        std::make_shared<AbstractSlot<TcpConnection>>(shared_from_this(), cb);
    // 2.将这个槽注册到时间轮上
    m_weak_slot = tmp;
    m_tcp_svr->freshTcpConnection(tmp);  // 也就是调用时间轮的成员函数fresh, 但是为啥要用addTask完成?
}

void TcpConnection::shutdownConnection() {
    TcpConnectionState state = getState();
    // 1.先检查是否已经关闭
    if (state == Closed || state == NotConnected) {
        std::cout << "调试日志: [在函数TcpConnection::shutdownConnection中] 当前客户端已经关闭" << std::endl;
        return;
    }
    // 2.如果未关闭, 调用shutdown进行关闭
    setState(HalfClosing);
    std::cout << "信息日志: [在函数TcpConnection::shutdownConnection中] 关闭TCP连接[" << 
        m_peer_addr->toString() << "], fd = " << m_fd;
    // call sys shutdown to send FIN
    // wait client done something, client will send FIN
    // and fd occur read event but byte count is 0
    // then will call clearClient to set CLOSED
    // IOThread::MainLoopTimerFunc will delete CLOSED connection
    shutdown(m_fd_event->getFd(), SHUT_RDWR);
}

TcpConnection::~TcpConnection() {
    if (m_connection_type == ServerConnection) {
        GetCoroutinePool()->returnCoroutine(m_loop_cor);
    }
    std::cout << "调试日志: TcpConnection析构函数执行, fd = " << m_fd << std::endl;
}

void TcpConnection::setUpClient() {
    setState(Connected);
}

void TcpConnection::setUpServer() {
  m_reactor->addCoroutine(m_loop_cor);
}

TcpConnectionState TcpConnection::getState() {
    TcpConnectionState state;
    RWMutex::ReadLock lock(m_mutex);
    state = m_state;
    lock.unlock();

    return state;
}

void TcpConnection::setState(const TcpConnectionState& state) {
    RWMutex::WriteLock lock(m_mutex);
    m_state = state;
    lock.unlock();
}

TcpBuffer* TcpConnection::getInBuffer() {
    return m_read_buffer.get();
}

TcpBuffer* TcpConnection::getOutBuffer() {
    return m_write_buffer.get();
}

bool TcpConnection::getResPackageData(const std::string& msg_req, TinyPbStruct::pb_ptr& pb_struct) {
    auto it = m_reply_datas.find(msg_req);
    if (it != m_reply_datas.end()) {  // 如果m_reply_datas中有这条数据, 就将其返回
        std::cout << "调试日志: [在函数TcpConnection::getResPackageData中] 返回一条resdata" << std::endl;
        pb_struct = it->second;
        m_reply_datas.erase(it);
        return true;
    }
    std::cout << "调试日志: [在函数TcpConnection::getResPackageData中] " << msg_req << "|应答数据不存在" << std::endl;
    return false;
}

AbstractCodeC::ptr TcpConnection::getCodec() const {
    return m_codec;
}

void TcpConnection::setOverTimeFlag(bool value) {
    m_is_over_time = value;
}

bool TcpConnection::getOverTimeFlag() {
    return m_is_over_time;
}

Coroutine::ptr TcpConnection::getCoroutine() {
    return m_loop_cor;
}

// 服务器端的报文处理协程, 即服务器的SubReactor(I/O线程)的子协程
// (注意, 客户端的报文处理过程不是这样!!!)
void TcpConnection::MainServerLoopCorFunc() {
    while (!m_stop) {
        input();
        execute();
        output();
    }
    std::cout << "信息日志: [在函数TcpConnection::MainServerLoopCorFunc中] " <<
        "当前TcpConnection对象的loop已结束" << std::endl;
}

void TcpConnection::input() {
    std::cout << "调试日志: [在函数TcpConnection::input中] 本函数开始" << std::endl;
    // 1.状态检查
    if (m_is_over_time) {
        std::cout << "信息日志: [在函数TcpConnection::input中] 已超时, 跳过input步骤" << std::endl;
        return;
    }
    TcpConnectionState state = getState();
    if (state == Closed || state == NotConnected) {
        return;
    }
    // 2.从内核缓冲区读到的数据, 要放到本类的m_read_buffer中
    bool read_all = false;   // 是否已读完数据
    bool close_flag = false; // 当前连接是否需要被关闭(如果read_hook出错会被关闭)
    int count = 0;           // 计数循环结束之前读入的字符总数
    while (!read_all) {  // 开始从内核读入m_buffer
        // 2-1 先检查m_read_buffer是否还有可写的空白空间: 如果没了, 就扩展到原来的两倍
        if (m_read_buffer->writeAble() == 0) {
            m_read_buffer->resizeBuffer(2 * m_read_buffer->getSize());
        }
        // 2-2 然后通过read_hook将内核缓冲区内容读入m_read_buffer, 并更新writeIndex
        int read_count = m_read_buffer->writeAble();   // m_read_buffer的剩余可写空间大小
        int write_index = m_read_buffer->writeIndex(); // 要向m_read_buffer写入字符, 应从多少地址开始
        std::cout << "调试日志: [在函数TcpConnection::input中] m_read_buffer: size = " <<
            m_read_buffer->getBufferVector().size() << 
            ", rd(即readIndex) = " << m_read_buffer->readIndex() << 
            ", wd(即writeIndex) = " << m_read_buffer->writeIndex() << std::endl;
        int rt = read_hook(m_fd, &(m_read_buffer->m_buffer[write_index]), read_count);
        // 2-3 处理读取结果: 
        // - 读取成功, 更新writeIndex
        // - 读取失败, 停止循环read操作(即当前循环)
        // - 读取成功, 还要判断是否读完了: 要是这次读的字符没把m_read_buffer填满, 那就是读完了
        if (rt > 0) {
            m_read_buffer->recycleWrite(rt);  // writeIndex
            std::cout << "调试日志: [在函数TcpConnection::input中] 成功执行read_hook, " << 
                "从内核缓冲区读入数据, 存储在m_read_buffer当中" << std::endl;
        }
        std::cout << "调试日志: [在函数TcpConnection::input中] m_read_buffer: size = " <<
            m_read_buffer->getBufferVector().size() << 
            ", rd(即readIndex) = " << m_read_buffer->readIndex() << 
            ", wd(即writeIndex) = " << m_read_buffer->writeIndex() << std::endl;
        count += rt;  // 更新while循环中读入的字符总数
        if (m_is_over_time) {
            std::cout << "调试日志: [在函数TcpConnection::input中] 已超时, 循环read操作中断" << std::endl;
            break;
        }
        if (rt <= 0) {  // 如果read_hook返回值<=0, 说明read_hook的执行出错了, 也是中断当前的循环read操作
            std::cout << "调试日志: [在函数TcpConnection::input中] read_hook函数返回值rt <= 0" << std::endl;
            std::cout << "错误日志: [在函数TcpConnection::input中] " << 
                "read empty while occur read event, because of peer close, fd= " << m_fd <<
                ", sys error = " << strerror(errno) << ", now to clear tcp connection" <<  std::endl;
            close_flag = true;
            break;
        } else {
            if (rt == read_count) {  // 写入的字符数等于预留的最大空间, 说明
                // read_hook读到的字符填满了m_read_buffer, 这种情况下可能
                // 内核缓冲区中还有没有被读取的字符, 要继续当前的循环read操作
                std::cout << "调试日志: [在函数TcpConnection::input中] read_count = rt, " << 
                    "read_hook从内核缓冲区读到的字符把m_read_buffer全都填满了, " << 
                    "说明内核缓冲区的字符可能没被读完, 因此继续循环read操作" << std::endl;
                continue;
            } else if (rt < read_count) {  // 反之, 如果read_hook读到的字符未能将m_read_buffer填满, 
                // 则说明已经把内核缓冲区的字符都读完了, 因此可以中断当前的循环read操作
                std::cout << "调试日志: [在函数TcpConnection::input中] read_count < rt, " << 
                    "read_hook从内核缓冲区读到的字符把m_read_buffer全都填满了, " << 
                    "说明核缓冲区的字符都读完了, 因此中断循环read操作" << std::endl;
                read_all = true;
                break;
            }
        }
    }
    // 3.rt<0, 跳出循环后的进一步处理: 
    if (close_flag) {
        clearClient();
        std::cout << "调试日志: [在函数TcpConnection::input中] peer close, " << 
            "当前协程中止, 等待主线程清理此TcpConnection" << std::endl;
        Coroutine::GetCurrentCoroutine()->setCanResume(false);
        Coroutine::Yield();
    }
    // 4.
    if (m_is_over_time) {
        return;
    }
    if (!read_all) {
        std::cout << "错误日志: [在函数TcpConnection::input中] 未能将内核缓冲区中所有数据都读完" << std::endl;
    }
    std::cout << "信息日志: [在函数TcpConnection::input中] 从对端[" << 
        m_peer_addr->toString()  << "]接收到[" << count << "]字节数据, 当前连接的fd = " <<
        m_fd << std::endl; 
    if (m_connection_type == ServerConnection) {
        TcpTimeWheel::TcpConnectionSlot::ptr tmp = m_weak_slot.lock();
        if (tmp) {
            m_tcp_svr->freshTcpConnection(tmp);
        }
    }
    std::cout << "调试日志: [在函数TcpConnection::input中] 本函数结束" << std::endl;
}

void TcpConnection::clearClient() {
    // 1.状态检查
    if (getState() == Closed) {
        std::cout << "调试日志: [在函数TcpConnection::clearClient中] 当前客户端已经关闭" << std::endl;
        return;
    }
    // 2.从reactor注销该事件
    m_fd_event->unregisterFromReactor();
    // 3.停止m_loop_cor的回调函数
    m_stop = true;
    // 4.其他
    close(m_fd_event->getFd());
    setState(Closed);
}

void TcpConnection::execute() {
    std::cout << "调试日志: [在函数TcpConnection::execute中] 本函数开始" << std::endl;
    while (m_read_buffer->readAble() > 0) {
        // 1.读到报文后, 首先创建一个空的Abstractdata类对象, 然后对报文进行解码
        // 1-1 创建空的Abstractdata类对象data
        std::shared_ptr<AbstractData> data;
        if (m_codec->getProtocalType() == TinyPb_Protocal) {
            data = std::make_shared<TinyPbStruct>();
        } else {
            std::cout << "调试日志: [在函数TcpConnection::execute中] 目前尚不支持HTTP协议" << std::endl;
            break;
        }
        // 1-2 对m_read_buffer中的报文进行解码, 将所得数据填充到data中
        m_codec->decode(m_read_buffer.get(), data.get());
        if (!data->decode_succ) {
            std::cout << "错误日志: [在函数TcpConnection::execute中] 解析请求时出错, 对应fd = " << m_fd << std::endl;
            break;
        } else {
            std::cout << "调试日志: [在函数TcpConnection::execute中] 成功解码fd[" << m_fd << "]收到的请求报文" << std::endl;
        }
        // 2.接下来对报文进行处理, 服务器端连接和客户端连接对报文有不同处理
        if (m_connection_type == ServerConnection) {
            // 2-1 服务器端: 说明data是客户端发来的请求包, 
            // 则使用dispatch解析包、调用相应方法、将应答包填入m_write_buufer
            m_tcp_svr->getDispatcher()->dispatch(data.get(), this);
        } else if (m_connection_type == ClientConnection) {
            // 2-2 客户端: 说明data是服务器端发来的应答包, 
            // 则将data填入m_reply_datas中, 待处理
            std::shared_ptr<TinyPbStruct> tmp = std::dynamic_pointer_cast<TinyPbStruct>(data);
            if (tmp) {
                m_reply_datas.insert(std::make_pair(tmp->msg_req, tmp));
            }
        }
    }
    std::cout << "调试日志: [在函数TcpConnection::execute中] 本函数结束" << std::endl;
}

void TcpConnection::output() {
    // 1.状态检查
    if (m_is_over_time) {
        std::cout << "信息日志: [在函数TcpConnection::output中] 已超时, 跳过output步骤" << std::endl;
        return;
    }
    // 2.循环向socket写入字符
    while (true) {
        // 2-1 状态检查
        TcpConnectionState state = getState();
        if (state != Connected) {
            break;
        }
        if (m_write_buffer->readAble() == 0) {
            // m_write_buffer中的内容是要写入socket的, 如果readAble()为0, 
            // 即writeIndex-readIndex为0, 说明没有内容需要写入socket
            std::cout << "调试日志: [在函数TcpConnection::output中] 无数据需要写入socket, 中止当前协程" << std::endl;
            break;
        }
        // 2-2 向socket(即m_fd)写入数据
        int total_size = m_write_buffer->readAble();  // 要写入socket的字符总数
        int read_index = m_write_buffer->readIndex();
        int rt = write_hook(m_fd, &(m_write_buffer->m_buffer[read_index]), total_size);
        if (rt <= 0) { // 错误处理
            std::cout << "错误日志: [在函数TcpConnection::output中] 没能写入任何字符, error=" << 
                strerror(errno) << std::endl;
        }
        std::cout << "调试日志: [在函数TcpConnection::output中] 成功写入" << rt << "字节字符" << std::endl;
        m_write_buffer->recycleRead(rt);  // 更新m_write_buffer->m_read_index
        std::cout << "调试日志: [在函数TcpConnection::output中] m_write_buffer被更新: " << 
            "m_write_index = " << m_write_buffer->writeIndex() << 
            ", m_read_index = " << m_write_buffer->readIndex() << 
            "readAble() = " << m_write_buffer->readAble() << std::endl;
        std::cout << "信息日志: [在函数TcpConnection::output中] 向 fd[" << m_fd << "]写入了" <<
            rt << "字节数据, 发送给[" << m_peer_addr->toString() << "]" << std::endl;
        // 2-3 判断停止条件: 
        if (m_write_buffer->readAble() <= 0) {  // 如果已经没有需要从m_write_buffer中读取并写入socket的数据, 则停止循环
            std::cout << "已将所有数据发送, 注销当前写事件, 中断write循环" << std::endl;
            break;
        }
        if (m_is_over_time) {  // 如果超时, 也停止循环
            std::cout << "信息日志: [在函数TcpConnection::output中] 超时, 中断当前循环" << std::endl;
            break;
        }
    }
}

}