#include <iostream>
#include <memory>
#include <sys/mman.h>
#include <assert.h>
#include <stdlib.h>
// #include "tinyrpc/comm/log.h"
#include "tinyrpc/coroutine/memory.h"

namespace tinyrpc {

Memory::Memory(int block_size, int block_count): m_block_size(block_size), m_block_count(block_count) {
    // 1.计算总空间, 并开辟空间
    m_size = m_block_count * m_block_size;
    m_start = (char*)malloc(m_size);
    assert(m_start != (void*)(-1));  // (void*)(-1)就是0xFFFFFFFF(32位机), 是虚拟内存的最高地址
    std::cout << "信息日志: succ mmap " << m_size << " bytes memory";
    m_end = m_start + m_size - 1;
    // 2.初始化m_blocks, 其记录各内存块是否已被取用
    m_blocks.resize(m_block_count);
    for (size_t i = 0; i < m_blocks.size(); ++i) {
        m_blocks[i] = false;
    }
    m_ref_counts = 0;
}

Memory::~Memory() {
    // 1.释放空间
    if (!m_start || m_start == (void*)(-1)) {
        return;
    }
    free(m_start);
    m_start = NULL;
    std::cout << "信息日志: succ free munmap " << m_size << " bytes memory";
    // 2.处理其他属性
    m_ref_counts = 0;
}

char* Memory::getStart() {
    return m_start;
}

char* Memory::getEnd() {
    return m_end;
}

int Memory::getRefCount() {
    return m_ref_counts;
}

char* Memory::getBlock() {
    int t = -1;
    // 1.查找一块未被取用的内存块
    Mutex::Lock lock(m_mutex);
    for (size_t i = 0; i < m_blocks.size(); ++i) {
        if (m_blocks[i] == false) {
            m_blocks[i] = true;
            t = i;
            break;
        }
    }
    lock.unlock();
    // 2.返回值
    // 2-1 如果该对象管理的内存块已被用完, 则返回NULL
    if (t == -1) {
        return NULL;
    }
    // 2-2 否则, 返回内存块首地址, 并更新m_ref_counts
    m_ref_counts++;
    return m_start + (t * m_block_size);
}

void Memory::backBlock(char* s) {
    // 错误检查
    if (s > m_end || s < m_start) {
        std::cout << "错误日志: 这块内存不属于这个Memory对象" << std::endl;
        return;
    }
    // 将起始于s的内存块恢复为未被取用, 更新相关属性
    int i = (s - m_start) / m_block_size;
    Mutex::Lock lock(m_mutex);
    m_blocks[i] = false;
    lock.unlock();
    m_ref_counts--;
}

// 询问s这个地址是否属于这个Memory管理的内存空间
bool Memory::hasBlock(char* s) {
    return ((s >= m_start) && (s <= m_end));
}

}