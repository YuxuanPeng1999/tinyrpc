#include <iostream>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <assert.h>
#include <string.h>
#include <algorithm>
#include "tinyrpc/comm/log.h"
#include "reactor.h"
#include "mutex.h"
#include "fd_event.h"
#include "timer.h"
#include "tinyrpc/coroutine/coroutine.h"
#include "tinyrpc/coroutine/coroutine_hook.h"

extern read_fun_ptr_t g_sys_read_fun;
extern write_fun_ptr_t g_sys_write_fun;

namespace tinyrpc {

// 属于本线程的reactor
static thread_local Reactor* t_reactor_ptr = nullptr;
static thread_local int t_max_epoll_timeout = 10000;  // ms
static CoroutineTaskQueue* t_coroutine_task_queue = nullptr;

Reactor::Reactor() {
    // 一个线程最多创建一个reactor
    if (t_reactor_ptr != nullptr) {
        std::cout << "错误日志: [在Reactor类构造函数中] 这个线程已经创建了一个reactor" << std::endl;
    }

    // 打印「创建reactor对象成功」信息
    m_tid = gettid();  // 初始化成员变量m_tid
    std::cout << "调试日志: [在Reactor类构造函数中] 线程[" << m_tid << "]成功创建了一个reactor对象" << std::endl;
    t_reactor_ptr = this;

    // 创建本reactor的epoll文件描述符
    if ((m_epfd = epoll_create(1)) <= 0) {
        std::cout << "错误日志: [在Reactor类构造函数中] 启动服务器时出现错误, epoll_create函数出错, sys error = " << strerror(errno) << std::endl;
        Exit(0);
    } else {
        std::cout << "调试日志: [在Reactor类构造函数中] m_epfd = " << m_epfd << std::endl;
    }

    // 创建m_wake_fd, 并将其加入epoll的监听列表
    // 1.创建
    if ((m_wake_fd = eventfd(0, EFD_NONBLOCK)) <= 0) {
        std::cout << "错误日志: [在Reactor类构造函数中] 启动服务器时出现错误, event_fd函数出错, sys error = " << strerror(errno) << std::endl;
        Exit(0);
    }
    std::cout << "调试日志: [在Reactor类构造函数中] wakefd = " << m_wake_fd << std::endl;
    // 2.加入监听列表
    addWakeupFd();  // 将wakefd加入epoll的监听时间列表, 监听其EPOLLIN事件
}

Reactor::~Reactor() {
    std::cout << "调试日志: [在Reactor类析构函数中] 执行Reactor的析构函数" << std::endl;
    // 主要是两个事情:
    // 1.关闭epoll文件描述符
    close(m_epfd);
    // 2.释放m_timer指向的内存
    if (m_timer != nullptr) {
        delete m_timer;
        m_timer = nullptr;
    }
    t_reactor_ptr = nullptr;
}

Reactor* Reactor::GetReactor() {
    if (t_reactor_ptr == nullptr) {
        std::cout << "调试日志: [在函数Reactor::GetReactor中] 创建新Reactor对象";
        t_reactor_ptr = new Reactor();
    }
    return t_reactor_ptr;
}

// 该函数由其他线程调用(FdEvent::updateToReactor), 需要锁
void Reactor::addEvent(int fd, epoll_event event, bool is_wakeup) {
    // 1.参数检查
    if (fd == -1) {
        std::cout << "错误日志: [在函数Reactor::addEvent中] 输入参数fd非法, fd = -1, 当前函数结束" << std::endl;
        return;
    }
    // 2.添加需要监听的事件event
    // 2-1 如果当前reactor在loop中, 就直接将事件event添加到m_epfd中
    if (isLoopThread()) {
        addEventInLoopThread(fd, event);
        return;
    }
    // 2-2 如果当前reactor不在loop中, 就先把事件event添加到m_pending_add_fds中
    // loop开始后, m_pending_add_fds中的事件会通过addEventInLoopThread被加入m_epfd中
    {
        Mutex::Lock lock(m_mutex);
        m_pending_add_fds.insert(std::pair<int, epoll_event>(fd, event));
    }
    if (is_wakeup) {
        wakeup();
    }
}

// 该函数由其他线程调用(FdEvent::unregisterFromReactor), 需要锁
void Reactor::delEvent(int fd, bool is_wakeup) {
    // 1.参数检查
    if (fd == -1) {
        std::cout << "错误日志: [在函数Reactor::delEvent中] 输入参数fd非法, fd = -1, 当前函数结束" << std::endl;
        return;
    }
    // 2.删除需要监听的文件描述符fd
    // 2-1 如果reactor正处于loop当中, 则直接在m_epfd中删除fd
    if (isLoopThread()) {
        delEventInLoopThread(fd);
        return;
    }
    // 2-2 如果reactor未处于loop当中, 就先把fd添加到m_pending_del_fds中
    // loop开始后, m_pending_del_fds中的事件会通过delEventInLoopThread被从m_epfd中删除
    {
        Mutex::Lock lock(m_mutex);
        m_pending_del_fds.push_back(fd);
    }
    if (is_wakeup) {
        wakeup();
    }
}

void Reactor::wakeup() {
    // 1.如果m_is_looping=false, 那就算了
    if (!m_is_looping) {
        return;
    }
    // 2.向wakeupfd中写入内容
    uint64_t tmp = 1;
    uint64_t* p = &tmp;
    if (g_sys_write_fun(m_wake_fd, p, 8) != 8) {
        std::cout << "错误日志: [在函数Reactor::wakeup中] 向wakeupfd[" << m_wake_fd 
            << "]写入内容时出现错误" << std::endl;
    }
}

// m_tid只能在Reactor::Reactor中修改, 因此不用对m_tid上锁
// 判断当前reactor对应的线程m_tid是否正在运行(即正在loop)
bool Reactor::isLoopThread() const {
    if (m_tid == gettid()) {
        return true;
    }
    return false;
}

void Reactor::addWakeupFd() {
    // 向epoll注册要监听的事件, 主要分两步
    // 1.创建epoll_event
    epoll_event event;         // 创建epoll_event对象
    event.data.fd = m_wake_fd; // 设置要监听的fd
    event.events = EPOLLIN;    // 设置要监听的事件
    // 2.注册, 使用epoll_ctl函数
    int op = EPOLL_CTL_ADD; // 指明要通过epoll_ctl函数做添加文件描述符的操作
    if ((epoll_ctl(m_epfd, op, m_wake_fd, &event)) != 0) {
        std::cout << "错误日志: [在函数Reactor::addWakeupFd中] epoll_ctl函数出现错误, fd[" << m_wake_fd << "], errno = " 
            << errno << ", err = " << strerror(errno) << std::endl;
    }
    m_fds.push_back(m_wake_fd);
}

// 无需互斥锁, 只有这个线程会调用该函数
void Reactor::addEventInLoopThread(int fd, epoll_event event) {
    assert(isLoopThread());

    int op = EPOLL_CTL_ADD;
    bool is_add = true;
    // 首先检查fd是否已经在m_fds中
    auto it = find(m_fds.begin(), m_fds.end(), fd);
    // 如果已经在m_fds中, 就把要做的操作改为「修改」(EPOLL_CTL_MOD)
    if (it != m_fds.end()) {
        is_add = false;
        op = EPOLL_CTL_MOD;
    }
    // 进行添加或者修改
    if (epoll_ctl(m_epfd, op, fd, &event) != 0) {
        std::cout << "错误日志: [在函数Reactor::addEventInLoopThread中] epoll_ctl函数出现错误, fd[" << fd << "], sys errinfo = " << strerror(errno) << std::endl;
        return;
    }
    // 如果之前执行的是添加(而非修改), 就要把新的文件描述符fd添加到m_fds中
    if (is_add) {
        m_fds.push_back(fd);
    }
    std::cout << "调试日志: [在函数Reactor::addEventInLoopThread中] epoll_ctl成功添加fd[" << fd << "]" << std::endl;
}

// 无需互斥锁, 只有这个线程会调用该函数
void Reactor::delEventInLoopThread(int fd) {
    assert(isLoopThread());
    // 首先检查fd是否已经在m_fds中
    auto it = find(m_fds.begin(), m_fds.end(), fd);
    // 如果不在, 那就无需执行删除
    if (it == m_fds.end()) {
        std::cout << "调试日志: [在函数Reactor::delEventInLoopThread中] fd[" << fd << "]本来就不在当前loop中" << std::endl;
        return;
    }
    // 如果在, 就执行删除
    int op = EPOLL_CTL_DEL;
    if ((epoll_ctl(m_epfd, op, fd, nullptr)) != 0) {
        std::cout << "调试日志: [在函数Reactor::delEventInLoopThread中] epoll_ctl函数出现错误, fd[" << fd << "], sys errinfo = " << strerror(errno) << std::endl;
    }
    m_fds.erase(it);
    std::cout << "调试日志: [在函数Reactor::delEventInLoopThread中] 已成功删除fd[" << fd << "]" << std::endl;
}

void Reactor::loop() {
    // 先检查正在loop的是不是当前线程, 如果不是, 那后面就不用执行了
    assert(isLoopThread());
    // 如果当前线程本来就已经在loop, 那本函数直接返回
    if (m_is_looping) {
        return;
    }
    // 否则, 修改属性, 开始loop
    m_is_looping = true;
    m_stop_flag = false;

    Coroutine* first_coroutine = nullptr;
    // 开始循环
    while (!m_stop_flag) {
        const int MAX_EVENTS = 10;
        epoll_event re_events[MAX_EVENTS + 1];
        
        // 应开始一些预处理
        if (first_coroutine) {  // 如果first_coroutine不是nullptr
            tinyrpc::Coroutine::Resume(first_coroutine);
            first_coroutine = NULL;
        }

        // 如果是SubReactor, 就执行如下内容: 
        if (m_reactor_type != MainReactor) {
            FdEvent* ptr = NULL;
            while (1) {
                ptr = CoroutineTaskQueue::GetCoroutineTaskQueue()->pop();
                if (ptr) {
                    ptr->setReactor(this);
                    tinyrpc::Coroutine::Resume(ptr->getCoroutine());
                } else {
                    break;
                }
            }
        }
        // 预处理结束

        // 执行队列中所有任务
        // 首先获取任务队列tmp_tasks
        Mutex::Lock lock(m_mutex);
        std::vector<std::function<void()>> tmp_tasks;
        tmp_tasks.swap(m_pending_tasks);  // 把m_pending_tasks中内容移到tmp_tasks中, 并清空m_pending_tasks
        lock.unlock();

        // 逐个执行tmp_tasks中的任务
        for (size_t i = 0; i < tmp_tasks.size(); ++i) {
            std::cout << "调试日志: [在函数Reactor::loop中] 开始执行m_pending_tasks中的任务[" << i <<"]" << std::endl;
            if (tmp_tasks[i]) {
                tmp_tasks[i]();
            }
            std::cout << "调试日志: [在函数Reactor::loop中] m_pending_tasks中的任务[" << i <<"]执行完毕" << std::endl;
        }

        // 开始等待注册到epoll_wait的文件描述符的EPOLLIN/EPOLLOUT事件, epoll_wait返回后
        // 将这些事件对应的协程回到函数
        std::cout << "调试日志: [在函数Reactor::loop中] 开始执行epoll_wait函数" << std::endl;
        int rt = epoll_wait(m_epfd, re_events, MAX_EVENTS, t_max_epoll_timeout);
        std::cout << "调试日志: [在函数Reactor::loop中] epoll_wait函数执行完毕, 返回" << std::endl;

        if (rt < 0) {
            std::cout << "错误日志: [在函数Reactor::loop中] epoll_wait函数出现错误" << std::endl;
        } else {
            std::cout << "调试日志: [在函数Reactor::loop中] epoll_wait函数执行返回值rt = " << rt << std::endl;
            for (int i = 0; i < rt; ++i) {
                epoll_event one_event = re_events[i];
                if (one_event.data.fd == m_wake_fd && (one_event.events & READ)) {
                    // 如果是m_wake_fd上出现了读事件, 就读取被写入m_wake_fd的8个字符, 如果读取出错就返回
                    std::cout << "错误日志: [在函数Reactor::loop中] epoll wakeup, fd = " << m_wake_fd << std::endl;
                    char buf[8];
                    while (1) {
                        if ((g_sys_read_fun(m_wake_fd, buf, 8) == -1) && errno == EAGAIN) {
                            break;
                        }
                    }
                } else {  // 如果不是m_wake_fd上的事件, 该如何处理: 
                    tinyrpc::FdEvent* ptr = (tinyrpc::FdEvent*)one_event.data.ptr;
                    // 错误检查
                    if (ptr != nullptr) {
                        int fd = ptr->getFd();
                        // 错误检查
                        if ((!(one_event.events & EPOLLIN)) && (!(one_event.events & EPOLLOUT))) {
                            std::cout << "错误日志: [在函数Reactor::loop中] socket[" << fd << "]出现了未知的事件[" 
                                << one_event.events << "], 需要注销该socket" << std::endl;
                            delEventInLoopThread(fd);
                        } else {  // 这里正式开始处理非m_wake_fd上的事件
                            // 下面的if-else给出了两种处理方式: 
                            // - if分支: ptr中协程非空的, 直接Resume协程
                            // - else分支: 否则, 才去看ptr中的m_read_callback、m_write_callback
                            if (ptr->getCoroutine()) {
                                // epoll_wait函数返回后的第一个协程不会被加入全局的
                                // 协程任务队列(CoroutineTaskQueue), 而是当前线程
                                // 直接切换到此协程, 因为每次操作协程任务队列(CoroutineTaskQueue)都要加互斥锁
                                if (!first_coroutine) {  // 如果first_coroutine为NULL
                                    first_coroutine = ptr->getCoroutine();
                                    continue;
                                }
                                if (m_reactor_type == SubReactor) {  // 如果是子reactor
                                    delEventInLoopThread(fd);
                                    ptr->setReactor(NULL);
                                    CoroutineTaskQueue::GetCoroutineTaskQueue()->push(ptr);
                                } else {  // 如果是主reactor, 只需切换到ptr->getCoroutine()协程就可
                                    tinyrpc::Coroutine::Resume(ptr->getCoroutine());
                                    if (first_coroutine) {
                                        first_coroutine = NULL;
                                    }
                                }
                            } else {
                                std::function<void()> read_cb;  // 回调函数之用于读者
                                std::function<void()> write_cb; // 回调函数之用于写者
                                read_cb = ptr->getCallBack(READ);
                                write_cb = ptr->getCallBack(WRITE);
                                // 如果是timer事件, 直接执行
                                if (fd == m_timer_fd) {
                                    read_cb();
                                    continue;
                                }
                                if (one_event.events & EPOLLIN) {
                                    std::cout << "调试信息: [在函数Reactor::loop中] socket[" << fd << "]上出现了读事件" << std::endl;
                                    Mutex::Lock lock(m_mutex);
                                    m_pending_tasks.push_back(read_cb);
                                }
                                if (one_event.events & EPOLLOUT) {
                                    std::cout << "调试信息: [在函数Reactor::loop中] socket[" << fd << "]上出现了写事件" << std::endl;
                                    Mutex::Lock lock(m_mutex);
                                    m_pending_tasks.push_back(write_cb);
                                }
                            }
                        }
                    }
                }
            }

            // 添加新的需要epoll_wait监测的文件描述符, 以及
            // 删除不再需要epoll_wait监测的文件描述符
            std::map<int, epoll_event> tmp_add;
            std::vector<int> tmp_del;
            {
                Mutex::Lock lock(m_mutex);
                tmp_add.swap(m_pending_add_fds);
                m_pending_add_fds.clear();

                tmp_del.swap(m_pending_del_fds);
                m_pending_del_fds.clear();
            }
            for (auto i = tmp_add.begin(); i != tmp_add.end(); ++i) {
                std::cout << "调试信息: [在函数Reactor::loop中] fd[" << (*i).first << "]需要加入loop" << std::endl;
                addEventInLoopThread((*i).first, (*i).second);
            }
            for (auto i = tmp_del.begin(); i != tmp_del.end(); ++i) {
                std::cout << "调试信息: [在函数Reactor::loop中] fd[" << (*i) << "]需要从loop中删除" << std::endl;
                delEventInLoopThread((*i));
            }
        }
    }
    std::cout << "调试信息: [在函数Reactor::loop中] reactor的loop结束了" << std::endl;
    m_is_looping = false;
}

void Reactor::stop() {
    if (!m_stop_flag && m_is_looping) {
        m_stop_flag = true;
        wakeup();
    }
}

void Reactor::addTask(std::function<void()> task, bool is_wakeup) {
    {
        Mutex::Lock lock(m_mutex);
        m_pending_tasks.push_back(task);
    }
    if (is_wakeup) {
        wakeup();
    }
}

void Reactor::addCoroutine(tinyrpc::Coroutine::ptr cor, bool is_wakeup) {
    auto func = [cor](){
        tinyrpc::Coroutine::Resume(cor.get());
    };
    addTask(func, is_wakeup);
}

Timer* Reactor::getTimer() {
    if (!m_timer) {
        m_timer = new Timer(this);
        m_timer_fd = m_timer->getFd();
    }
    return m_timer;
}

pid_t Reactor::getTid() {
    return m_tid;
}

void Reactor::setReactorType(ReactorType type) {
    m_reactor_type = type;
}

CoroutineTaskQueue* CoroutineTaskQueue::GetCoroutineTaskQueue() {
    // 若当前线程有「协程任务队列」, 则直接返回
    if (t_coroutine_task_queue) {
        return t_coroutine_task_queue;
    }
    // 若没有, 则创建后返回
    t_coroutine_task_queue = new CoroutineTaskQueue();
    return t_coroutine_task_queue;
}

// CoroutineTaskQueue在整个程序中只有一个实例: t_coroutine_task_queue
// 因此该实例是所有IO线程共用的, 因此push、pop时都需要加锁
void CoroutineTaskQueue::push(FdEvent* cor) {
    Mutex::Lock lock(m_mutex);
    m_task.push(cor);
    lock.unlock();
}

// CoroutineTaskQueue在整个程序中只有一个实例: t_coroutine_task_queue
// 因此该实例是所有IO线程共用的, 因此push、pop时都需要加锁
FdEvent* CoroutineTaskQueue::pop() {
    FdEvent* re = nullptr;  // re, 即返回值之意
    Mutex::Lock lock(m_mutex);
    if (m_task.size() >= 1) {
        re = m_task.front();
        m_task.pop();
    }
    lock.unlock();

    return re;
}

}