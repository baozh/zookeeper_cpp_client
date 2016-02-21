/*
 * ZkClientManager.cc
 *
 *      Created on: 2016年2月20日
 *      Author: ZengHui Bao (bao_z_h@163.com)
 */

#include "ZkClientManager.h"

#include "muduo/base/CurrentThread.h"
#include "muduo/base/AsyncLogging.h"
#include <signal.h>
#include <sys/eventfd.h>


namespace ZkCppClient
{

//epoll统一用水平触发
__thread int t_epollfd = 0;    //线程私有变量，一个线程使用一个epoll
__thread int t_eventfd = 0;    //线程私有变量，一个线程使用一个eventfd（用来支持runInThread）
__thread int t_timerfd = 0;    //线程私有变量，一个线程使用一个timerfd（用来支持定时器）


muduo::AsyncLogging *gpAsyncLog = NULL;

static bool sIsDebugLogLevel = true;
static std::string sZkLogPath = "";
static FILE* sZkLogFp = NULL;

void asyncLogFun(const char *data, int len)
{
    if (gpAsyncLog != NULL)
    {
        gpAsyncLog->append(data, len);
    }
}

bool ZkClientManager::setLogConf(bool isDebug, const std::string& zkLogFilePath)
{
    sIsDebugLogLevel = isDebug;
    sZkLogPath = zkLogFilePath;

    // log级别
    ZooLogLevel log_level = isDebug ? ZOO_LOG_LEVEL_DEBUG : ZOO_LOG_LEVEL_INFO;
    zoo_set_debug_level(log_level);
    std::cout << "[ZkClientManager::setLogConf] isDebugLogLevel:" << isDebug << std::endl;

    // log目录
    if (zkLogFilePath != "")
    {
        std::cout << "[ZkClientManager::setLogConf] log_path:" << zkLogFilePath << std::endl;
        sZkLogFp = fopen(zkLogFilePath.c_str(), "w");
        if (!sZkLogFp)
        {
            std::cout << "[ZkClientManager::setLogConf] Can't open this log path. path " << zkLogFilePath << std::endl;
            return false;
        }
        zoo_set_log_stream(sZkLogFp);
    }

    zoo_deterministic_conn_order(1); // enable deterministic order
    return true;
}

ZkClientManager::ZkClientManager()
{
    nextHandle_ = 1;
    zkThreads_.clear();
    isExit_ = false;
    zkNetClients_.clear();
    totalZkClients_.clear();
    zkNetClients_.clear();

    //只能调一次
    init();
}

ZkClientManager::~ZkClientManager()
{
	isExit_ = true;

	std::vector<muduo::Thread*>::iterator iter = zkThreads_.begin();
	for (; iter != zkThreads_.end(); iter++)
	{
		if ((*iter) != NULL)
		{
			(*iter)->join();
			delete (*iter);
		}
	}
	zkThreads_.clear();

	totalZkClients_.clear();

	std::vector<ZkNetClient*>::iterator iterNet = zkNetClients_.begin();
	for (; iterNet != zkNetClients_.end(); iterNet++)
	{
		if ((*iterNet) != NULL)
		{
			delete (*iterNet);
		}
	}
	zkNetClients_.clear();
}

void ZkClientManager::init()
{
#define  NET_THREAD_NUM     2

	//初始化日志线程
	gpAsyncLog = new muduo::AsyncLogging("zk_cpp_client", 50*1000*1000, 3);
	muduo::Logger::setLogLevel(sIsDebugLogLevel ? muduo::Logger::DEBUG : muduo::Logger::WARN);
	muduo::Logger::setAsynOutput(asyncLogFun);
	gpAsyncLog->start();

	//创建 多个线程
	//两个线程：
    //  线程1：定时(1ms~10ms)检查zookeeper session是否超时。
    //  线程2：
    //        1）如果session失效，则需要定时重连zookeeper server
    //        2）触发watcher后，需重复注册watcher(用了阻塞的方式注册watch,为了避免阻塞原线程，故用另外的线程来注册)
    //        3）与zookeeper server重连成功后，需要 重新注册watcher。
    //        4）为了避免丢失watcher（小概率事件），每半小时 重新注册 所有的watcher。（暂时不做）
	//        5）创建叶子结点时，如果分支结点不存在，需要递归创建目录结点
    //        6）删除分支结点时，如果含有叶子结点，需要递归删除所有叶子结点
	char threadName[48] = {0};
	for (int i = 1; i <= NET_THREAD_NUM; i++)
	{
		snprintf(threadName, 48, "zk_thread_%d", i);
		muduo::Thread* pThreadHandle = new muduo::Thread(boost::bind(&ZkClientManager::LoopFun, this), threadName);
		if (pThreadHandle != NULL)
		{
			zkThreads_.push_back(pThreadHandle);
		}
	}

	for (std::vector<muduo::Thread*>::iterator iter = zkThreads_.begin(); iter != zkThreads_.end(); iter++)
	{
		if ((*iter) != NULL)
		{
			(*iter)->start();
		}
	}
};

//子线程中  线程执行函数
void ZkClientManager::LoopFun()
{
	//创建线程时，在线程函数中 创建
	t_epollfd = epoll_create1(EPOLL_CLOEXEC);

	char clientName[48] = {0};

	//创建eventfd
	t_eventfd = CbFunManager::instance().insertOrSetThreadData(muduo::CurrentThread::tid(), t_epollfd);

	//创建eventfd相关的netClient,connChannel，并将可读事件 注册到epoll中.
	snprintf(clientName, 48, "net_client_eventfd_%d_epollfd_%d_threadid_%d", t_eventfd, t_epollfd, muduo::CurrentThread::tid());
	ZkNetClient* pEventClient = new ZkNetClient(t_epollfd, muduo::CurrentThread::tid(), t_eventfd, clientName);
	ZkUtil::enableReading(pEventClient);
	ZkUtil::addEpollFd(t_epollfd, pEventClient);

	//创建 timerfd,timerQueue，并将可读事件 注册到epoll中.
	t_timerfd = ZkTimerManager::instance().insertTimeQueue(muduo::CurrentThread::tid(), t_epollfd);

	LOG_WARN << "Zk thread id:" << muduo::CurrentThread::tid() << ", epollfd:" << t_epollfd << ", eventfd:" << t_eventfd << ", timerfd:" << t_timerfd;

	volatile uint64_t loopIndex = 0;
	LOG_DEBUG << "loop start, loopIndex:" << loopIndex << ", threadid:" << muduo::CurrentThread::tid();
	while (isExit_ == false) 
	{
		loopIndex++;
		loop_once(t_epollfd, ZkUtil::kThisEpollTimeMs, loopIndex);
	}

	close(t_epollfd);
	close(t_eventfd);
	LOG_WARN << "thread exit. thread id:" << muduo::CurrentThread::tid();
}

//子ssl线程中
void ZkClientManager::loop_once(int epollfd, int waitms, uint64_t loop_index)
{
	const int kMaxEvents = 48;
	struct epoll_event activeEvs[kMaxEvents];
	int n = epoll_wait(epollfd, activeEvs, kMaxEvents, waitms);

	for (int i = n-1; i >= 0; i --) 
	{
		ZkNetClient* ch = (ZkNetClient*)activeEvs[i].data.ptr;
		if (ch == NULL || ch->getChannel() == NULL)
		{
			continue;
		}

		int events = activeEvs[i].events;
		if ((events & POLLHUP) && !(events & POLLIN) && (ch != NULL && ch->getChannel() != NULL))
		{
			LOG_DEBUG << "(thread id:" << muduo::CurrentThread::tid() << ", epollfd:" << epollfd << ", fd:" << ch->getChannel()->fd_ << ", netName:" << ch->getNetName()
				<< ", loopIndex:" << loop_index
				<< ") peer close connection!";

			//ch->handleClose();   //要调用onConnection()回调
		}
		if ((events & POLLNVAL) && (ch != NULL && ch->getChannel() != NULL))
		{
			LOG_DEBUG << "Channel::handle_event() POLLNVAL";
		}
		if ((events & (POLLERR | POLLNVAL)) && (ch != NULL && ch->getChannel() != NULL))
		{
			LOG_DEBUG << "(thread id:" << muduo::CurrentThread::tid() << ", epollfd:" << epollfd << ", fd:" << ch->getChannel()->fd_ << ", netName:" << ch->getNetName()
				<< ", loopIndex:" << loop_index
				<< ") handle error.";
			//ch->handleError();
		}
		if ((events & (POLLIN | POLLPRI | POLLRDHUP)) && (ch != NULL && ch->getChannel() != NULL))
		{
			LOG_DEBUG << "(thread id:" << muduo::CurrentThread::tid() << ", epollfd:" << epollfd << ", fd:" << ch->getChannel()->fd_ << ", netName:" << ch->getNetName()
				<< ", loopIndex:" << loop_index
				<< ") handle read.";
			ch->handleRead();
		}
		if ((events & POLLOUT) && (ch != NULL && ch->getChannel() != NULL))
		{
			LOG_DEBUG << "(thread id:" << muduo::CurrentThread::tid() << ", epollfd:" << epollfd << ", fd:" << ch->getChannel()->fd_ << ", netName:" << ch->getNetName()
				<< ", loopIndex:" << loop_index
				<< ") handle write.";
			ch->handleWrite();
		}
	}

	//处理 runInThread中的回调函数
    CbFunManager::instance().doPendingFunctors(muduo::CurrentThread::tid());
}

//内部使用，在session init的时候，需要从这里获取zkclientptr
ZkClientPtr ZkClientManager::__getZkClient(uint32_t handle)
{
	ZkClientPtr client;
	clientMutex_.lock();
	if (totalZkClients_.find(handle) != totalZkClients_.end())
	{
		client = totalZkClients_[handle];
	}
	else
	{
		LOG_WARN << "Can't find this zkclient! handle:" << handle;
	}
	clientMutex_.unlock();

	return client;
}

uint32_t ZkClientManager::createZkClient(const std::string& host, int timeout, SessionClientId *clientId /*= NULL*/,
                                     ZkUtil::SessionExpiredHandler expired_handler /*= NULL*/, void* context /*= NULL*/)
{
    nextHandle_++;
    if (nextHandle_ == 0)
    {
        nextHandle_++;
    }

    ZkClientPtr client(new ZkClient(nextHandle_));
	clientMutex_.lock();
	totalZkClients_[nextHandle_] = client;
	clientMutex_.unlock();

    if (client->init(host,timeout, clientId, expired_handler, context) == false)
    {
        LOG_ERROR << "zkclient init failed! handle:" << nextHandle_ << ", host:" << host << ", timeout:" << timeout;
        return 0;
    }
    return nextHandle_;
}

void ZkClientManager::destroyClient(uint32_t handle)
{
	clientMutex_.lock();
	if (totalZkClients_.find(handle) != totalZkClients_.end())
	{
		totalZkClients_.erase(handle);
	}
	else
	{
		LOG_WARN << "Can't find this zkclient! handle:" << handle;
	}
	clientMutex_.unlock();
}

//根据handle，返回zkClient对象
ZkClientPtr ZkClientManager::getZkClient(uint32_t handle)
{
    ZkClientPtr client;
    clientMutex_.lock();
    if (totalZkClients_.find(handle) != totalZkClients_.end())
    {
		if (totalZkClients_[handle]->isInit())
		{
			client = totalZkClients_[handle];
		}
    }
    else
    {
        LOG_WARN << "Can't find this zkclient! handle:" << handle;
    }
    clientMutex_.unlock();

    return client;
}


CbFunManager::CbFunManager()
{
	threadDatas_.clear();

	pendingFunctors_ = new std::map<int, std::vector<Functor> >();
	pendingFunctors_->clear();
}

CbFunManager::~CbFunManager()
{
	dataMutex_.lock();
	threadDatas_.clear();
	dataMutex_.unlock();

	funsMutex_.lock();
	if (pendingFunctors_)
	{
		pendingFunctors_->clear();
		delete pendingFunctors_;
        pendingFunctors_ = NULL;
	}
	funsMutex_.unlock();
}

CbFunManager& CbFunManager::instance()
{
	return muduo::Singleton<CbFunManager>::instance();
}

void CbFunManager::runInThread(int thread_id, const Functor& cb)
{
	if (isInCurrentThread(thread_id))
	{
		cb();
	}
	else
	{
		queueInThreadFuns(thread_id, cb);
	}
}

bool CbFunManager::isInCurrentThread(int thread_id)
{
	if (muduo::CurrentThread::tid() == thread_id)
	{
		return true;
	}
	else
	{
		return false;
	}
}

void CbFunManager::queueInThreadFuns(int thread_id, const Functor& cb)
{
	funsMutex_.lock();
	if (pendingFunctors_ != NULL)
	{
		if (pendingFunctors_->find(thread_id) == pendingFunctors_->end())
		{
			pendingFunctors_->insert(std::make_pair(thread_id, std::vector<Functor>()));
			(*pendingFunctors_)[thread_id].clear();
		}
		(*pendingFunctors_)[thread_id].push_back(cb);
	}
	else
	{
		LOG_ERROR << "pendingFunctors_ is NULL!";
	}
	funsMutex_.unlock();

	wakeup(thread_id);
}

void CbFunManager::wakeup(int thread_id)
{
	threadData data;
	bool getData = false;

	dataMutex_.lock();
	if (threadDatas_.find(thread_id) != threadDatas_.end())
	{
		data = threadDatas_[thread_id];
		getData = true;
	}
	dataMutex_.unlock();

	if (getData == false)
	{
		LOG_ERROR << "Can't find this thread data.thread_id:" << thread_id;
		return;
	}

	if (!isInCurrentThread(thread_id) || data.callingPendingFunctors_)
	{
		uint64_t one = 1;
		ssize_t n = sockets::write(data.eventfd_, &one, sizeof one);
		if (n != sizeof one)
		{
			LOG_ERROR << "cbFunManager::wakeup() writes " << n << " bytes instead of 8";
		}
	}
}

void CbFunManager::doPendingFunctors(int thread_id)
{
	dataMutex_.lock();
	if (threadDatas_.find(thread_id) != threadDatas_.end())
	{
		threadDatas_[thread_id].callingPendingFunctors_ = true;
	}
	dataMutex_.unlock();

	std::vector<Functor> functors;
	funsMutex_.lock();
	if (pendingFunctors_ != NULL)
	{
		if (pendingFunctors_->find(thread_id) != pendingFunctors_->end())
		{
			functors.swap((*pendingFunctors_)[thread_id]);
		}
	}
	else
	{
		LOG_ERROR << "pendingFunctors_ is NULL!";
	}
	funsMutex_.unlock();

	for (size_t i = 0; i < functors.size(); ++i)
	{
		functors[i]();
	}

	dataMutex_.lock();
	if (threadDatas_.find(thread_id) != threadDatas_.end())
	{
		threadDatas_[thread_id].callingPendingFunctors_ = false;
	}
	dataMutex_.unlock();
}

//返回eventfd，在外面 将eventfd的事件 注册到epoll中.
int CbFunManager::insertOrSetThreadData(int thread_id, int epollfd)
{
	threadData data;
	data.eventfd_ = ZkUtil::createEventfd();
	data.epollfd_ = epollfd;
	data.callingPendingFunctors_ = false;

	dataMutex_.lock();
	threadDatas_[thread_id] = data;
	dataMutex_.unlock();

	return data.eventfd_;
}

ZkTimerManager& ZkTimerManager::instance()
{
	return muduo::Singleton<ZkTimerManager>::instance();
}

//返回timerfd
int ZkTimerManager::insertTimeQueue(int thread_id, int epollfd)
{
	int timerFd = -1;
	timeMutex_.lock();
	if (timerQueues_ != NULL)
	{
		if (timerQueues_->find(thread_id) == timerQueues_->end())
		{
			ZkTimerQueue *queue = new ZkTimerQueue(thread_id, epollfd);
			timerQueues_->insert(std::make_pair(thread_id, queue));
		}
		timerFd = (*timerQueues_).at(thread_id)->getTimerFd();   //[thread_id].getTimerFd();
	}
	timeMutex_.unlock();

	return timerFd;
}

ZkTimerManager::~ZkTimerManager()
{
	timeMutex_.lock();
	if (timerQueues_ != NULL)
	{
		std::map<int, ZkTimerQueue*>::iterator iter = timerQueues_->begin();
		for(; iter != timerQueues_->end(); iter++)
		{
			if ((iter->second) != NULL)
			{
				delete (iter->second);
				iter->second = NULL;
			}
		}
		timerQueues_->clear();
		delete timerQueues_;
		timerQueues_ = NULL;
	}
	timeMutex_.unlock();
}

void ZkTimerManager::runAt(int thread_id, const Timestamp& time, const TimerCallback& cb)
{
	timeMutex_.lock();
	if (timerQueues_ != NULL && timerQueues_->find(thread_id) != timerQueues_->end() &&
		(*timerQueues_).at(thread_id) != NULL)
	{
		(*timerQueues_).at(thread_id)->addTimer(cb, time, 0.0);
	}
	timeMutex_.unlock();
}

void ZkTimerManager::runAfter(int thread_id, double delay, const TimerCallback& cb)
{
	Timestamp time(addTime(Timestamp::now(), delay));
	runAt(thread_id, time, cb);
}

void ZkTimerManager::runEvery(int thread_id, double interval, const TimerCallback& cb)
{
	Timestamp time(addTime(Timestamp::now(), interval));

	timeMutex_.lock();
	if (timerQueues_ != NULL && timerQueues_->find(thread_id) != timerQueues_->end() &&
		(*timerQueues_).at(thread_id) != NULL)
	{
		(*timerQueues_).at(thread_id)->addTimer(cb, time, interval);
	}
	timeMutex_.unlock();
}

}
