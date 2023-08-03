#include <iostream>
#include <vector>
#include <algorithm>
#include <sstream>
#include <memory>
#include <string.h>
#include "tinyrpc/net/tinypb/tinypb_codec.h"
#include "tinyrpc/net/byte.h"
// #include "tinyrpc/comm/log.h"
#include "tinyrpc/net/abstract_data.h"
#include "tinyrpc/net/tinypb/tinypb_data.h"
#include "tinyrpc/comm/msg_req.h"

namespace tinyrpc {

static const char PB_START = 0x02;  // RPC包起始符
static const char PB_END = 0x03;    // RPC包结束符
static const int MSG_REQ_LEN = 20;  // msg_req的默认长度

TinyPbCodeC::TinyPbCodeC() {}

TinyPbCodeC::~TinyPbCodeC() {}

/*
参数说明: 
- buf: 传出参数, 保存data序列化的结果.
- data: TinyPb协议结构体, 该函数将data序列化. 
*/
void TinyPbCodeC::encode(TcpBuffer* buf, AbstractData* data) {
  // 1.错误检查
  if (!buf || !data) {
    std::cout << "错误日志: [在函数TinyPbCodeC::encode中] encode error! buf or data nullptr" << std::endl;
    return;
  }
  
  // 2.将data序列化
  // 2-1 将data转换成其子类, 命名为tmp
  TinyPbStruct* tmp = dynamic_cast<TinyPbStruct*>(data);
  // 2-2 将tmp序列化, 返回的char指针re即序列化结果, len是tmp序列化所得string的长度
  int len = 0;
  const char* re = encodePbData(tmp, len);
  if (re == nullptr || len == 0 || !tmp->encode_succ) {
    std::cout << "错误日志: [在函数TinyPbCodeC::encode中] encode error" << std::endl;
    data->encode_succ = false;
    return;
  }
  std::cout << "调试日志: [在函数TinyPbCodeC::encode中] encode package len = " << len << std::endl;
  // 3.将序列化结果写入buf
  if (buf != nullptr) {
    buf->writeToBuffer(re, len);
    std::cout << "调试日志: [在函数TinyPbCodeC::encode中] succ encode and write to buffer, writeindex=" << 
        buf->writeIndex() << std::endl;
  }
  data = tmp;
  if (re) {
    free((void*)re);
    re = NULL;
  }
  // DebugLog << "test encode end";

}

const char* TinyPbCodeC::encodePbData(TinyPbStruct* data, int& len) {
  // 错误检查
  if (data->service_full_name.empty()) {
    std::cout << "错误日志: [在函数TinyPbCodeC::encodePbData中] parse error, service_full_name is empty" << std::endl;
    data->encode_succ = false;
    return nullptr;
  }

  // 1.计算data的msg_req, msg_req_len字段
  if (data->msg_req.empty()) {
    data->msg_req = MsgReqUtil::genMsgNumber();
    data->msg_req_len = data->msg_req.length();
    std::cout << "调试日志: [在函数TinyPbCodeC::encodePbData中] generate msgno = " << data->msg_req << std::endl;
  }

  // 2.计算data的整包长度字段, 为报文开辟空间(用malloc开辟, 在堆区)
  int32_t pk_len = 2 * sizeof(char) + 6 * sizeof(int32_t)
                    + data->pb_data.length() + data->service_full_name.length()
                    + data->msg_req.length() + data->err_info.length();
  
  std::cout << "调试日志: [在函数TinyPbCodeC::encodePbData中] encode pk_len = " << pk_len << std::endl;
  char* buf = reinterpret_cast<char*>(malloc(pk_len));  // 报文空间指针(本函数返回值)
  char* tmp = buf;  // 临时指针, 因为构造报文过程中指针需要不断变化

  // 3.填充报文的开始符
  *tmp = PB_START;
  tmp++;

  // 4.填充报文的pl_len字段
  int32_t pk_len_net = htonl(pk_len);  // int32_t型须先转换为网络字节序
  memcpy(tmp, &pk_len_net, sizeof(int32_t));
  tmp += sizeof(int32_t);

  // 5.填充报文的msg_req_len、msg_req字段
  // 5-1 长度字段
  int32_t msg_req_len = data->msg_req.length();
  std::cout << "调试日志: [在函数TinyPbCodeC::encodePbData中] msg_req_len= " << msg_req_len << std::endl;
  int32_t msg_req_len_net = htonl(msg_req_len);
  memcpy(tmp, &msg_req_len_net, sizeof(int32_t));
  tmp += sizeof(int32_t);
  // 5-2 长度非0才能填充msg_req
  if (msg_req_len != 0) {
    memcpy(tmp, &(data->msg_req[0]), msg_req_len);
    tmp += msg_req_len;
  }

  // 6.填充报文的service_full_name_len、service_full_name字段
  // 6-1 service_full_name_len字段
  int32_t service_full_name_len = data->service_full_name.length();
  std::cout << "调试日志: [在函数TinyPbCodeC::encodePbData中] src service_full_name_len = " << service_full_name_len << std::endl;
  int32_t service_full_name_len_net = htonl(service_full_name_len);
  memcpy(tmp, &service_full_name_len_net, sizeof(int32_t));
  tmp += sizeof(int32_t);
  // 6-2 长度非0才能填充service_full_name
  if (service_full_name_len != 0) {
    memcpy(tmp, &(data->service_full_name[0]), service_full_name_len);
    tmp += service_full_name_len;
  }

  // 7.填充报文的err_code字段, err_info_len、err_info字段
  // 7-1 err_code字段
  int32_t err_code = data->err_code;
  std::cout << "调试日志: [在函数TinyPbCodeC::encodePbData中] err_code= " << err_code << std::endl;
  int32_t err_code_net = htonl(err_code);
  memcpy(tmp, &err_code_net, sizeof(int32_t));
  tmp += sizeof(int32_t);
  // 7-2 err_info_len字段
  int32_t err_info_len = data->err_info.length();
  std::cout << "调试日志: [在函数TinyPbCodeC::encodePbData中] err_info_len= " << err_info_len << std::endl;
  int32_t err_info_len_net = htonl(err_info_len);
  memcpy(tmp, &err_info_len_net, sizeof(int32_t));
  tmp += sizeof(int32_t);
  // 7-3 长度err_info_len非零才能填充err_info
  if (err_info_len != 0) {
    memcpy(tmp, &(data->err_info[0]), err_info_len);
    tmp += err_info_len;
  }

  // 8.TinyPbStruct中的请求/应答类对象本来就是序列化后的结果, 因此直接填充到报文里即可
  memcpy(tmp, &(data->pb_data[0]), data->pb_data.length());
  tmp += data->pb_data.length();
  std::cout << "调试日志: [在函数TinyPbCodeC::encodePbData中] pb_data_len= " << data->pb_data.length() << std::endl;

  // 9.计算校验码并填充
  int32_t checksum = 1;
  int32_t checksum_net = htonl(checksum);
  memcpy(tmp, &checksum_net, sizeof(int32_t));
  tmp += sizeof(int32_t);

  // 10.填充结束符
  *tmp = PB_END;

  // 11.顺表完善data, data进来的时候并不完整, 所有长度以及校验码都没有
  data->pk_len = pk_len;
  data->msg_req_len = msg_req_len;
  data->service_name_len = service_full_name_len;
  data->err_info_len = err_info_len;
  // checksum has not been implemented yet, directly skip chcksum
  data->check_num = checksum;
  data->encode_succ = true;

  // 12.返回值
  len = pk_len;  // 传出参数

  return buf;

}

void TinyPbCodeC::decode(TcpBuffer* buf, AbstractData* data) {
  std::cout << "调试日志: [在函数TinyPbCodeC::decode中] 本函数开始" << std::endl;
  // 1.错误检查
  if (!buf || !data) {  // 如果buf或data有一为nullptr, 则直接返回
    std::cout << "错误日志: [在函数TinyPbCodeC::decode中] decode error! buf or data nullptr" << std::endl;
    return;
  }

  // 2.报文定界: 确定报文的起始符索引, 和终止符索引, 
  // 并用parse_full_pack表示定界是否成功, 若失败, 
  // 应返回
  // 2-1 需要的变量
  std::vector<char> tmp = buf->getBufferVector();
  // int total_size = buf->readAble();
  int start_index = buf->readIndex();  // 起始符索引
  int end_index = -1;  // 终止符索引
  int32_t pk_len= -1; 
  bool parse_full_pack = false;  // 是否解析到完整的报文
  // 2-2 循环寻找正确的开始符, 找到后开始定界
  for (int i = start_index; i < buf->writeIndex(); ++i) {
    // 首先, 找到报文的开始符
    if (tmp[i] == PB_START) {
      if (i + 1 < buf->writeIndex()) {  // i+1是起始符后面的第一个字节, 
        // buf->writeIndex()=m_write_index是报文的最后一字节的后一字节, 
        // 若该条件成功, 说明报文中处理起始符还有其他内容, 即报文不是空的
        // 然后找整包长度
        pk_len = getInt32FromNetByte(&tmp[i+1]);  // 将4字节数据转换为整包长度(包括起始符和结束符)
        std::cout << "调试日志: [在函数TinyPbCodeC::decode中] prase pk_len =" << pk_len << std::endl;
        // 然后计算报文结束符的位置
        int j = i + pk_len - 1;  // 结束符位置
        std::cout << "调试日志: [在函数TinyPbCodeC::decode中] j =" << j << ", i=" << i << std::endl;

        if (j >= buf->writeIndex()) {  // 这说明报文还不完整
          // DebugLog << "recv package not complete, or pk_start find error, continue next parse";
          continue;
        }
        // 三重确认: 开头必须是PB_START, 结尾必须是PB_END, 长度必须符合整包长度的记录
        if (tmp[j] == PB_END) {  // 这里综合验证第2、3点, 第1点已在上面的「if (tmp[i] == PB_START) 」验证
          start_index = i;
          end_index = j;
          // DebugLog << "parse succ, now break";
          parse_full_pack = true;  // 这才是最终确认
          break;
        }
        
      }
      
    }
  }
  // 2-3 检查定界结果, 如果失败, 直接返回
  if (!parse_full_pack) {
    std::cout << "调试日志: [在函数TinyPbCodeC::decode中] not parse full package, return" << std::endl;
    return;
  }

  buf->recycleRead(end_index + 1 - start_index);  // 更新buf的m_read_index

  std::cout << "调试日志: [在函数TinyPbCodeC::decode中] m_read_buffer size=" << buf->getBufferVector().size() << 
    "rd=" << buf->readIndex() << "wd=" << buf->writeIndex() << std::endl;

  // TinyPbStruct pb_struct;
  TinyPbStruct* pb_struct = dynamic_cast<TinyPbStruct*>(data);
  pb_struct->pk_len = pk_len;
  pb_struct->decode_succ = false;

  int msg_req_len_index = start_index + sizeof(char) + sizeof(int32_t);
  if (msg_req_len_index >= end_index) {
    std::cout << "错误日志: [在函数TinyPbCodeC::decode中] parse error, msg_req_len_index[" << 
      msg_req_len_index << "] >= end_index[" << end_index << "]" << std::endl;
    // drop this error package
    return;
  }

  pb_struct->msg_req_len = getInt32FromNetByte(&tmp[msg_req_len_index]);
  if (pb_struct->msg_req_len == 0) {
    std::cout << "错误日志: [在函数TinyPbCodeC::decode中] prase error, msg_req emptr" << std::endl;
    return;
  }

  std::cout << "调试日志: [在函数TinyPbCodeC::decode中] msg_req_len= " << pb_struct->msg_req_len << std::endl;
  int msg_req_index = msg_req_len_index + sizeof(int32_t);
  std::cout << "调试日志: [在函数TinyPbCodeC::decode中] msg_req_len_index= " << msg_req_index << std::endl;

  char msg_req[50] = {0};

  memcpy(&msg_req[0], &tmp[msg_req_index], pb_struct->msg_req_len);
  pb_struct->msg_req = std::string(msg_req);
  std::cout << "调试日志: [在函数TinyPbCodeC::decode中] msg_req= " << pb_struct->msg_req << std::endl;
  
  int service_name_len_index = msg_req_index + pb_struct->msg_req_len;
  if (service_name_len_index >= end_index) {
    std::cout << "错误日志: [在函数TinyPbCodeC::decode中] parse error, service_name_len_index[" << 
      service_name_len_index << "] >= end_index[" << end_index << "]" << std::endl;
    // drop this error package
    return;
  }

  std::cout << "调试日志: [在函数TinyPbCodeC::decode中] service_name_len_index = " << 
    service_name_len_index << std::endl;
  int service_name_index = service_name_len_index + sizeof(int32_t);

  if (service_name_index >= end_index) {
    std::cout << "错误日志: [在函数TinyPbCodeC::decode中] parse error, service_name_index[" << 
      service_name_index << "] >= end_index[" << end_index << "]" << std::endl;
    return;
  }

  pb_struct->service_name_len = getInt32FromNetByte(&tmp[service_name_len_index]);

  if (pb_struct->service_name_len > pk_len) {
    std::cout << "错误日志: [在函数TinyPbCodeC::decode中] parse error, service_name_len[" << 
      pb_struct->service_name_len << "] >= pk_len [" << pk_len << "]" << std::endl;
    return;
  }
  std::cout << "调试日志: [在函数TinyPbCodeC::decode中] service_name_len = " << 
    pb_struct->service_name_len << std::endl;

  char service_name[512] = {0};

  memcpy(&service_name[0], &tmp[service_name_index], pb_struct->service_name_len);
  pb_struct->service_full_name = std::string(service_name);
  std::cout << "调试日志: [在函数TinyPbCodeC::decode中] service_name = " << 
    pb_struct->service_full_name << std::endl;

  int err_code_index = service_name_index + pb_struct->service_name_len;
  pb_struct->err_code = getInt32FromNetByte(&tmp[err_code_index]);

  int err_info_len_index = err_code_index + sizeof(int32_t);

  if (err_info_len_index >= end_index) {
    std::cout << "错误日志: [在函数TinyPbCodeC::decode中] parse error, err_info_len_index[" << 
      err_info_len_index << "] >= end_index[" << end_index << "]" << std::endl;
    // drop this error package
    return;
  }
  pb_struct->err_info_len = getInt32FromNetByte(&tmp[err_info_len_index]);
  std::cout << "调试日志: [在函数TinyPbCodeC::decode中] err_info_len = " << 
    pb_struct->err_info_len << std::endl;
  int err_info_index = err_info_len_index + sizeof(int32_t);

  char err_info[512] = {0};

  memcpy(&err_info[0], &tmp[err_info_index], pb_struct->err_info_len);
  pb_struct->err_info = std::string(err_info); 

  int pb_data_len = pb_struct->pk_len 
                      - pb_struct->service_name_len - pb_struct->msg_req_len - pb_struct->err_info_len
                      - 2 * sizeof(char) - 6 * sizeof(int32_t);

  int pb_data_index = err_info_index + pb_struct->err_info_len;
  std::cout << "调试日志: [在函数TinyPbCodeC::decode中] pb_data_len= " << pb_data_len << 
    ", pb_index = " << pb_data_index << std::endl;

  if (pb_data_index >= end_index) {
    std::cout << "错误日志: [在函数TinyPbCodeC::decode中] parse error, pb_data_index[" << 
      pb_data_index << "] >= end_index[" << end_index << "]" << std::endl;
    return;
  }
  // DebugLog << "pb_data_index = " << pb_data_index << ", pb_data.length = " << pb_data_len;

  std::string pb_data_str(&tmp[pb_data_index], pb_data_len);
  pb_struct->pb_data = pb_data_str;

  // DebugLog << "decode succ,  pk_len = " << pk_len << ", service_name = " << pb_struct->service_full_name; 

  pb_struct->decode_succ = true;
  data = pb_struct;

  std::cout << "调试日志: [在函数TinyPbCodeC::decode中] 本函数结束" << std::endl;
}


ProtocalType TinyPbCodeC::getProtocalType() {
  return TinyPb_Protocal;
}

}