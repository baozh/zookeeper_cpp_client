/*
 * ZkClientManager.h
 *
 *      Created on: 2016年2月20日
 *      Author: ZengHui Bao (bao_z_h@163.com)
 */

#ifndef __ZK_CLIENT_MANAGER_H
#define __ZK_CLIENT_MANAGER_H


#include <string>
#include <map>
#include <vector>
#include "muduo/base/Singleton.h"
#include "muduo/base/Thread.h"
#include "muduo/base/Mutex.h"
#include "muduo/base/Logging.h"
#include <boost/noncopyable.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include "ZkTimerQueue.h"
#include "ZkClient.h"


using namespace muduo::net;
using namespace muduo;



namespace ZkCppClient
{

class ZkClientManager : boost::noncopyable
{
public:
	ZkClientManager();

	~ZkClientManager();

	ZkClientPtr __getZkClient(uint32_t handle);

	int getFirstThreadId()
	{
		if (zkThreads_.empty() == false && zkThreads_[0] != NULL)
		{
            return (zkThreads_[0])->tid();
		}
        return 0;
	}

	int getSecondThreadId()
	{
		if (zkThreads_.empty() == false && (zkThreads_.size() >= 2) && zkThreads_[1] != NULL)
		{
			return (zkThreads_[1])->tid();
		}
		return 0;
	}

public:
	//对外服务接口
    //需要先调这个接口 来 初始化 日志相关的参数、日志线程
    static bool setLogConf(bool isDebug, const std::string& zkLogFilePath = "");

	static ZkClientManager& instance() {return muduo::Singleton<ZkClientManager>::instance();};

    //创建一个ZkClient, 如果创建成功，会返回 它对应的handle; 创建失败，返回0 (线程安全)
	//SessionClientId通常为NULL, 若不为NLL，则 SessionClientId 须是 已建立成功的zkclient的 clientId.
    uint32_t createZkClient(const std::string& host, int timeout, SessionClientId *clientId = NULL,
                        ZkUtil::SessionExpiredHandler expired_handler = NULL, void* context = NULL);

    //根据handle，返回ZkClientPtr对象.如果找不到，返回空指针的shared_ptr. (线程安全)
    ZkClientPtr getZkClient(uint32_t handle);

	void destroyClient(uint32_t handle);

private:
	void init();
	void LoopFun();
	void loop_once(int epollfd, int waitms, uint64_t loop_index);

private:

	std::vector<muduo::Thread*> zkThreads_;     //主要运行定时器 和 runInThread 注册的函数
	volatile bool isExit_;  /* atomic */        //一个全局的开关，所有线程是否停止 运行

	std::map<uint32_t, ZkClientPtr> totalZkClients_;  //map<handle, ZkClientPtr>
	muduo::MutexLock clientMutex_;

	std::vector<ZkNetClient*> zkNetClients_;

	uint32_t nextHandle_;
};


//管理runInThread的回调函数
class CbFunManager : boost::noncopyable
{
public:
	typedef boost::function<void()> Functor;
	friend class ZkClientManager;

	//为了加快速度，每个线程的epollfd、eventfd存两份，一份存线程私有数据，一份存threadDatas_
	struct threadData
	{
		threadData()
		{
			epollfd_ = 0;
			eventfd_ = 0;
			callingPendingFunctors_ = false;
		}
		int epollfd_;    
		int eventfd_;
		volatile bool callingPendingFunctors_; /* atomic */
	};

public:
	CbFunManager();
	~CbFunManager();

	// singleton
	static CbFunManager& instance();

	void runInThread(int thread_id, const Functor& cb);

private:
	//返回eventfd，在外面 将eventfd的事件 注册到epoll中.
	int insertOrSetThreadData(int thread_id, int epollfd);

	bool isInCurrentThread(int thread_id);

	void queueInThreadFuns(int thread_id, const Functor& cb);

	void wakeup(int thread_id);

	void doPendingFunctors(int thread_id);

private:
	std::map<int, threadData> threadDatas_;   //<thread_id, threadData>
	muduo::MutexLock dataMutex_;

	std::map<int, std::vector<Functor> >* pendingFunctors_;  //<thread_id, cbfun_list>
	muduo::MutexLock funsMutex_;
};


class ZkTimerManager : boost::noncopyable
{
public:
	friend class ZkClientManager;

	ZkTimerManager()
	{
		timerQueues_ = new std::map<int, ZkTimerQueue*>();
		timerQueues_->clear();
	}

	~ZkTimerManager();

	// singleton
	static ZkTimerManager& instance();

	void runAt(int thread_id, const Timestamp& time, const TimerCallback& cb);

	//delay 单位为秒，可以用小数
	void runAfter(int thread_id, double delay, const TimerCallback& cb);

	//interval 单位为秒，可以用小数
	void runEvery(int thread_id, double interval, const TimerCallback& cb);

private:
	//返回timerfd
	int insertTimeQueue(int thread_id, int epollfd);

public:
	//因为 SslTimerQueue中的timerfd不应该重复创建，所以SslTimerQueue是不可复制的，所以 存指针
	std::map<int, ZkTimerQueue*>* timerQueues_;  
	muduo::MutexLock timeMutex_;
};

}

#endif
