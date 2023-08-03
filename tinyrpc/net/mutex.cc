#include <iostream>
#include <pthread.h>
#include <memory>
#include "tinyrpc/net/mutex.h"
#include "tinyrpc/net/reactor.h"
#include "tinyrpc/coroutine/coroutine.h"
#include "tinyrpc/coroutine/coroutine_hook.h"

namespace tinyrpc {

CoroutineMutex::CoroutineMutex() {}

CoroutineMutex::~CoroutineMutex() {
    if (m_lock) {
        unlock();
    }
}

void CoroutineMutex::lock() {
    // 1.错误检查
    if (Coroutine::IsMainCoroutine()) {
        std::cout << "错误日志: [在函数CoroutineMutex::lock中] 主协程不能使用协程锁" << std::endl;
        return;
    }
    
    // 2.上锁
    // 2-1 获取当前协程备用
    Coroutine* cor = Coroutine::GetCurrentCoroutine();
    // 2-2 根据m_lock判断当前协程是否能拿到锁
    Mutex::Lock lock(m_mutex); // 构造一个局部互斥锁, 保护共享资源m_lock
    // 2-3 尝试拿锁
    if (!m_lock) {  // 2-3-1 如果m_lock = false, 即当前锁未被其他协程拿走, 则本协程成功拿到锁
        m_lock = true;
        std::cout << "调试日志: [在函数CoroutineMutex::lock中] 当前协程成功获取协程锁" << std::endl;
        lock.unlock();
    } else {  // 2-3-2 否则, 说明当前锁已被其他协程拿走, 则当前协程进入睡眠队列
        m_sleep_cors.push(cor);  // 当前协程进入睡眠队列
        auto tmp = m_sleep_cors;
        lock.unlock();
        std::cout << "调试日志: [在函数CoroutineMutex::lock中] 当前协程挂起, 等待协程锁, 当前睡眠队列上共有 " << 
            tmp.size() << " 个协程" << std::endl;
        Coroutine::Yield();  // 当前协程挂起
    }
}

void CoroutineMutex::unlock() {
    // 1.错误检查
    if (Coroutine::IsMainCoroutine()) {
        std::cout << "错误日志: [在函数CoroutineMutex::lock中] 主协程不能使用协程锁" << std::endl;
        return;
    }

    // 2.上锁
    // 2-1 构建一个局部互斥锁, 保护共享资源m_lock
    Mutex::Lock lock(m_mutex);
    // 2-2 尝试改为未占用状态
    if (m_lock) {  // 如果现在拥有锁
        // 2-2-1 那就改为未占用状态
        m_lock = false;
        // 2-2-2 检查睡眠队列, 如果有睡眠等待锁的协程, 就唤醒一个
        if (m_sleep_cors.empty()) {  // 如果睡眠队列为空, 直接返回
            return;
        }
        // 如果睡眠队列非空, 唤醒
        Coroutine* cor = m_sleep_cors.front();  // 首先是让一个协程弹出睡眠队列
        m_sleep_cors.pop();
        lock.unlock();
        // 然后是将「唤醒协程」任务提交
        if (cor) {  // if (cor != nullptr), 则需要唤醒一个睡眠协程
            // 打印日志: 唤醒某个协程
            // 把当前协程的Resume任务封装成一个Lambda函数, 放到协程调度器的任务队列上
            tinyrpc::Reactor::GetReactor()->addTask(
                [cor]() {
                    tinyrpc::Coroutine::Resume(cor);
                }
            , true);
        }
    }
}

}