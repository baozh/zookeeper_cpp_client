// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef __ZK_TIMERQUEUE_H
#define __ZK_TIMERQUEUE_H

#include <set>
#include <vector>

#include <boost/noncopyable.hpp>

#include "muduo/base/Mutex.h"
#include "muduo/base/Timestamp.h"
#include "muduo/net/Callbacks.h"

#include "muduo/net/Timer.h"
#include "muduo/net/TimerId.h"

#include "ZkClient.h"



class ZkNetClient;

namespace ZkCppClient
{

class ZkTimerQueue
{
 public:
  ZkTimerQueue(int thread_id, int epoll_fd/*, SslClientThreadManager *manager*/);
  ~ZkTimerQueue();

  ///
  /// Schedules the callback to be run at given time,
  /// repeats if @c interval > 0.0.
  ///
  /// Must be thread safe. Usually be called from other threads.
  muduo::net::TimerId addTimer(const muduo::net::TimerCallback& cb,
                   muduo::Timestamp when,
                   double interval);

  void cancel(muduo::net::TimerId timerId);
  int getTimerFd() {return timerfd_;};

 private:

  typedef std::pair<muduo::Timestamp, muduo::net::Timer*> Entry;
  typedef std::set<Entry> TimerList;
  typedef std::pair<muduo::net::Timer*, int64_t> ActiveTimer;
  typedef std::set<ActiveTimer> ActiveTimerSet;

  void addTimerInLoop(muduo::net::Timer* timer);
  void cancelInLoop(muduo::net::TimerId timerId);

  void handleRead();
 
  std::vector<Entry> getExpired(muduo::Timestamp now);

  void reset(const std::vector<Entry>& expired, muduo::Timestamp now);

  bool insert(muduo::net::Timer* timer);

  //EventLoop* loop_;
  int threadId_;
  const int timerfd_;
  ZkNetClient* pTimerClient_;
  int epollfd_;

  //Channel timerfdChannel_;
  // Timer list sorted by expiration
  TimerList timers_;

  // for cancel()
  ActiveTimerSet activeTimers_;
  bool callingExpiredTimers_; /* atomic */
  ActiveTimerSet cancelingTimers_;
};

}

#endif
