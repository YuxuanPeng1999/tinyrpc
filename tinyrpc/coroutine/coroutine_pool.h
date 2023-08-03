#ifndef TINYRPC_COROUTINE_COUROUTINE_POOL_H
#define TINYRPC_COROUTINE_COUROUTINE_POOL_H

#include <vector>
#include "tinyrpc/coroutine/coroutine.h"
#include "tinyrpc/net/mutex.h"
#include "tinyrpc/coroutine/memory.h"

namespace tinyrpc {

class CoroutinePool {
public:
    CoroutinePool(int pool_size, int stack_size = 1024*128);
    ~CoroutinePool();

public:
    Coroutine::ptr getCoroutineInstance();    // 取用协程
    void returnCoroutine(Coroutine::ptr cor); // 归还协程

private:
    int m_stack_size {0};  // 每个协程拥有的栈空间大小
    // m_free_cors中是本类中保存的所有协程, 
    // pair.first: 协程指针
    // pair.second: 协程是否可被调度
    //     false: 能被调度
    //     true:  不能被调度
    std::vector<std::pair<Coroutine::ptr, bool>> m_free_cors;

    int m_pool_size {0};   // m_memory_pool中内存块用完时, 每次申请的内存块数
    // 本类管理的协程使用的内存空间, 有已经分配给m_free_cors中的协程的, 也有还处于闲置状态的
    std::vector<Memory::ptr> m_memory_pool;

    Mutex m_mutex;
};

CoroutinePool* GetCoroutinePool();

}

#endif