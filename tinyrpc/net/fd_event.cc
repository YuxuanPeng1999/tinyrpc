#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include "fd_event.h"

namespace tinyrpc {

static FdEventContainer* g_FdContainer = nullptr;

FdEvent::FdEvent(tinyrpc::Reactor* reactor, int fd): m_fd(fd), m_reactor(reactor) {
    if (reactor == nullptr) {
        std::cout << "错误日志: FdEvent类构造函数错误: 请先创建一个reactor" << std::endl;
    }
}

FdEvent::FdEvent(int fd): m_fd(fd) { }

FdEvent::~FdEvent() { }

void FdEvent::handleEvent(int flag) {
    if (flag == READ) {
        m_read_callback();
    } else if (flag == WRITE) {
        m_write_callback();
    } else {
        std::cout << "错误日志: [在函数FdEvent::handleEvent中] 传入的flag(事件类型)错误" << std::endl;
    }
}

void FdEvent::setCallBack(IOEvent flag, std::function<void()> cb) {
    if (flag == READ) {
        m_read_callback = cb;
    } else if (flag == WRITE) {
        m_write_callback = cb;
    } else {
        std::cout << "错误日志: FdEvent::setCallBack函数出错, flag参数错误" << std::endl;
    }
}

std::function<void()> FdEvent::getCallBack(IOEvent flag) const {
    if (flag == READ) {
        return m_read_callback;
    } else if (flag == WRITE) {
        return m_write_callback;
    }
    return nullptr;
}

// enum IOEvent { READ = EPOLLIN, WRITE = EPOLLOUT, ETModel = EPOLLET };
void FdEvent::addListenEvents(IOEvent event) {
    if (m_listen_events & event) {
        std::cout << "调试日志: [在函数FdEvent::addListenEvents中] 已添加该事件, 函数结束" << std::endl;
        return;
    }
    m_listen_events |= event;
    updateToReactor();
}

void FdEvent::updateToReactor() {
    // 1.先创建要监听的epoll_event对象
    epoll_event event;
    event.events = m_listen_events;
    event.data.ptr = this;
    // 2.然后添加到本事件对应的reactor当中
    // 2-1 如果本事件还没与任何reactor对应, 那么先获取reactor
    if (!m_reactor) {
        m_reactor = tinyrpc::Reactor::GetReactor();
    }
    m_reactor->addEvent(m_fd, event);
}

void FdEvent::delListenEvents(IOEvent event) {
    if (m_listen_events & event) {  // 如果m_listen_events包含这件事, 再谈删除event
        std::cout << "调试日志: [在函数FdEvent::delListenEvents中] 成功删除" << std::endl;
        m_listen_events &= ~event;
        updateToReactor();
        return;
    }
    std::cout << "调试日志: [在函数FdEvent::delListenEvents中] 输入的事件不存在, " << 
        "无法删除, 当前函数直接返回" << std::endl;
}

void FdEvent::unregisterFromReactor() {
    if (!m_reactor) {
        m_reactor = tinyrpc::Reactor::GetReactor();
    }
    m_reactor->delEvent(m_fd);
    m_listen_events = 0;
    m_read_callback = nullptr;
    m_write_callback = nullptr;
}

int FdEvent::getFd() const {
    return m_fd;
}

void FdEvent::setFd(const int fd) {
    m_fd = fd;
}

int FdEvent::getListenEvents() const {
    return m_listen_events;
}

Reactor* FdEvent::getReactor() const {
    return m_reactor;
}

void FdEvent::setReactor(Reactor* r) {
    m_reactor = r;
}

void FdEvent::setNonBlock() {
    // 1.错误检查
    if (m_fd == -1) {
        std::cout << "错误日志: [在函数FdEvent::setNonBlock中] fd = -1, 当前函数无法执行" << std::endl;
        return;
    }
    // 2.开始设置
    // 2-1 获取当前fd的flag
    int flag = fcntl(m_fd, F_GETFL, 0);
    if (flag & O_NONBLOCK) {
        std::cout << "调试日志: [在函数FdEvent::setNonBlock中] fd早已被设为非阻塞, 当前函数退出" << std::endl;
        return;
    }
    // 2-2 在flag中添加O_NONBLOCK事件
    fcntl(m_fd, F_SETFL, flag | O_NONBLOCK);
    // 2-3 验证添加是否成功
    flag = fcntl(m_fd, F_GETFL, 0);
    if (flag & O_NONBLOCK) {
        std::cout << "调试日志: [在函数FdEvent::setNonBlock中] 成功将fd设为非阻塞" << std::endl;
    } else {
        std::cout << "调试日志: [在函数FdEvent::setNonBlock中] 将fd设为非阻塞失败" << std::endl;
    }
}

bool FdEvent::isNonBlock() {
    if (m_fd == -1) {
        std::cout << "错误日志: [在函数FdEvent::setNonBlock中] fd = -1, 当前函数无法执行" << std::endl;
        return false;
    }
    int flag = fcntl(m_fd, F_GETFL, 0);  // 获取flag
    return (flag & O_NONBLOCK);  // 返回检查结果
}

void FdEvent::setCoroutine(Coroutine* cor) {
    m_coroutine = cor;
}

void FdEvent::clearCoroutine() {
    m_coroutine = nullptr;
}

Coroutine* FdEvent::getCoroutine() {
    return m_coroutine;
}

// FdEventContainer类构造函数: 在FdEvent指针数组m_fds中添加
// 文件描述符0, 1, 2, ..., size - 1对应的FdEvent指针.
FdEventContainer::FdEventContainer(int size) {
    for (int i = 0; i < size; ++i) {
        m_fds.push_back(std::make_shared<FdEvent>(i));
    }
}

FdEvent::ptr FdEventContainer::getFdEvent(int fd) {
    // 1.如果fd(对应的FdEvent)在m_fds中存在, 则直接将其返回
    RWMutex::ReadLock rlock(m_mutex);
    if (fd < static_cast<int>(m_fds.size())) {
        tinyrpc::FdEvent::ptr re = m_fds[fd];
        rlock.unlock();
        return re;
    }
    rlock.unlock();
    // 2.若不存在, 首先扩充m_fds的大小, 然后返回fd对应的FdEvent
    RWMutex::WriteLock wlock(m_mutex);
    // 2-1 扩充大小
    int n = (int)(fd * 1.5);
    for (int i = m_fds.size(); i < n; ++i) {
        m_fds.push_back(std::make_shared<FdEvent>(i));
    }
    // 2-2 返回fd对应的FdEvent
    tinyrpc::FdEvent::ptr re = m_fds[fd];
    wlock.unlock();
    return re;
}

FdEventContainer* FdEventContainer::GetFdContainer() {
    if (g_FdContainer == nullptr) {
        g_FdContainer = new FdEventContainer(1000);
    }
    return g_FdContainer;
}

}