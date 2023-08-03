#include <iostream>
#include <sys/timerfd.h>
#include <assert.h>
#include <time.h>
#include <string.h>
#include <vector>
#include <sys/time.h>
#include <functional>
#include <map>
// #include "../comm/log.h"
#include "timer.h"
#include "mutex.h"
#include "fd_event.h"
#include "../coroutine/coroutine_hook.h"

extern read_fun_ptr_t g_sys_read_fun;  // sys read func

namespace tinyrpc {

int64_t getNowMs() {
    timeval val;
    gettimeofday(&val, nullptr);
    int64_t re = val.tv_sec * 1000 + val.tv_usec / 1000;
    return re;
}

Timer::Timer(Reactor* reactor): FdEvent(reactor) {
    // 创建一个一往无前定时器
    m_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK|TFD_CLOEXEC);
    std::cout << "调试日志: [Timer构造函数] 创建了一个定时器文件描述符, m_fd = " << m_fd << std::endl;
    if (m_fd == -1) {
        std::cout << "调试日志: [Timer构造函数] timerfd_create函数执行出错" << std::endl;
    }
    // 设置定时器发生可读事件(即当定时器超时)时的回调函数, 
    // m_read_callback是基类(FdEvent类)的可读事件回调函数
    m_read_callback = std::bind(&Timer::onTimer, this);
    // 将事件注册到reactor上
    addListenEvents(READ);
}

Timer::~Timer() {
    unregisterFromReactor();
    close(m_fd);
}

void Timer::addTimerEvent(TimerEvent::ptr event, bool need_reset) {
    // 1.向m_pending_events中添加事件
    RWMutex::WriteLock lock(m_event_mutex);
    bool is_reset = false;
    if (m_pending_events.empty()) {
        is_reset = true;
    } else {
        auto it = m_pending_events.begin();
        if (event->m_arrive_time < (*it).second->m_arrive_time) {
            is_reset = true;
        }
    }
    m_pending_events.emplace(event->m_arrive_time, event);
    lock.unlock();

    // 2.通过resetArriveTime函数, 向定时器注册刚才添加的事件
    if (is_reset && need_reset) {
        std::cout << "调试日志: [在函数Timer::addTimerEvent中] 需要reset定时器" << std::endl;
        resetArriveTime();
    }
    std::cout << "调试日志: [在函数Timer::addTimerEvent中] 成功添加定时器事件" << std::endl;
}

void Timer::delTimerEvent(TimerEvent::ptr event) {
    event->m_is_canceled = true;

    // 通过二分法在m_pending_events中寻找事件
    RWMutex::WriteLock lock(m_event_mutex);
    auto begin = m_pending_events.lower_bound(event->m_arrive_time); // 返回第一个大于等于给定值的元素的迭代器
    auto end = m_pending_events.upper_bound(event->m_arrive_time); // 返回第一个小于等于给定值的元素的迭代器
    auto it = begin;
    for (it = begin; it != end; ++it) {
        if (it->second == event) {
            std::cout << "调试日志: [在函数Timer::delTimerEvent中] 已找到要删除的事件, 现在将其删除. " <<
                "src arrive time = " << event->m_arrive_time << std::endl;
            break;
        }
    }
    if (it != m_pending_events.end()) {
        m_pending_events.erase(it);
    }
    lock.unlock();
    std::cout << "调试日志: [在函数Timer::delTimerEvent中] 成功删除定时器事件, " <<
        "原定时时间 = " << event->m_arrive_time << std::endl;
}

void Timer::resetArriveTime() {
    // 1.获取m_pending_events
    RWMutex::ReadLock lock(m_event_mutex);
    std::multimap<int64_t, TimerEvent::ptr> tmp = m_pending_events;
    lock.unlock();

    // 2.错误检查
    if (tmp.size() == 0) {
        std::cout << "调试日志: [在函数Timer::resetArriveTime中] 无正在等待的定时器事件, " << 
            "size = 0, 当前函数返回" << std::endl;
        return;
    }

    // 3.向定时器注册
    // 3-1 计算要定时到何时
    int64_t now = getNowMs();
    auto it = tmp.rbegin();
    if ((*it).first < now) {  // (*it).first为所注册事件的预计发生时间, 应该比now大(晚), 今若小, 则说明已过期
        std::cout << "调试日志: [在函数Timer::resetArriveTime中] 所有定时器事件均已过期, " << 
            "当前函数返回" << std::endl;
        return;
    }
    // 从这里看出(*it).first是绝对时间, 应转换成相对时间, 因为后面用的是相对定时器
    int64_t interval = (*it).first - now;
    // 3-2 定时
    // 初始化要定时到的时间
    itimerspec new_value;
    memset(&new_value, 0, sizeof(new_value));

    timespec ts;
    memset(&ts, 0, sizeof(ts));
    ts.tv_sec = interval / 1000;
    ts.tv_nsec = (interval % 1000) * 1000000;
    new_value.it_value = ts;  // 初始化new_value.it_value
    // 设置一个相对定时器, 事件在设置后的new_value.it_value毫秒(ms)后发生
    int rt = timerfd_settime(m_fd, 0, &new_value, nullptr); // 相对定时器
    if (rt != 0) {
        std::cout << "调试日志: [在函数Timer::resetArriveTime中] timer_settime函数出现错误, " << 
            "interval = " << interval << std::endl;
    }
    std::cout << "调试日志: [在函数Timer::resetArriveTime中] 成功reset定时器, " << 
        "下次到期时间为 = " << (*it).first << std::endl;
}

void Timer::onTimer() {
    char buf[8];
    while (1) {
        if ((g_sys_read_fun(m_fd, buf, 8) == -1) && errno == EAGAIN) {
            break;
        }
    }

    // 从m_pending_events中取出未超时且未被取消的TimerEvent
    int64_t now = getNowMs();
    RWMutex::WriteLock lock(m_event_mutex);
    auto it = m_pending_events.begin();
    std::vector<TimerEvent::ptr> tmps;
    std::vector<std::pair<int64_t, std::function<void()>>> tasks;
    for (it = m_pending_events.begin(); it != m_pending_events.end(); ++it) {
        if ((*it).first <= now && !((*it).second->m_is_canceled)) {
            tmps.push_back((*it).second);
            tasks.push_back(std::make_pair((*it).second->m_arrive_time, (*it).second->m_task));
        } else {
            break;
        }
    }

    m_pending_events.erase(m_pending_events.begin(), it);
    lock.unlock();

    for (auto i = tmps.begin(); i != tmps.end(); ++i) {
        if ((*i)->m_is_repeated) {
            (*i)->resetTime();
            addTimerEvent(*i, false);
        }
    }

    resetArriveTime();

    for (auto i: tasks) {
        i.second();
    }
}

}