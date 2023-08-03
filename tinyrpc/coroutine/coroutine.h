#ifndef TINYRPC_COROUTINE_COROUTINE_H
#define TINYRPC_COROUTINE_COROUTINE_H

#include <memory>
#include <functional>
#include "tinyrpc/coroutine/coctx.h"
#include "tinyrpc/comm/run_time.h"

namespace tinyrpc {

int getCoroutineIndex();
RunTime* getCurrentRunTime();
void setCurrentRunTime(RunTime* v);

class Coroutine {
public:
    typedef std::shared_ptr<Coroutine> ptr;

private:
    Coroutine();  // 只用于构造主协程

public:
    Coroutine(int size, char* stack_ptr);  // 用于构造用户协程
    Coroutine(int size, char* stack_ptr, std::function<void()> cb);  // 用于构造用户协程
    ~Coroutine();
    bool setCallBack(std::function<void()> cb);  // 设置用户协程的回调函数(cb, callback)

public: // get/set函数
    int getCorId() const { return m_cor_id; }

    void setIsInCoFunc(const bool v) { m_is_in_cofunc = v; }
    bool getIsInCoFunc() const { return m_is_in_cofunc; }

    void setMsgNo(const std::string& msg_no) { m_msg_no = msg_no; }
    std::string getMsgNo() { return m_msg_no; }

    RunTime* getRunTime() { return &m_run_time; }

    void setIndex(int index) { m_index = index; }
    int getIndex() { return m_index; }

    char* getStackPtr() { return m_stack_sp; }

    int getStackSize() { return m_stack_size; }
    
    void setCanResume(bool v) { m_can_resume = v; }

public:
    static void Yield();
    static void Resume(Coroutine* cor);
    static Coroutine* GetCurrentCoroutine();
    static Coroutine* GetMainCoroutine();
    static bool IsMainCoroutine();

private:
    int m_cor_id {0};           // 协程id
    coctx m_coctx;              // 协程寄存器(协程上下文)
    int m_stack_size {0};       // 协程的栈空间大小, 单位: 字节(Byte)
    char* m_stack_sp {NULL};    // 协程的栈空间指针, 可通过malloc或mmap开辟空间, 初始化该值
    bool m_is_in_cofunc {false};// 指示当前协程的回调函数是否正在运行
    std::string m_msg_no;
    RunTime m_run_time;
    bool m_can_resume {true};   // 是否可以切换到当前协程
    int m_index {-1};           // 本协程在协程池中的编号

public: 
    public:
    std::function<void()> m_call_back;  // 协程的回调函数
};

}

#endif