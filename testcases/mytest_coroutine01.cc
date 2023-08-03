#include <iostream>
#include "tinyrpc/coroutine/coroutine.h"

// cor2的回调函数. 根据Coroutine类的定义, 协程回调函数必须为void()式, 也就是没有输入和返回
void fun2() {
    std::cout << "2----this is coroutine 2" << std::endl;
    tinyrpc::Coroutine::Yield();
    std::cout << "4----yield back, it is cor 2" << std::endl;
}

int main() {
    // cor1: 当前main函数
    // cor2: 执行fun2函数
    // 为协程开辟栈空间
    int stack_size = 128 * 1024;  // 参照工程示例代码test_coroutine.cc设置
    char* sp2 = reinterpret_cast<char*>(malloc(stack_size));
    tinyrpc::Coroutine::ptr cor2 = std::make_shared<tinyrpc::Coroutine>(stack_size, sp2, fun2);

    // 开始测试
    std::cout << "1----this is main cor" << std::endl;
    tinyrpc::Coroutine::Resume(cor2.get());
    std::cout << "3----resume back, this is main cor" << std::endl;
    tinyrpc::Coroutine::Resume(cor2.get());
    std::cout << "5----resume back, this is main cor" << std::endl;

    return 0;
}