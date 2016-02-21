// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#define __STDC_LIMIT_MACROS
#include "muduo/base/Logging.h"
#include "muduo/net/Timer.h"
#include "muduo/net/TimerId.h"
#include <sys/timerfd.h>
#include <boost/bind.hpp>
#include "ZkTimerQueue.h"
#include "ZkClientManager.h"




using namespace muduo;
using namespace muduo::net;

namespace ZkCppClient
{

namespace ZkUtil
{
	int createTimerfd()
	{
	  int timerfd = ::timerfd_create(CLOCK_MONOTONIC,
									 TFD_NONBLOCK | TFD_CLOEXEC);
	  if (timerfd < 0)
	  {
		LOG_SYSFATAL << "Failed in timerfd_create";
	  }
	  return timerfd;
	}

	struct timespec howMuchTimeFromNow(Timestamp when)
	{
	  int64_t microseconds = when.microSecondsSinceEpoch()
							 - Timestamp::now().microSecondsSinceEpoch();
	  if (microseconds < 100)
	  {
		microseconds = 100;
	  }
	  struct timespec ts;
	  ts.tv_sec = static_cast<time_t>(
		  microseconds / Timestamp::kMicroSecondsPerSecond);
	  ts.tv_nsec = static_cast<long>(
		  (microseconds % Timestamp::kMicroSecondsPerSecond) * 1000);
	  LOG_DEBUG<<"ts.tv_nsec:["<<ts.tv_nsec<<"]----ts.tv.sec:["<<ts.tv_sec<<"]";
	  return ts;
	}

	void readTimerfd(int timerfd, Timestamp now)
	{
	  uint64_t howmany;
	  ssize_t n = ::read(timerfd, &howmany, sizeof howmany);
	  LOG_TRACE << "TimerQueue::handleRead() " << howmany << " at " << now.toString();
	  if (n != sizeof howmany)
	  {
		LOG_ERROR << "TimerQueue::handleRead() reads " << n << " bytes instead of 8";
	  }
	}

