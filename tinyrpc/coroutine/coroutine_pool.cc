#include <vector>
#include <sys/mman.h>
#include "tinyrpc/comm/config.h"
// #include "tinyrpc/comm/log.h"
#include "tinyrpc/coroutine/coroutine_pool.h"
#include "tinyrpc/coroutine/coroutine.h"
#include "tinyrpc/net/mutex.h"

namespace tinyrpc {

extern tinyrpc::Config::ptr gRpcConfig;

static CoroutinePool* t_coroutine_container_ptr = nullptr;

CoroutinePool* GetCoroutinePool() {
    if (!t_coroutine_container_ptr) {  // 如果t_coroutine_container_ptr为空
        t_coroutine_container_ptr = new CoroutinePool(gRpcConfig->m_cor_pool_size, 
            gRpcConfig->m_cor_stack_size);
    }
    return t_coroutine_container_ptr;
}

CoroutinePool::CoroutinePool(int pool_size, int stack_size): m_pool_size(pool_size), m_stack_size(stack_size)
{
    // 1.首先设置主协程
    Coroutine::GetCurrentCoroutine();

    // 2.创建pool_size个协程, 存入m_free_cors
    // 2-1 开辟pool_size个协程需要的内存空间, 每个协程拥有的内存空间大小为stack_size
    m_memory_pool.push_back(std::make_shared<Memory>(stack_size, pool_size));
    Memory::ptr tmp = m_memory_pool[0];
    // 2-2 初始化pool_size个协程
    for (int i = 0; i < pool_size; ++i) {
        Coroutine::ptr cor = std::make_shared<Coroutine>(stack_size, tmp->getBlock());
        cor->setIndex(i);
        m_free_cors.push_back(std::make_pair(cor, false));
    }
}

CoroutinePool::~CoroutinePool() {}

/* 本函数从0开始, 寻找满足如下条件的free coroutine: 
 * 1. it.second = false, 也就是当前协程可以被调度
 * 2. getIsInCoFunc()返回false, 也就是当前协程的回调函数未在运行
 * 尽最大可能: 重用已经被使用过的协程, 而非选择未被使用过的协程
 * 因为如果协程已经被使用过, 那么其物理内存中就已被写入内容, 
 * 但是如果协程未被使用过, 那么它就尚未被分配物理内存. 我们只通过调用mmap来获取虚拟地址, 但是当下不会写入内容.
 * 因此, Linux只会在我们真正开始写入内容时才分配物理内存, 这会导致缺页中断. */
/* 总结: 
 * 1.该协程池管理新协程的方法:
 *   (1) 有已创建的协程可用, 则用之.
 *   (2) 无, 但有之前创建的内存空间可用, 则以之创建新协程并返回.
 *   (3) 若既无已创建协程, 又无可用内存空间, 则先申请内存空间, 再创建新协程, 并返回. 
 * 2.内存管理方式: 当需要创建协程, 但无可用内存空间时, 并非只为一块协程申请内存空间, 而是多申请几块, 
 *   这次用一块, 再留着剩下的备用. 这样, 可以保证各协程的内存空间有一定连续性, 不至于太分散, 以至于
 *   总是产生缺页中断, 降低程序效率. */
Coroutine::ptr CoroutinePool::getCoroutineInstance() {
    // 1.在已创建的协程中, 寻找可返回的free coroutine
    Mutex::Lock lock(m_mutex);
    for (int i = 0; i < m_pool_size; ++i) {
        if (!m_free_cors[i].first->getIsInCoFunc() && !m_free_cors[i].second) {
            m_free_cors[i].second = true;
            Coroutine::ptr cor = m_free_cors[i].first;
            lock.unlock();  // 从这里可以看出, 封装后互斥锁的使用更便捷了, 就算没有进入这个条件, 互斥锁也会在lock析构时被释放
            return cor;
        }
    }
    // 2.如果已经创建的协程都不符合条件, 但是m_memory_pool中的Memory对象还管理有未使用的内存块
    // 则使用这些未使用的内存块创建新协程, 并返回
    for (size_t i = 1; i < m_memory_pool.size(); ++i) {
        char* tmp = m_memory_pool[i]->getBlock();
        if (tmp) {  // 如果返回值tmp不是NULL(即m_memory_pool[i]管理的内存中还有可供取用的)
            Coroutine::ptr cor = std::make_shared<Coroutine>(m_stack_size, tmp);
            return cor;
        }
    }
    // 3.如果已经创建的协程都不符合条件, 并且m_memory_pool中的Memory对象还管理的内存块也都使用完了
    // 那就先创建新的Memory对象, 再创建协程, 并返回
    m_memory_pool.push_back(std::make_shared<Memory>(m_stack_size, m_pool_size));
    return std::make_shared<Coroutine>(m_stack_size, m_memory_pool[m_memory_pool.size() - 1]->getBlock());
}

void CoroutinePool::returnCoroutine(Coroutine::ptr cor) {
    int i = cor->getIndex();
    // 1.如果是在构造函数中已经创建的协程, 那就把其在m_free_cors中的记录
    // 改为false, 以待下次取用该协程, 而不去归还掉该协程占用的内存空间
    if (i >= 0 && i < m_pool_size) {
        m_free_cors[i].second = false;
    } else {  // 2.如果该协程不是在构造函数中创建的, 直接还掉内存, 该协程随后就随着TcpConnection的析构而被析构了
        for (size_t i = 1; i < m_memory_pool.size(); ++i) {
            if (m_memory_pool[i]->hasBlock(cor->getStackPtr())) {
                m_memory_pool[i]->backBlock(cor->getStackPtr());
            }
        }
    }
}

}