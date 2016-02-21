// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//

#include "muduo/net/ConnectorOri.h"

#include "muduo/base/Logging.h"
#include "muduo/net/Channel.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/SocketsOps.h"

#include <boost/bind.hpp>

#include <errno.h>

using namespace muduo;
using namespace muduo::net;

const int ConnectorOri::kMaxRetryDelayMs;
//add by egypt
ConnectorOri::ConnectorOri(EventLoop* loop, const InetAddress& serverAddr, const string& name, bool isBindAddr)
  : connectState_(false),
	retryState_(false),
    loop_(loop),
    serverAddr_(serverAddr),
    connect_(false),
    state_(kDisconnected),
	retryDelayMs_(kInitRetryDelayMs),
    clientName_(name),
	isbindLocalAddr_(isBindAddr)   //bzh add 2015.6.30
{
  LOG_DEBUG << "ctor[" << this << "]";
}

ConnectorOri::~ConnectorOri()
{
  LOG_DEBUG << "dtor[" << this << "]";
  assert(!channel_);
}

void ConnectorOri::start()
{
  connect_ = true;
  loop_->runInLoop(boost::bind(&ConnectorOri::startInLoop, this)); // FIXME: unsafe
}

void ConnectorOri::startInLoop()
{
  loop_->assertInLoopThread();
  assert(state_ == kDisconnected);
  if (connect_)
  {
    connect();
  }
  else
  {
    LOG_DEBUG << "do not connect";
  }
}

void ConnectorOri::stop()
{
  connect_ = false;
  loop_->queueInLoop(boost::bind(&ConnectorOri::stopInLoop, this)); // FIXME: unsafe
  // FIXME: cancel timer
}

void ConnectorOri::stopInLoop()
{
  loop_->assertInLoopThread();
  if (state_ == kConnecting)
  {
    setState(kDisconnected);
    int sockfd = removeAndResetChannel();
    retry(sockfd);
  }
}

void ConnectorOri::connect()
{
  int sockfd = sockets::createNonblockingOrDie();

  //bzh add 2015.6.30
  if (isbindLocalAddr_ == true)
  {
	  //int option_value = 1;        
	  //int ret= setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,(char *)&option_value,sizeof(option_value));
	  int optval = 1;            //端口复用
	  ::setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, static_cast<socklen_t>(sizeof optval));

	  const struct sockaddr_in& localAddr_in = bindLocalAddr_.getSockAddrInet();
	  //int ret = ::bind(sockfd, sockaddr_cast(&localAddr_in), static_cast<socklen_t>(sizeof localAddr_in));
	  int ret = ::bind(sockfd, static_cast<const sockaddr*>(implicit_cast<const void*>(&localAddr_in)), static_cast<socklen_t>(sizeof localAddr_in));
	  if (ret < 0)
	  {
		  LOG_SYSFATAL << "bind failed!";
	  }
  }

  int ret = sockets::connect(sockfd, serverAddr_.getSockAddrInet());
  int savedErrno = (ret == 0) ? 0 : errno;
  switch (savedErrno)
  {
    case 0:
    case EINPROGRESS:
    case EINTR:
    case EISCONN:
      connecting(sockfd);
      break;

    case EAGAIN:
    case EADDRINUSE:
    case EADDRNOTAVAIL:
    case ECONNREFUSED:
    case ENETUNREACH:
      retry(sockfd);
      break;

    case EACCES:
    case EPERM:
    case EAFNOSUPPORT:
    case EALREADY:
    case EBADF:
    case EFAULT:
    case ENOTSOCK:
      LOG_SYSERR << "connect error in ConnectorOri::startInLoop " << savedErrno;
      sockets::close(sockfd);
      break;

    default:
      LOG_SYSERR << "Unexpected error in ConnectorOri::startInLoop " << savedErrno;
      sockets::close(sockfd);
      // connectErrorCallback_();
      break;
  }
}

void ConnectorOri::restart()
{
  loop_->assertInLoopThread();
  setState(kDisconnected);
  retryDelayMs_ = kInitRetryDelayMs;
  connect_ = true;
  startInLoop();
}

void ConnectorOri::connecting(int sockfd)
{
  setState(kConnecting);
  assert(!channel_);
  channel_.reset(new Channel(loop_, sockfd));
  channel_->setWriteCallback(
      boost::bind(&ConnectorOri::handleWrite, this)); // FIXME: unsafe
  channel_->setErrorCallback(
      boost::bind(&ConnectorOri::handleError, this)); // FIXME: unsafe

  // channel_->tie(shared_from_this()); is not working,
  // as channel_ is not managed by shared_ptr
  channel_->enableWriting();
}

int ConnectorOri::removeAndResetChannel()
{
  channel_->disableAll();
  channel_->remove();
  int sockfd = channel_->fd();
  // Can't reset channel_ here, because we are inside Channel::handleEvent
  //注意目前channel_正在当前loop的 currentActiveChannel_->handleEvent(pollReturnTime_);所以不能释放channel_对象
  loop_->queueInLoop(boost::bind(&ConnectorOri::resetChannel, this)); // FIXME: unsafe
  
  return sockfd;
}

void ConnectorOri::resetChannel()
{
  channel_.reset();
}

void ConnectorOri::handleWrite()
{
  LOG_TRACE << "ConnectorOri::handleWrite " << state_;

  if (state_ == kConnecting)
  {
    int sockfd = removeAndResetChannel();

    int err = sockets::getSocketError(sockfd);
    if (err)
    {
      LOG_WARN << "ConnectorOri::handleWrite - SO_ERROR = "
               << err << " " << strerror_tl(err);
      retry(sockfd);
    }
    else if (sockets::isSelfConnect(sockfd))
    {
      LOG_WARN << "ConnectorOri::handleWrite - Self connect";
      retry(sockfd);
    }
    else
    {
      setState(kConnected);
      if (connect_)
      {
        newConnectionCallback_(sockfd, clientName_);
      }
      else
      {
        sockets::close(sockfd);
      }
    }
  }
  else
  {
    // what happened?
    assert(state_ == kDisconnected);
  }
}

void ConnectorOri::handleError()
{
  LOG_ERROR << "ConnectorOri::handleError state=" << state_;
  if (state_ == kConnecting)
  {
    int sockfd = removeAndResetChannel();
    int err = sockets::getSocketError(sockfd);
    LOG_TRACE << "SO_ERROR = " << err << " " << strerror_tl(err);
    retry(sockfd);
  }
}

void ConnectorOri::retry(int sockfd)
{
  sockets::close(sockfd);
  setState(kDisconnected);
  if (connect_)
  {
    LOG_INFO << "ConnectorOri::retry - Retry connecting to " << serverAddr_.toIpPort()
             << " in " << retryDelayMs_ << " milliseconds. ";
    loop_->runAfter(retryDelayMs_/1000.0,
                    boost::bind(&ConnectorOri::startInLoop, shared_from_this()));
    retryDelayMs_ = std::min(retryDelayMs_ * 2, kMaxRetryDelayMs);
  }
  else
  {
    LOG_DEBUG << "do not connect";
  }
}

