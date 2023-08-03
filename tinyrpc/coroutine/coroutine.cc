#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <atomic>
#include "tinyrpc/coroutine/coroutine.h"
// #include "tinyrpc/comm/log.h"
#include "tinyrpc/comm/run_time.h"

namespace tinyrpc {

// 主协程(对于当前线程下的所有协程, 只有一份)
static thread_local Coroutine* t_main_coroutine = NULL;

// 当前线程中正在运行的协程
static thread_local Coroutine* t_cur_coroutine = NULL;

static thread_local RunTime* t_cur_run_time = NULL;
RunTime* getCurrentRunTime() { return t_cur_run_time; }
void setCurrentRunTime(RunTime* v) { t_cur_run_time = v; }

// 当前线程拥有的协程数量
static std::atomic_int t_coroutine_count {0};

// 协程id计数器
static std::atomic_int t_cur_coroutine_id {1};
int getCoroutineIndex() { return t_cur_coroutine_id; }

/* 这个函数封装了协程的回调函数, 封装的原因是执行回调函数前后, 
还要控制m_is_in_cofunc的值, 以及执行Yield函数. */
void CoFunction(Coroutine* co) {
    // 既然回调函数要被执行, 首先就需要把m_is_in_cofunc置为true
    if (co != nullptr) {
        co->setIsInCoFunc(true);
    }
    // 然后再执行回调函数
    co->m_call_back();
    // 执行完之后, 还要把m_is_in_cofunc改回来
    co->setIsInCoFunc(false);
    // 最后, 目标协程(本协程执行完了, 就被挂起, 切换回主协程)
    Coroutine::Yield();
}

Coroutine::Coroutine() {
    m_cor_id = 0;  // 主协程的id为0
    t_coroutine_count++;  // 协程计数+1
    // 将本协程的上下文(m_coctx)的各字节初始化为0
    // 协程上下文本质上是用于保存协程上次被挂起之前的程序运行现场,
    // 现在本协程刚创建, 何来「上次」? 故将m_coctx初始化为全0
    memset(&m_coctx, 0, sizeof(m_coctx));
    t_cur_coroutine = this;  // 记录本线程的当前协程
}

Coroutine::Coroutine(int size, char* stack_ptr): m_stack_size(size), m_stack_sp(stack_ptr) {
    // 验证stack_sp是否为NULL
    assert(m_stack_sp);  // assert(断言): 括号中条件为真则继续执行, 否则报错且程序结束

    if (!t_main_coroutine) {  // 如果还没有构造主协程, 则构造之
        t_main_coroutine = new Coroutine();
    }

    m_cor_id = t_cur_coroutine_id++;
    t_coroutine_count++;
}

Coroutine::Coroutine(int size, char* stack_ptr, std::function<void()> cb): 
m_stack_size(size), m_stack_sp(stack_ptr) {
    // 验证stack_sp是否为NULL
    assert(m_stack_sp);  // assert(断言): 括号中条件为真则继续执行, 否则报错且程序结束

    if (!t_main_coroutine) {  // 如果还没有构造主协程, 则构造之
        t_main_coroutine = new Coroutine();
    }

    setCallBack(cb);
    m_cor_id = t_cur_coroutine_id++;
    t_coroutine_count++;
}

bool Coroutine::setCallBack(std::function<void()> cb) {
    // 不能为主协程设置回调函数
    if (this == t_main_coroutine) {
        // 打印报错日志, 以后再补
        return false;
    }

    // 为其他协程设置回调函数
    // 1.设置回调函数
    if (m_is_in_cofunc) {  // 如果协程的回调函数正在执行, 则不能为其设置回调函数
        // 打印报错日志, 以后再补
        return false;
    }
    m_call_back = cb;
    // 2.初始化协程上下文
    // 2-1 计算栈顶(SP)位置()
    char* top = m_stack_sp + m_stack_size;
    top = reinterpret_cast<char*>((reinterpret_cast<unsigned long>(top)) & -16LL);  // 字节对齐(-16LL: long long int类型, 64位, 值为-16)
    // 2-2 初始化协程上下文
    memset(&m_coctx, 0, sizeof(m_coctx));  // 先将所有上下文全都置为0
    m_coctx.regs[kRSP] = top;  // 初始化栈顶(SP)
    m_coctx.regs[kRBP] = top;  // 初始化栈底(BP)
    // 初始化下一条指令的地址, 即回调函数的入口地址
    m_coctx.regs[kRETAddr] = reinterpret_cast<char*>(CoFunction);
    // 初始化协程回调函数(CoFunction)的第一个参数, 即当前协程的指针
    m_coctx.regs[kRDI] = reinterpret_cast<char*>(this);
    // 3.将本协程置为可以切换到本协程
    m_can_resume = true;

    return true;
}

Coroutine::~Coroutine() {
    t_coroutine_count--;
}

Coroutine* Coroutine::GetCurrentCoroutine() {
    if (t_cur_coroutine == nullptr) {
        t_main_coroutine = new Coroutine();
        t_cur_coroutine = t_main_coroutine;
    }
    return t_cur_coroutine;
}

Coroutine* Coroutine::GetMainCoroutine() {
    if (t_main_coroutine) {
        return t_main_coroutine;
    }
    t_main_coroutine = new Coroutine();
    return t_main_coroutine;
}
// 判断当前协程是否是主协程
bool Coroutine::IsMainCoroutine() {
    if (t_main_coroutine == nullptr || t_cur_coroutine == t_main_coroutine) {
        return true;
    }
    return false;
}


// 目标协程->主协程
void Coroutine::Yield() {
    // 1.先检查
    // 1-1 先检查主协程是否存在
    if (t_main_coroutine == nullptr) {
        // 打印报错信息, 后面再弄
        return;
    }
    // 1-2 先检查目前是不是在目标协程中, 如果不在也没必要执行
    if (t_cur_coroutine == t_main_coroutine) {
        // 打印报错信息, 后面再弄
        return;
    }
    // 2.开始操作
    Coroutine* co = t_cur_coroutine;
    t_cur_coroutine = t_main_coroutine;  // 更新t_cur_coroutine记录
    t_cur_run_time = NULL;
    coctx_swap(&(co->m_coctx), &(t_main_coroutine->m_coctx));  // 从前到后
}

// 从主协程->目标协程
void Coroutine::Resume(Coroutine* co) {
    // 1.先检查
    // 1-1 检查该线程中, 是否是主协程正在运行
    if (t_cur_coroutine != t_main_coroutine) {
        // 打印报错日志, 后面再弄
        return;
    }
    // 1-2 检查主协程是否为NULL
    if (!t_main_coroutine) {
        // 打印报错日志, 后面再弄
        return;
    }
    // 1-3 如果目标协程为NULL, 或者目标协程不能被切换到, 那也不能切换
    if (!co || !co->m_can_resume) {
        // 打印报错日志, 后面再弄
        return;
    }
    // 1-4 如果当前就是目标协程正在运行, 那也不能切换到目标协程
    if (t_cur_coroutine == co) {
        // 打印报错日志, 后面再弄
        return;
    }
    // 2.进行切换
    // 2-1 先修改相关记录
    t_cur_coroutine = co;
    t_cur_run_time = co->getRunTime();
    coctx_swap(&(t_main_coroutine->m_coctx), &(co->m_coctx));  // 从前到后
}

}