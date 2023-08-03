#ifndef TINYRPC_NET_TIMER_H
#define TINYRPC_NET_TIMER_H

#include <iostream>
#include <time.h>
#include <memory>
#include <map>
#include <functional>
#include "tinyrpc/net/mutex.h"
#include "tinyrpc/net/reactor.h"
#include "tinyrpc/net/fd_event.h"
// #include "tinyrpc/comm/log.h"

namespace tinyrpc {

// typedef long long int64_t
int64_t getNowMs();

class TimerEvent {
public:
    typedef std::shared_ptr<TimerEvent> ptr;

public: 
    TimerEvent(int64_t interval, bool is_repeated, std::function<void()> task) 
        : m_interval(interval), m_is_repeated(is_repeated), m_task(task) {
        m_arrive_time = getNowMs() + m_interval;
        std::cout << "调试日志: [在TimerEvent构造函数中] 定时事件将于" << m_arrive_time << "执行" << std::endl;
    }

    void resetTime() {
        std::cout << "调试日志: [在函数TimerEvent::resetTime中] 重置定时事件, 原执行时间: " << "m_arrive_time" << std::endl;
        m_arrive_time = getNowMs() + m_interval;
        std::cout << "调试日志: [在函数TimerEvent::resetTime中] 重置定时事件, 现执行时间: " << "m_arrive_time" << std::endl;
        m_is_canceled = false;
    }

    void wake() {
        m_is_canceled = false;
    }

    void cancel() {
        m_is_canceled = true;
    }

    void cancelRepeated() {
        m_is_repeated = false;
    }

public:
    int64_t m_arrive_time;  // 指定何时执行任务(绝对时间), 单位: ms
    int64_t m_interval;     // 两任务之间的间隔, 单位: ms
    bool m_is_repeated {false};
    bool m_is_canceled {false};
    std::function<void()> m_task;
};

class Reactor;
class FdEvent;

class Timer: public tinyrpc::FdEvent {
public: 
    typedef std::shared_ptr<Timer> ptr;

public:
    Timer(Reactor* reactor);
    ~Timer();

public: 
    void addTimerEvent(TimerEvent::ptr event, bool need_reset = true);
    void delTimerEvent(TimerEvent::ptr event);
    void resetArriveTime();
    void onTimer();

private:
    // 定时时间(ms), 事件
    std::multimap<int64_t, TimerEvent::ptr> m_pending_events;
    RWMutex m_event_mutex;
};

}

#endif