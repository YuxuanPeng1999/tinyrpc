#ifndef TINYRPC_NET_ABSTRACT_DATA_H
#define TINYRPC_NET_ABSTRACT_DATA_H

namespace tinyrpc {

class AbstractData {
public:
    AbstractData() = default;
    virtual ~AbstractData() {};

public:
    bool decode_succ {false};
    bool encode_succ {false};
};

}

#endif