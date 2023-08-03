#include <time.h>
#include <sys/time.h>
#include <sstream>
#include <sys/syscall.h>
#include <unistd.h>
#include <iostream>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <algorithm>
#include <semaphore.h>
#include <errno.h>

namespace tinyrpc {

static thread_local pid_t t_thread_id = 0;

pid_t gettid() {
    if (t_thread_id == 0) {
        t_thread_id = syscall(SYS_gettid);
    }
}

void Exit(int code) {
    printf("抱歉, TinyRPC服务器出现错误, 请查看错误日志, 以获得更多信息!\n");
    _exit(code);
}

}