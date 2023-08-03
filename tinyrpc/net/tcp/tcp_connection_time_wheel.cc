#include <iostream>
#include <queue>
#include <vector>
#include "tinyrpc/net/tcp/abstract_slot.h"
// #include "tinyrpc/net/tcp/tcp_connection.h"
#include "tinyrpc/net/tcp/tcp_connection_time_wheel.h"
#include "tinyrpc/net/timer.h"

namespace tinyrpc {

TcpTimeWheel::TcpTimeWheel(Reactor* reactor, int bucket_count, int interval)
    : m_reactor(reactor), m_bucket_count(bucket_count), m_interval(interval) {
    // 1.建立空槽(初始化m_wheel)
    for (int i = 0; i < bucket_count; ++i) {
        std::vector<TcpConnectionSlot::ptr> tmp;
        m_wheel.push(tmp);
    }
    // 2.定时器设置
    // 2-1 创建TimerEvent, 包含定时器的回调函数和定到的时间
    m_event = std::make_shared<TimerEvent>(m_interval * 1000, true, std::bind(&TcpTimeWheel::loopFunc, this));
    // 2-2 注册到reactor的定时器上
    m_reactor->getTimer()->addTimerEvent(m_event);
}

TcpTimeWheel::~TcpTimeWheel() {
    m_reactor->getTimer()->delTimerEvent(m_event);
}

void TcpTimeWheel::loopFunc() {
    // 弹出(丢弃)队首的bucket
    m_wheel.pop();
    std::cout << "调试日志: [在函数TcpTimeWheel::loopFunc中] pop src bucket" << std::endl;
    // 在队尾添加新的bucket
    std::vector<TcpConnectionSlot::ptr> tmp;
    m_wheel.push(tmp);
    std::cout << "调试日志: [在函数TcpTimeWheel::loopFunc中] push new bucket" << std::endl;
}

void TcpTimeWheel::fresh(TcpConnectionSlot::ptr slot) {
    std::cout << "调试日志: [在函数TcpTimeWheel::fresh中] 更新Tcp连接对象的位置" << std::endl;
    m_wheel.back().emplace_back(slot);
}

}