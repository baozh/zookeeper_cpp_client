// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_CONNECTOR_ORI_H
#define MUDUO_NET_CONNECTOR_ORI_H

#include "muduo/net/InetAddress.h"

#include <boost/enable_shared_from_this.hpp>
#include <boost/function.hpp>
#include <boost/noncopyable.hpp>
#include <boost/scoped_ptr.hpp>

namespace muduo
{
namespace net
{

class Channel;
class EventLoop;

class ConnectorOri : boost::noncopyable,
                  public boost::enable_shared_from_this<ConnectorOri>
{
 public:
  typedef boost::function<void (int sockfd,const string &)> NewConnectionCallback;
  //add by egypt
  ConnectorOri(EventLoop* loop, const InetAddress& serverddr, const string& name = "", bool isBindAddr = false);
  ~ConnectorOri();

  void setNewConnectionCallback(const NewConnectionCallback& cb)
  { newConnectionCallback_ = cb; }

  void start();  // can be called in any thread
  void restart();  // must be called in loop thread
  void stop();  // can be called in any thread

  const InetAddress& serverAddress() const { return serverAddr_; }
  //add by egypt
  const string& clientName() const { return clientName_; }
  //add by egypt
  int& getConnName() { return connName_; }
  void setConnName(const int& connName){ connName_ = connName; }
  // add by egypt
  bool connectState_;
  // add by egypt
  bool retryState_;

  void setBindAddr(const InetAddress& addr)
  {
	  isbindLocalAddr_ = true;
	  bindLocalAddr_ = addr;
  };

  //bzh add 2015.8.20
  bool isConnected() {return state_== kConnected;};
 private:
  enum States { kDisconnected, kConnecting, kConnected };
  static const int kMaxRetryDelayMs = 30*1000;
  static const int kInitRetryDelayMs = 500;

  void setState(States s) { state_ = s; }
  void startInLoop();
  void stopInLoop();
  void connect();
  void connecting(int sockfd);
  void handleWrite();
  void handleError();
  void retry(int sockfd);
  int removeAndResetChannel();
  void resetChannel();

  EventLoop* loop_;
  InetAddress serverAddr_;

   //bzh add 2015.6.30 start
  bool isbindLocalAddr_;
  InetAddress bindLocalAddr_;
  //bzh add 2015.6.30 end

  bool connect_; // atomic
  States state_;  // FIXME: use atomic variable
  boost::scoped_ptr<Channel> channel_;
  NewConnectionCallback newConnectionCallback_;
  int retryDelayMs_;
  //add by egypt
  const string clientName_;
  //add by egypt
  int connName_;
};

}
}

#endif  // MUDUO_NET_CONNECTOR_H
