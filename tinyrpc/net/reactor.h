#ifndef TINYRPC_NET_EVENT_LOOP_H
#define TINYRPC_NET_EVENT_LOOP_H

#include <sys/socket.h>
#include <sys/types.h>
#include <vector>
#include <atomic>
#include <map>
#include <functional>
#include <queue>
#include "tinyrpc/coroutine/coroutine.h"
#include "fd_event.h"
// #include "timer.h"
#include "mutex.h"

namespace tinyrpc {

enum ReactorType {
    MainReactor = 1,  // 主reactor, 只能通过主线程设置
    SubReactor = 2    // 子reactor, 每个I/O线程都是此类型
};

// 先声明有这两个类
class FdEvent;
class Timer;

class Reactor {

public: 
    typedef std::shared_ptr<Reactor> ptr;
    explicit Reactor();  // 禁止隐式转换
    ~Reactor();
    void addEvent(int fd, epoll_event event, bool is_wakeup = true);
    void delEvent(int fd, bool is_wakeup = true);
    void addTask(std::function<void()> task, bool is_wakeup = true);
    void addTask(std::vector<std::function<void()>> task, bool is_wakeup = true);
    void addCoroutine(tinyrpc::Coroutine::ptr cor, bool is_wakeup = true);
    void wakeup();
    void loop();
    void stop();
    Timer* getTimer();
    pid_t getTid();
    void setReactorType(ReactorType type);

public:
    static Reactor* GetReactor();

private:
    void addWakeupFd();
    bool isLoopThread() const;
    void addEventInLoopThread(int fd, epoll_event event); // 向m_epfd中添加要监听的事件
    void delEventInLoopThread(int fd);  // 从m_epfd中删除要监听的事件

private:
    int m_epfd {-1};  // epoll文件描述符, epoll_create的返回值, 对应epoll实例
    int m_wake_fd {-1};  // wakeup fd
    int m_timer_fd {-1};
    bool m_stop_flag {false};
    bool m_is_looping {false};  // 指示reactor的loop是否要停止
    pid_t m_tid {0};

    Mutex m_mutex;

    std::vector<int> m_fds;
    std::atomic<int> m_fd_size;

    // 等待处理的文件描述符(fd), 包括两种处理: 
    // 1.将其加入loop
    // 2.将其从loop删除
    std::map<int, epoll_event> m_pending_add_fds;  // 等待加入loop的文件描述符(fd)
    std::vector<int> m_pending_del_fds;  // 等待从loop中删除的文件描述符(fd)
    std::vector<std::function<void()>> m_pending_tasks;

    Timer* m_timer {nullptr};

    ReactorType m_reactor_type {SubReactor};
};

class CoroutineTaskQueue {

public:
    static CoroutineTaskQueue* GetCoroutineTaskQueue();
    void push(FdEvent* fd);
    FdEvent* pop();
    
private:
    std::queue<FdEvent*> m_task; // 似从未被用到
    Mutex m_mutex;

    std::vector<int> m_fds;
    std::atomic<int> m_fd_size;
};

}

#endif