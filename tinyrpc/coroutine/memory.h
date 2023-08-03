#ifndef TINYRPC_COROUTINE_MEMORY_H
#define TINYRPC_COROUTINE_MEMORY_H

#include <memory>
#include <atomic>
#include <vector>
#include "tinyrpc/net/mutex.h"

namespace tinyrpc {

// 一个划分成m_block_count个内存块的连续内存空间
class Memory {
public: 
    typedef std::shared_ptr<Memory> ptr;

public: 
    Memory(int block_size, int block_count);
    ~Memory();

public:  // get/set函数 
    int getRefCount();
    char* getStart();
    char* getEnd();

public:
    char* getBlock();        // 取用内存块
    void backBlock(char* s); // 归还内存块
    bool hasBlock(char* s);

private:
    int m_block_size {0};  // 每个内存块的大小
    int m_block_count {0}; // 内存块数量
    int m_size {0};        // 内存空间总大小(每个内存块的大小*内存块数量)
    char* m_start {NULL};  // 内存空间起始地址
    char* m_end {NULL};    // 内存空间结束地址
    std::atomic<int> m_ref_counts {0};  // 记录已被取用的内存块的数量
    std::vector<bool> m_blocks;  // 记录各内存块是否已被取用

    Mutex m_mutex;
};

}

#endif