	void resetTimerfd(int timerfd, Timestamp expiration)
	{
	  // wake up loop by timerfd_settime()
	  struct itimerspec newValue;
	  struct itimerspec oldValue;
	  bzero(&newValue, sizeof newValue);
	  bzero(&oldValue, sizeof oldValue);
	  newValue.it_value = ZkUtil::howMuchTimeFromNow(expiration);
	  int ret = ::timerfd_settime(timerfd, 0, &newValue, &oldValue);
	  if (ret)
	  {
		LOG_SYSERR << "timerfd_settime()";
	  }
	}
}


ZkTimerQueue::ZkTimerQueue(int thread_id, int epoll_fd/*, SslClientThreadManager *manager*/)
  : threadId_(thread_id),
	epollfd_(epoll_fd),
	/*pClientManager_(manager),*/
    timerfd_(ZkUtil::createTimerfd()),
    timers_(),
    callingExpiredTimers_(false)
{
	//注册timefd 可读事件 到epollfd
	//创建timerfd相关的zkClient,zkChannel，并将可读事件 注册到epoll中.
	char clientName[48] = {0};
	snprintf(clientName, 48, "zk_client_timerfd_%d_epollfd_%d_threadid_%d", timerfd_, epollfd_, muduo::CurrentThread::tid());
	pTimerClient_ = new ZkNetClient(epollfd_, thread_id, timerfd_, clientName);
	pTimerClient_->setReadTimerCb(boost::bind(&ZkTimerQueue::handleRead, this));
	ZkUtil::enableReading(pTimerClient_);
	ZkUtil::addEpollFd(epollfd_, pTimerClient_);
}

ZkTimerQueue::~ZkTimerQueue()
{
	ZkUtil::disableAll(pTimerClient_);
	ZkUtil::modEpollFd(epollfd_, pTimerClient_);
	::close(timerfd_);

  // do not remove channel, since we're in EventLoop::dtor();
  for (TimerList::iterator it = timers_.begin();
      it != timers_.end(); ++it)
  {
    delete it->second;
  }
}

TimerId ZkTimerQueue::addTimer(const TimerCallback& cb,
                             Timestamp when,
                             double interval)
{
  Timer* timer = new Timer(cb, when, interval);
    CbFunManager::instance().runInThread(threadId_, boost::bind(&ZkTimerQueue::addTimerInLoop, this, timer));
  return TimerId(timer, timer->sequence());
}

void ZkTimerQueue::addTimerInLoop(Timer* timer)
{
	//loop_->assertInLoopThread();
	bool earliestChanged = insert(timer);

	if (earliestChanged)
	{
		ZkUtil::resetTimerfd(timerfd_, timer->expiration());
	}
}

bool ZkTimerQueue::insert(Timer* timer)
{
	//loop_->assertInLoopThread();
	assert(timers_.size() == activeTimers_.size());
	bool earliestChanged = false;
	Timestamp when = timer->expiration();
	TimerList::iterator it = timers_.begin();
	if (it == timers_.end() || when < it->first)
	{
		earliestChanged = true;
	}

	{
		std::pair<TimerList::iterator, bool> result = timers_.insert(Entry(when, timer));
		assert(result.second); (void)result;
	}

	{
		std::pair<ActiveTimerSet::iterator, bool> result = activeTimers_.insert(ActiveTimer(timer, timer->sequence()));
		assert(result.second); (void)result;
	}

	assert(timers_.size() == activeTimers_.size());
	return earliestChanged;
}

void ZkTimerQueue::handleRead()
{
	//loop_->assertInLoopThread();
	LOG_DEBUG<<"--func:["<<__FUNCTION__<<"]---line:["<<__LINE__<<"]";
	Timestamp now(Timestamp::now());
	ZkUtil::readTimerfd(timerfd_, now);

	std::vector<Entry> expired = getExpired(now);

	callingExpiredTimers_ = true;
	cancelingTimers_.clear();
	// safe to callback outside critical section
	for (std::vector<Entry>::iterator it = expired.begin();
		it != expired.end(); ++it)
	{
		it->second->run();
	}
	callingExpiredTimers_ = false;

	reset(expired, now);
}

std::vector<ZkTimerQueue::Entry> ZkTimerQueue::getExpired(Timestamp now)
{
	assert(timers_.size() == activeTimers_.size());
	std::vector<Entry> expired;
	Entry sentry(now, reinterpret_cast<Timer*>(UINTPTR_MAX));
	TimerList::iterator end = timers_.lower_bound(sentry);
	assert(end == timers_.end() || now < end->first);
	std::copy(timers_.begin(), end, back_inserter(expired));
	timers_.erase(timers_.begin(), end);

	for (std::vector<Entry>::iterator it = expired.begin();
		it != expired.end(); ++it)
	{
		ActiveTimer timer(it->second, it->second->sequence());
		size_t n = activeTimers_.erase(timer);
		assert(n == 1); (void)n;
	}

	assert(timers_.size() == activeTimers_.size());
	return expired;
}

void ZkTimerQueue::reset(const std::vector<Entry>& expired, Timestamp now)
{
	Timestamp nextExpire;

	for (std::vector<Entry>::const_iterator it = expired.begin();
		it != expired.end(); ++it)
	{
		ActiveTimer timer(it->second, it->second->sequence());
		if (it->second->repeat()
			&& cancelingTimers_.find(timer) == cancelingTimers_.end())
		{
			it->second->restart(now);
			insert(it->second);
		}
		else
		{
			// FIXME move to a free list
			delete it->second; // FIXME: no delete please
		}
	}

	if (!timers_.empty())
	{
		nextExpire = timers_.begin()->second->expiration();
	}

	if (nextExpire.valid())
	{
		ZkUtil::resetTimerfd(timerfd_, nextExpire);
	}
}

void ZkTimerQueue::cancel(TimerId timerId)
{
    CbFunManager::instance().runInThread(threadId_, boost::bind(&ZkTimerQueue::cancelInLoop, this, timerId));
}

void ZkTimerQueue::cancelInLoop(TimerId timerId)
{
  //loop_->assertInLoopThread();
  assert(timers_.size() == activeTimers_.size());
  ActiveTimer timer(timerId.getTimer(), timerId.getSeq());
  ActiveTimerSet::iterator it = activeTimers_.find(timer);
  if (it != activeTimers_.end())
  {
    size_t n = timers_.erase(Entry(it->first->expiration(), it->first));
    assert(n == 1); (void)n;
    delete it->first; // FIXME: no delete please
    activeTimers_.erase(it);
  }
  else if (callingExpiredTimers_)
  {
    cancelingTimers_.insert(timer);
  }
  assert(timers_.size() == activeTimers_.size());
}

}



