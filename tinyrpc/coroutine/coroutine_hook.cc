#include <iostream>
#include <assert.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "tinyrpc/coroutine/coroutine_hook.h"
#include "tinyrpc/coroutine/coroutine.h"
#include "tinyrpc/net/fd_event.h"
#include "tinyrpc/net/reactor.h"
// #include "tinyrpc/comm/log.h"
#include "tinyrpc/net/timer.h"
#include "tinyrpc/comm/config.h"

#define HOOK_SYS_FUNC(name) name##_fun_ptr_t g_sys_##name##_fun = (name##_fun_ptr_t)dlsym(RTLD_NEXT, #name);

HOOK_SYS_FUNC(accept)
HOOK_SYS_FUNC(read)
HOOK_SYS_FUNC(write)
HOOK_SYS_FUNC(connect)
HOOK_SYS_FUNC(sleep)

namespace tinyrpc {

extern tinyrpc::Config::ptr gRpcConfig;

static bool g_hook = true;

void SetHook(bool value) {
    g_hook = value;
}

void toEpoll(tinyrpc::FdEvent::ptr fd_event, int events) {
    tinyrpc::Coroutine* cur_cor = tinyrpc::Coroutine::GetCurrentCoroutine();
    if (events & tinyrpc::IOEvent::READ) {
        std::cout << "fd:[" << fd_event->getFd() << "], 向epoll注册READ事件(即EPOLLIN事件)" << std::endl;
        fd_event->setCoroutine(cur_cor);
        fd_event->addListenEvents(tinyrpc::IOEvent::READ);  // 向reactor注册一个任务
    }
    if (events & tinyrpc::IOEvent::WRITE) {
        std::cout << "fd:[" << fd_event->getFd() << "], 向epoll注册WRITE事件(即EPOLLOUT事件)" << std::endl;
        fd_event->setCoroutine(cur_cor);
        fd_event->addListenEvents(tinyrpc::IOEvent::WRITE);
    }
}

int accept_hook(int sockfd, struct sockaddr* addr, socklen_t* addrlen) {
    // 1.检查: 如果是主协程, 直接调用g_sys_accept_fun
    std::cout << "调试日志: [在函数read_hook中] accept_hook开始" << std::endl;
    if (tinyrpc::Coroutine::IsMainCoroutine()) {
        std::cout << "调试日志: [在函数accept_hook中] 当前协程为主协程, 主协程无法hook, 直接调用g_sys_accept_fun" << std::endl;
        return g_sys_accept_fun(sockfd, addr, addrlen);
    }
    // 2.获取当前线程对应的Reactor, 并指定给fd对应的FdEvent对象
    tinyrpc::Reactor::GetReactor();  // 如果当前线程没有Reactor, 则先创建一个reactor
    tinyrpc::FdEvent::ptr fd_event = tinyrpc::FdEventContainer::GetFdContainer()->getFdEvent(sockfd);
    if (fd_event->getReactor() == nullptr) {
        fd_event->setReactor(tinyrpc::Reactor::GetReactor());
    }
    // 3.开始接受
    fd_event->setNonBlock();  // sockfd被设为非阻塞
    // 3-1 如果能接受到, 就直接接受, 否则返回
    int n = g_sys_accept_fun(sockfd, addr, addrlen);
    if (n > 0) {
        return n;
    }
    // 3-2
    toEpoll(fd_event, tinyrpc::IOEvent::READ);
    std::cout << "调试日志: [在函数read_hook中] accept_hook所在协程被挂起" << std::endl;
    tinyrpc::Coroutine::Yield();
    // 3-3
    fd_event->delListenEvents(tinyrpc::IOEvent::READ);
    fd_event->clearCoroutine();
    std::cout << "调试日志: [在函数accept_hook中] accept_hook所在协程恢复, 现在调用g_sys_accept_fun" << std::endl;
    return g_sys_accept_fun(sockfd, addr, addrlen);
}

ssize_t read_hook(int fd, void* buf, size_t count) {
    std::cout << "调试日志: [在函数read_hook中] read_hook开始" << std::endl;
    // 1.检查: 如果是主协程, 直接调用g_sys_read_fun
    if (tinyrpc::Coroutine::IsMainCoroutine()) {
        std::cout << "调试日志: [在函数read_hook中] 当前协程为主协程, 主协程无法hook, 直接调用系统调用read" << std::endl;
        return g_sys_read_fun(fd, buf, count);
    }
    // 2.获取当前线程对应的Reactor, 并指定给fd对应的FdEvent对象
    tinyrpc::Reactor::GetReactor();  // 如果当前线程没有Reactor, 则先创建一个reactor
    tinyrpc::FdEvent::ptr fd_event = tinyrpc::FdEventContainer::GetFdContainer()->getFdEvent(fd);
    if (fd_event->getReactor() == nullptr) {
        fd_event->setReactor(tinyrpc::Reactor::GetReactor());
    }
    // 3.开始读取
    fd_event->setNonBlock();
    // 3-1 如果能读到, 就直接读, 读完返回
    ssize_t n = g_sys_read_fun(fd, buf, count);
    if (n > 0) {
        return n;
    }
    // 3-2 如果读不到, 就把监听m_fd的上的读事件的任务交给Reactor, 然后先挂起当前协程
    toEpoll(fd_event, tinyrpc::IOEvent::READ);
    std::cout << "调试日志: [在函数read_hook中] read_hook所在协程被挂起" << std::endl;
    tinyrpc::Coroutine::Yield();
    // 3-3 等到m_fd上被检测到了IOEvent::READ事件(即EPOLLIN事件)后, 再读取
    fd_event->delListenEvents(tinyrpc::IOEvent::READ);
    fd_event->clearCoroutine();
    std::cout << "调试日志: [在函数read_hook中] read_hook所在协程恢复, 现在调用g_sys_read_fun" << std::endl;
    return g_sys_read_fun(fd, buf, count);
}

ssize_t write_hook(int fd, const void *buf, size_t count) {
    std::cout << "调试日志: [在函数write_hook中] write_hook开始" << std::endl;
    // 1.
    if (tinyrpc::Coroutine::IsMainCoroutine()) {
        std::cout << "调试日志: [在函数write_hook中] 无法hook, 直接调用系统调用write" << std::endl;
        return g_sys_write_fun(fd, buf, count);
    }
    // 2.
    tinyrpc::Reactor::GetReactor();
    tinyrpc::FdEvent::ptr fd_event = tinyrpc::FdEventContainer::GetFdContainer()->getFdEvent(fd);
    if (fd_event->getReactor() == nullptr) {
        fd_event->setReactor(tinyrpc::Reactor::GetReactor());
    }
    // 3.
    fd_event->setNonBlock();
    // 3-1
    ssize_t n = g_sys_write_fun(fd, buf, count);
    if (n > 0) {
        return n;
    }
    // 3-2 
    toEpoll(fd_event, tinyrpc::IOEvent::WRITE);
    std::cout << "调试日志: [在函数write_hook中] read_hook所在协程被挂起" << std::endl;
    tinyrpc::Coroutine::Yield();
    // 3-3
    fd_event->delListenEvents(tinyrpc::IOEvent::WRITE);
    fd_event->clearCoroutine();
    std::cout << "调试日志: [在函数read_hook中] write_hook所在协程恢复, 现在调用g_sys_write_fun" << std::endl;
    return g_sys_write_fun(fd, buf, count);
}

// 这个函数比read_hook, write_hook多了一个定时器超时事件, 因此比较长
// read_hook/write_hook是什么时候监听到读/写事件什么时候再读/写就行了, 不存在超时问题
// 但是connect_hook存在, 如果半天连接不上, 就超时, 不连了, 因此多了个定时器超时事件
int connect_hook(int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
    std::cout << "调试日志: [在函数connect_hook中] connect_hook开始" << std::endl;
    // 1.检查: 如果是主协程, 直接调用g_sys_connect_fun
    if (tinyrpc::Coroutine::IsMainCoroutine()) {
        std::cout << "调试日志: [在函数connect_hook中] 无法hook, 直接调用g_sys_connect_fun" << std::endl;
        return g_sys_connect_fun(sockfd, addr, addrlen);
    }
    // 2.获取当前线程对应的Reactor, 并指定给fd对应的FdEvent对象
    tinyrpc::Reactor* reactor = tinyrpc::Reactor::GetReactor();
    tinyrpc::FdEvent::ptr fd_event = tinyrpc::FdEventContainer::GetFdContainer()->getFdEvent(sockfd);
    if (fd_event->getReactor() == nullptr) {
        fd_event->setReactor(reactor);
    }
    // 3.开始连接
    tinyrpc::Coroutine* cur_cor = tinyrpc::Coroutine::GetCurrentCoroutine();
    fd_event->setNonBlock();
    // 3-1 首先尝试直接开始连接
    int n = g_sys_connect_fun(sockfd, addr, addrlen);
    if (n == 0) {  // 如果直接连接成功, 则返回
        std::cout << "调试日志: [在函数connect_hook中] 直接连接成功, 返回" << std::endl;
        return n;
    } else if (errno != EINPROGRESS) {  // 如果连接失败, 但错误不是EINPROGRESS, 则直接返回
        std::cout << "调试日志: [在函数connect_hook中] 连接过程出现错误, 且错误非 EINPROGRESS, errno" <<
            errno << ", error = " << strerror(errno) << std::endl;
        return n;
    }
    std::cout << "调试日志: errno = EINPROGRESS";
    // 3-2 如果直接连接失败, 且错误是EINPROGRESS, 则做两件事: 
    // (1) 把监听sockfd上的写读事件的任务交给Reactor
    // (2) 向Reactor的m_timer(定时器)注册一件事: 定时gRpcConfig->m_max_connect_timeout时间, 
    //     超时后将is_timeout置为true
    // 然后挂起(Yield)当前协程. 注意, 这样, Yield返回的原因就有两种了: 
    // (1) Reactor监听到sockfd上的读写事件了
    // (2) 定时器超时了
    // 3-2-1 把监听sockfd的上写读事件的任务交给Reactor
    toEpoll(fd_event, tinyrpc::IOEvent::WRITE);  // 等待sockfd上的写事件
    // 3-2-2 向定时器注册超时事件
    bool is_timeout = false;  // 指示是否超时(, )
    // 超时函数句柄: 设置超时标志, 然后唤醒协程
    auto timeout_cb = [&is_timeout, cur_cor](){
        is_timeout = true;
        tinyrpc::Coroutine::Resume(cur_cor);
    };
    // 创建TimerEvent(定时器事件)
    tinyrpc::TimerEvent::ptr event = std::make_shared<tinyrpc::TimerEvent>(
        gRpcConfig->m_max_connect_timeout, false, timeout_cb);
    // 获取Reactor的定时器, 并将上面创建的定时器事件注册到Reactor的定时器上
    tinyrpc::Timer* timer = reactor->getTimer();
    timer->addTimerEvent(event);
    // 3-2-3 挂起当前协程
    tinyrpc::Coroutine::Yield();
    // 3-3 等到检测到sockfd上的写事件, Yield就会返回, 此时再行连接
    // 3-3-1 // write事件需要删除, 因为连接成功后, 后面会重新监听该fd的写事件
    fd_event->delListenEvents(tinyrpc::IOEvent::WRITE);
    fd_event->clearCoroutine();
    // 3-3-2 定时器也需要删除(这个定时器本就是临时的, 用于检验执行hook_connect有没有超时)
    timer->delTimerEvent(event);
    // 3-3-3 执行连接
    n = g_sys_connect_fun(sockfd, addr, addrlen);
    // 如果连接成功
    if ((n < 0 && errno == EISCONN) || n == 0) {
        std::cout << "调试日志: [在函数hook_connect中] 连接成功" << std::endl;
        return 0;
    }
    // 如果连接失败, 则打印错误信息
    if (is_timeout) {  // 如果是超时导致的连接失败, 则将errno置为ETIMEOUT
        std::cout << "调试日志: [在函数hook_connect中] 连接出现错误, 超时 " << 
            gRpcConfig->m_max_connect_timeout << " ms" << std::endl;
        errno = ETIMEDOUT;
    }
    std::cout << "调试日志: [在函数hook_connect中] 连接出现错误, errno = " << errno <<
        ", error = " << strerror(errno) << std::endl;
    return -1;
}

unsigned int sleep_hook(unsigned int seconds) {
    std::cout << "调试日志: [在函数sleep_hook中] sleep_hook开始" << std::endl;
    // 1.检查: 如果是主协程, 直接调用g_sys_sleep_fun
    if (tinyrpc::Coroutine::IsMainCoroutine()) {
        std::cout << "调试日志: [在函数connect_hook中] 无法hook, 直接调用g_sys_sleep_fun" << std::endl;
        return g_sys_sleep_fun(seconds);
    }
    // 2.创建一个定时器, 并向其注册超时事件
    tinyrpc::Coroutine* cur_cor = tinyrpc::Coroutine::GetCurrentCoroutine();
    bool is_timeout = false;  // 创建一个超时指示
    // 超时事件的回调函数
    auto timeout_cb = [cur_cor, &is_timeout]() {
        std::cout << "调试日志: [在函数sleep_hook中] 时间到, 恢复睡眠的协程" << std::endl;
        tinyrpc::Coroutine::Resume(cur_cor);
    };
    // 创建超时事件
    tinyrpc::TimerEvent::ptr event = std::make_shared<tinyrpc::TimerEvent>(1000 * seconds, false, timeout_cb);
    tinyrpc::Reactor::GetReactor()->getTimer()->addTimerEvent(event);  // 向Reactor的定时器注册超时事件
    std::cout << "调试日志: [在函数sleep_hook中] 现在挂起当前函数所在协程" << std::endl;
    // 为什么这里要循环执行Yield呢? 因为read或者write可能导致该协程恢复, 
    // 因此如果协程因为该原因恢复, 必须让其再次被挂起. 因此必须循环执行Yield, 
    // 只要is_timeout还是true, 就一直挂起协程.
    while (!is_timeout) {
        tinyrpc::Coroutine::Yield();
    }
    return 0;
}

}

extern "C" {


int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
	if (!tinyrpc::g_hook) {
		return g_sys_accept_fun(sockfd, addr, addrlen);
	} else {
		return tinyrpc::accept_hook(sockfd, addr, addrlen);
	}
}

ssize_t read(int fd, void *buf, size_t count) {
	if (!tinyrpc::g_hook) {
		return g_sys_read_fun(fd, buf, count);
	} else {
		return tinyrpc::read_hook(fd, buf, count);
	}
}

ssize_t write(int fd, const void *buf, size_t count) {
	if (!tinyrpc::g_hook) {
		return g_sys_write_fun(fd, buf, count);
	} else {
		return tinyrpc::write_hook(fd, buf, count);
	}
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
	if (!tinyrpc::g_hook) {
		return g_sys_connect_fun(sockfd, addr, addrlen);
	} else {
		return tinyrpc::connect_hook(sockfd, addr, addrlen);
	}
}

unsigned int sleep(unsigned int seconds) {
	if (!tinyrpc::g_hook) {
		return g_sys_sleep_fun(seconds);
	} else {
		return tinyrpc::sleep_hook(seconds);
	}
}

}