#ifndef TINYRPC_NET_TCP_TCPCONNECTIONTIMEWHEEL_H
#define TINYRPC_NET_TCP_TCPCONNECTIONTIMEWHEEL_H

#include <queue>
#include <vector>
#include "tinyrpc/net/tcp/abstract_slot.h"
#include "tinyrpc/net/reactor.h"
#include "tinyrpc/net/timer.h"

namespace tinyrpc {

class TcpConnection;

class TcpTimeWheel {
public: 
    typedef std::shared_ptr<TcpTimeWheel> ptr;
    typedef AbstractSlot<TcpConnection> TcpConnectionSlot;

public: 
    TcpTimeWheel(Reactor* reactor, int bucket_count, int invetal = 10);
    ~TcpTimeWheel();

public: 
    void fresh(TcpConnectionSlot::ptr slot);
    void loopFunc();

public:
    Reactor* m_reactor {nullptr};
    int m_bucket_count {0}; // 槽数
    int m_interval {0};     // 时间轮的更新周期, 单位: 秒

    TimerEvent::ptr m_event;
    std::queue<std::vector<TcpConnectionSlot::ptr>> m_wheel;  // 时间轮队列
};

}

#endif