#include <iostream>
#include <unistd.h>
#include <string.h>
#include "tinyrpc/net/tcp/tcp_buffer.h"
// #include "tinyrpc/comm/log.h"

namespace tinyrpc {

TcpBuffer::TcpBuffer(int size) {
    m_buffer.resize(size);
}

TcpBuffer::~TcpBuffer() {}

// m_buffer中, 还有多少个字符可读
int TcpBuffer::readAble() {
    return m_write_index - m_read_index;
}

// m_buffer中, 还有多少空位可写
int TcpBuffer::writeAble() {
    return m_buffer.size() - m_write_index;
}

int TcpBuffer::readIndex() const {
  return m_read_index;
}

int TcpBuffer::writeIndex() const {
  return m_write_index;
}

void TcpBuffer::resizeBuffer(int size) {
    // 1.创建一个size大小的数组
    std::vector<char> tmp(size);  // 新数组的大小为指定的size
    int c = std::min(size, readAble());
    memcpy(&tmp[0], &m_buffer[m_read_index], c);
    // 2.令m_buffer和tmp交换, 之后m_buffer即保存tmp的内容
    m_buffer.swap(tmp);
    // 3.更新相关属性
    m_read_index = 0;
    m_write_index = m_read_index + c;
}

void TcpBuffer::writeToBuffer(const char* buf, int size) {
    // 1.如果m_buffer中, 可写的空位数小于size, 先扩充m_buffer的大小
    if (size > writeAble()) {
        int new_size = (int)(1.5 * (m_write_index + size));
        resizeBuffer(new_size);
    }
    // 2.向m_buffer写入内容
    memcpy(&m_buffer[m_write_index], buf, size);
    // 3.更新m_write_index
    m_write_index += size;
}

void TcpBuffer::readFromBuffer(std::vector<char>& re, int size) {
    // 1.确定实际可读的字符数
    if (readAble() == 0) {
        std::cout << "调试日志: [TcpBuffer::readFromBuffer] 当前buffer中没有可读内容!" << std::endl;
        return;
    }
    int read_size = readAble() > size ? size : readAble();  // std::min(size, readAble())
    // 2.从m_buffer中读取read_size个字符
    std::vector<char> tmp(read_size);
    memcpy(&tmp[0], &m_buffer[m_read_index], read_size);
    re.swap(tmp);
    m_read_index += read_size;  // 更新已经读过的字符数
    // 3.如果已经读过的字符数超过m_buffer的尺寸的三分之一, 则将其从m_buffer中去除
    adjustBuffer();
}

void TcpBuffer::adjustBuffer() {
    // 如果m_buffer中已经被读取的字符超过m_buffer尺寸的三分之一, 则将其从m_buffer中去除
    if (m_read_index > static_cast<int>(m_buffer.size() / 3)) {
        // 1.将还没读过的字符移动到new_buffer中
        std::vector<char> new_buffer(m_buffer.size());  // 开辟新的buffer空间
        int count = readAble();  // 计算还没有被读取过的字符数
        memcpy(&new_buffer[0], &m_buffer[m_read_index], count);  // 将m_buffer中还没有读过的字符移动到new_buffer中
        // 2.new_buffer与m_buffer交换
        m_buffer.swap(new_buffer);
        // 3.更新相关属性
        m_write_index = count;
        m_read_index = 0;
        new_buffer.clear();
    }
}

int TcpBuffer::getSize() {
    return m_buffer.size();
}

void TcpBuffer::clearBuffer() {
    m_buffer.clear();
    m_read_index = 0;
    m_write_index = 0;
}

// 更新m_read_index.
void TcpBuffer::recycleRead(int index) {
    int j = m_read_index + index;
    if (j > (int)m_buffer.size()) {
        std::cout << "错误日志: [在函数TcpBuffer::recycleRead中] 当前函数出现错误" << 
            "要读的字符数超过m_buffer的尺寸" << std::endl;
        return;
    }
    m_read_index = j;
    adjustBuffer();
}

// 更新writeIndex.
// 本函数与read/read_hook配合使用: 
// 当使用read/read_hook向m_buffer写入字符之后, 
// 读取的字符数index被返回, 因此将m_write_index更新.
void TcpBuffer::recycleWrite(int index) {
    int j = m_write_index + index;
    if (j > (int)m_buffer.size()) {
        std::cout << "错误日志: [在函数TcpBuffer::recycleWrite中] 当前函数出现错误" << 
            "要写入的字符超过m_buffer的尺寸" << std::endl;
        return;
    }
    m_write_index = j;
    adjustBuffer();
}

// 返回m_buffer中所有的未读字符
std::string TcpBuffer::getBufferString() {
    std::string re(readAble(), '0');
    memcpy(&re[0], &m_buffer[m_read_index], readAble());
    return re;
}

std::vector<char> TcpBuffer::getBufferVector() {
    return m_buffer;
}

}