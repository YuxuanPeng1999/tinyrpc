#include <iostream>
#include <memory>
#include <map>
#include <time.h>
#include <stdlib.h>
#include <semaphore.h>
#include "tinyrpc/net/reactor.h"
#include "tinyrpc/net/tcp/io_thread.h"
#include "tinyrpc/net/tcp/tcp_connection.h"
#include "tinyrpc/net/tcp/tcp_server.h"
#include "tinyrpc/net/tcp/tcp_connection_time_wheel.h"
#include "tinyrpc/coroutine/coroutine.h"
#include "tinyrpc/coroutine/coroutine_pool.h"
#include "tinyrpc/comm/config.h"
#include "tinyrpc/comm/log.h"

namespace tinyrpc {

extern tinyrpc::Config::ptr gRpcConfig;

static thread_local Reactor* t_reactor_ptr = nullptr;
static thread_local IOThread* t_cur_io_thread = nullptr;

// 构造函数: 调用本类的main函数, 创建当前线程的SubReactor, 并使其开始loop
IOThread::IOThread() {
    int rt = sem_init(&m_init_semaphore, 0, 0);
    assert(rt == 0);

    // 把m_start_semaphore的初始值设置为0, 当第一次执行sem_wait时就会被阻塞, 
    // 直到另一个sem_post被执行
    rt = sem_init(&m_start_semaphore, 0, 0);
    assert(rt == 0);

    pthread_create(&m_thread, nullptr, &IOThread::main, this);

    std::cout << "调试日志: [在IOThread构造函数中] 这是主线程, 信号量对m_init_semaphore执行sem_wait函数, " << 
        "直到子线程m_thread回到函数IOThread::main()执行到对m_start_semaphore执行sem_wait函数, 完成初始化操作" << std::endl;
    // 执行sem_wait, 由于m_init_semaphore初值为0, 则当前线程一下子就被阻塞了, 
    // 而m_thread线程的main函数执行, 直到main函数到达sem_post(m_init_semaphore)
    rt = sem_wait(&m_init_semaphore);
    assert(rt == 0);
    std::cout << "调试日志: [在IOThread构造函数中] 信号量执行wait函数完毕, 完成I/O线程的创建" << std::endl;

    sem_destroy(&m_init_semaphore);
}

IOThread::~IOThread() {
    m_reactor->stop();
    pthread_join(m_thread, nullptr);

    if (m_reactor != nullptr) {
        delete m_reactor;
        m_reactor = nullptr;
    }
}

IOThread* IOThread::GetCurrentIOThread() {
    return t_cur_io_thread;
}

sem_t* IOThread::getStartSemaphore() {
    return &m_start_semaphore;
}

Reactor* IOThread::getReactor() {
    return m_reactor;
}

pthread_t IOThread::getPthreadId() {
    return m_thread;
}

void IOThread::setThreadIndex(const int index) {
    m_index = index;
}

int IOThread::getThreadIndex() {
    return m_index;
}

// IOThread的回调函数, 主协程
void* IOThread::main(void* arg) {
    // 1.初始化一些属性: 最重要的: 设置自己的reactor
    // 1-1 为当前线程创建Reactor, 并将其类型设置为SubReactor
    t_reactor_ptr = new Reactor();
    assert(t_reactor_ptr != NULL);
    // 1-2 设置线程成员t_cur_io_thread
    IOThread* thread = static_cast<IOThread*>(arg);
    t_cur_io_thread = thread;
    // 1-3 将t_reactor_ptr设为当前线程的m_reactor
    thread->m_reactor = t_reactor_ptr;
    thread->m_reactor->setReactorType(SubReactor);
    // 1-4 设置自己的tid
    thread->m_tid = gettid();

    // 2.为本线程创建主协程
    // 本函数: 创建主协程(如果没有), 并返回当前协程
    Coroutine::GetCurrentCoroutine();

    std::cout << "调试日志: [在函数IOThread::main中] IOThread类的初始化工作完成, 现在令信号量执行post操作" << std::endl;
    sem_post(&thread->m_init_semaphore);

    // 上来先执行一个sem_wait, 由于m_start_semaphore的初始值为0, 因此
    // 执行sem_wait后当前线程直接被阻塞, 而在本类成员函数start中, 是执行
    // 了sem_post, 信号量值加一, 本函数也就继续执行了
    sem_wait(&thread->m_start_semaphore);
    sem_destroy(&thread->m_start_semaphore);

    // 令本线程的reactor开始loop
    std::cout << "调试日志: [在函数IOThread::main中] IO线程 " << thread->m_tid << "开始loop" << std::endl;
    t_reactor_ptr->loop();

    return nullptr;
}

void IOThread::addClient(TcpConnection* tcp_conn) {
    // 待补充
    std::cout << "IOThread::addClient被调用, 但是尚未定义, 程序退出" << std::endl;
    exit(0);
}

// 初始化线程池m_io_threads
IOThreadPool::IOThreadPool(int size): m_size(size) {
    m_io_threads.resize(size);
    for (int i = 0; i < size; ++i) {
        m_io_threads[i] = std::make_shared<IOThread>();
        m_io_threads[i]->setThreadIndex(i);
    }
}

// 启动m_io_threads中的所有线程
void IOThreadPool::start() {
    for (int i = 0; i < m_size; ++i) {
        int rt = sem_post(m_io_threads[i]->getStartSemaphore());
        assert(rt == 0);
    }
}

IOThread* IOThreadPool::getIOThread() {
    // 如果已经将m_io_threads中所有线程都给出(m_index = m_size), 
    // 或者还没有给出过任何线程(m_index = -1), 
    // 则返回m_io_threads中的第一个线程
    if (m_index == m_size || m_index == -1) {
        m_index = 0;
    }
    return m_io_threads[m_index++].get();
}

int IOThreadPool::getIOThreadPoolSize() {
    return m_size;
}

}