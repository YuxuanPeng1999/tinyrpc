#ifndef TINYRPC_COMM_CONFIG_H
#define TINYRPC_COMM_CONFIG_H

#include <tinyxml/tinyxml.h>
#include <string>
#include <memory>
#include <map>

namespace tinyrpc {

enum LogLevel {
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    NONE = 5
};

class Config {
public:
    typedef std::shared_ptr<Config> ptr;

public:
    Config(const char* file_path);
    ~Config();

public:
    void readConf();
    void readDBConfig(TiXmlElement* node);
    void readLogConfig(TiXmlElement* node);
    TiXmlElement* getXmlNode(const std::string& name);

public:
    std::string m_log_path;
    std::string m_log_prefix;
    int m_log_max_size {0};
    LogLevel m_log_level {LogLevel::DEBUG};
    int m_log_sync_inteval {500};

public:  // 协程参数
    int m_cor_stack_size {0};
    int m_cor_pool_size {0};
    int m_msg_req_len {0};
    int m_max_connect_timeout {0};
    int m_iothread_num {0};
    int m_timewheel_bucket_num {0};
    int m_timewheel_inteval {0};

private:
    std::string m_file_path;
    TiXmlDocument* m_xml_file;
};

}

#endif