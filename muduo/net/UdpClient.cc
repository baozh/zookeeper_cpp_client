#include <muduo/base/Logging.h>
#include <muduo/net/UdpClient.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
//#include <iostream>

using namespace muduo;
using namespace muduo::net;

pthread_key_t UdpClient::threadKey_;
pthread_once_t UdpClient::threadOnce_ = PTHREAD_ONCE_INIT;

UdpClient::UdpClient()
	: sndBufSize_ (-1)
{}

void UdpClient::createThreadData()
{
	if(pthread_key_create(&threadKey_, &UdpClient::releaseThreadData) != 0)
	{
	  LOG_ERROR << "create thread key failed, errmsg=" << strerror_tl(errno);
	}
}

void UdpClient::releaseThreadData(void *data)
{
	if(NULL != data)
	{
		delete (ThreadData*)data;
	}
}

void UdpClient::send(const char *buf, int size)
{
	ThreadData *threadData = getThreadData();
	if(NULL == threadData)
	{
		LOG_ERROR << "get thread data failed.";
		return;
	}
	// sockfd may be closed due to an io fault
	if(threadData->sockfd < 0)
	{
		threadData->sockfd = createSocket();
		if(threadData->sockfd < 0)
		{
			return;
		}
	}
	// choose an address
	if(threadData->addrList.empty())
	{
		LOG_WARN << "addr list is empty.";
		return;
	}
	InetAddress & addr = threadData->addrList[threadData->curAddrIndex];
	threadData->curAddrIndex = (threadData->curAddrIndex+1)%threadData->addrList.size();
	const struct sockaddr_in &sin = addr.getSockAddrInet();
	// send data
	if(sendto(threadData->sockfd, buf, size, 0, (struct sockaddr *)&sin, sizeof(sin)) != size)
	{
		close(threadData->sockfd);
		// open a new socket and retry 
		threadData->sockfd = createSocket();
		if(threadData->sockfd < 0)
		{
			return;
		}
		// retry
		if(sendto(threadData->sockfd, buf, size, 0, (struct sockaddr *)&sin, sizeof(sin)) != size)
		{
			LOG_ERROR << "sendto failed, errmsg=" << strerror_tl(errno);
			close(threadData->sockfd);
			threadData->sockfd = -1;
		}
	}
}

void UdpClient::setSndBufSize(int size)
{
	sndBufSize_ = size;
}

int UdpClient::createSocket()
{
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if(fd < 0)
	{
		LOG_ERROR << "create socket failed, errmsg=" << strerror_tl(errno);
		return -1;
	}
	int sndBufSize = sndBufSize_ ;
	if(sndBufSize != -1)
	{
		if(setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndBufSize, sizeof(sndBufSize)) != 0)
		{
			LOG_WARN << "set send buf size failed, size=" << sndBufSize_ << ", errmsg=" << strerror_tl(errno);
		}
		else
		{
			sndBufSize = 0;
			socklen_t len = sizeof(sndBufSize);
			if(getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndBufSize, &len) == 0 && sndBufSize < sndBufSize_)
			{
				LOG_WARN << "set send buf size failed, actual size is "	<< sndBufSize;
			}
		}
	}
	
	return fd;
}

UdpClient::ThreadData *UdpClient::getThreadData()
{
	// create thread specific key
	if(pthread_once(&threadOnce_, &UdpClient::createThreadData) != 0)
	{
		LOG_ERROR << "pthread_once failed, " << strerror_tl(errno);
		return NULL;
	}
	// get thread specific data
	ThreadData *threadData = (ThreadData *)pthread_getspecific(threadKey_);
	if(NULL == threadData)
	{
		// create thread specific data
		threadData = new ThreadData;
		assert(threadData);
		{
			muduo::MutexLockGuard lk(mutex_);
			threadData->addrList = addrList_;
		}
		threadData->curAddrIndex = 0;
		threadData->sockfd = -1;
		if(pthread_setspecific(threadKey_, threadData) != 0)
		{
			LOG_ERROR << "set thread specific data failed, errmsg=" << strerror_tl(errno);
			delete threadData;
			return NULL;
		}
	}
	return threadData;
}

// run in child thread
void UdpClient::resetThreadData(const NetAddrList &addrList)
{
	// get thread specific data
	ThreadData *threadData = (ThreadData *)pthread_getspecific(threadKey_);
	if(NULL != threadData)
	{
		threadData->addrList = addrList;
		threadData->curAddrIndex = 0;
	}
}

// run in main thread
void UdpClient::resetAddrList(const NetAddrList &addrList)
{
	muduo::MutexLockGuard lk(mutex_);
	addrList_ = addrList;
}