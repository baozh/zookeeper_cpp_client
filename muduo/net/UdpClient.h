#ifndef MUDUO_NET_UDPCLIENT_H
#define MUDUO_NET_UDPCLIENT_H
//#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <iostream>
#include <vector>
//#include <stdlib.h>
//#include <stdio.h>
//#include <assert.h>
#include <string.h>

#include <muduo/base/Mutex.h>
#include <muduo/net/InetAddress.h>

#define MAX_SERVER_PORT 20

namespace muduo
{
namespace net
{

class UdpClient 
{
public:
  typedef std::vector<InetAddress> NetAddrList;
  UdpClient();
  // run in main thread
  void resetAddrList(const NetAddrList &addrList);
  // run in child thread
  void resetThreadData(const NetAddrList &addrList);
  void send(const char *buf, int size);
  void setSndBufSize(int size);
  //
private:
  struct ThreadData
  {
    NetAddrList addrList;
	int sockfd;
	int curAddrIndex;
  };
  // thread specific data and method
  static void createThreadData();
  static void releaseThreadData(void *data);
  static pthread_key_t threadKey_;
  static pthread_once_t threadOnce_;
  //
  ThreadData *getThreadData();
  int createSocket();
  //
  NetAddrList addrList_;
  muduo::MutexLock mutex_;
  //
  volatile int sndBufSize_;
};

}
}
#endif