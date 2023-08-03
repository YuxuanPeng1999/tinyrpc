#ifndef TINYRPC_COMM_LOG_H
#define TINYRPC_COMM_LOG_H

#include <sstream>
#include <sstream>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <unistd.h>
#include <memory>
#include <vector>
#include <queue>
#include <semaphore.h>
// #include "tinyrpc/net/mutex.h"
// #include "tinyrpc/comm/config.h"

namespace tinyrpc {

pid_t gettid();

void Exit(int code);

}

#endif