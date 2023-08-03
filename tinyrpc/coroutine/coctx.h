#ifndef TINYRPC_COROUTINE_COCTX_H
#define TINYRPC_COROUTINE_COCTX_H 

namespace tinyrpc{

enum {
  kRBP = 6,   // rbp, 协程栈底(高地址)
  kRDI = 7,   // rdi, 协程回调函数的第一个参数
  kRSI = 8,   // rsi, 协程回调函数的第二个参数
  kRETAddr = 9,   // 协程回调函数将被执行的下一条指令的地址, 将被放入rip
  kRSP = 13,   // rsp, 协程栈顶(低地址)
};

// coroutine context (协程上下文)
struct coctx {
  void* regs[14];
};

extern "C" {
// 将寄存器的当前状态保存到第一个coctx中, 然后将寄存器状态改为coctx中的内容
extern void coctx_swap(coctx *, coctx *) asm("coctx_swap");

};

}

#endif
