/*
 * ZkClient.cc
 *
 *      Created on: 2016年2月20日
 *      Author: ZengHui Bao (bao_z_h@163.com)
 */


#include <sys/eventfd.h>
#include <iostream>
#include "ZkClient.h"
#include "ZkClientManager.h"


namespace ZkCppClient
{

extern __thread int t_eventfd;    //线程私有变量，一个线程使用一个eventfd（用来支持runInThread）
extern __thread int t_timerfd;    //线程私有变量，一个线程使用一个timerfd（用来支持定时器）


void ZkNetClient::handleRead()
{
	if (pConnChannel_ == NULL)
		return;

	//如果是 eventfd的事件，则调用 handleEventFdRead
	if (pConnChannel_->fd_ == t_eventfd)
	{
		LOG_DEBUG << "pick the event. eventfd:" << t_eventfd << ", clientName:" << netName_;
		handleEventFdRead(t_eventfd);
		return;
	}

	//如果是 timerfd的事件，则调用 handleTimerFdRead
	if (pConnChannel_->fd_ == t_timerfd)
	{
		LOG_DEBUG << "pick the event. timerfd:" << t_timerfd << ", clientName:" << netName_;
		handleTimerFdRead();
		return;
	}
}

void ZkNetClient::handleEventFdRead(int eventfd)
{
	uint64_t one = 1;
	ssize_t n = sockets::read(eventfd, &one, sizeof one);
	if (n != sizeof one)
	{
		LOG_ERROR << "ZkNetClient::handleEventFdRead() reads " << n << " bytes instead of 8";
	}
}

void ZkNetClient::handleTimerFdRead() 
{
	if (timerReadCb_)
	{
		timerReadCb_();
	}
}

void ZkNetClient::setReadTimerCb(ReadTimerCallback cb)
{
	timerReadCb_ = cb;
};

void ZkNetClient::handleWrite()
{
	if (pConnChannel_ == NULL) return;

	LOG_DEBUG << "[pConnChannel_::handleWrite] fd:" << pConnChannel_->fd_ << ", netName:" << netName_;
	//去掉 监听写事件
	if (pConnChannel_)
	{
		pConnChannel_->events_ &= ~EPOLLOUT;
		pConnChannel_->update(this);
	}
};


ZkOperateAndWatchContext::ZkOperateAndWatchContext(const std::string& path, void* context, ZkClientPtr zkclient)
{
	this->path_ = path;
	this->context_ = context;
	this->zkclient_ = zkclient;
};

ZkClient::NodeWatchData::NodeWatchData()
{
    path_ = "";
    handler_ = NULL;
    context_ = NULL;
    value_ = "";
    version_ = ZkUtil::kInvalidDataVersion;
    isSupportAutoReg_ = true;
}

ZkClient::NodeWatchData::NodeWatchData(const ZkClient::NodeWatchData& data)
{
    if (this == &data)
        return;

    this->path_ = data.path_;
    this->handler_ = data.handler_;
    this->context_ = data.context_;
    this->value_ = data.value_;
    this->version_ = data.version_;
    this->isSupportAutoReg_ = data.isSupportAutoReg_;
}

ZkClient::NodeWatchData& ZkClient::NodeWatchData::operator= (const ZkClient::NodeWatchData& data)
{
    if (this == &data)
        return *this;

    this->path_ = data.path_;
    this->handler_ = data.handler_;
    this->context_ = data.context_;
    this->value_ = data.value_;
    this->version_ = data.version_;
    this->isSupportAutoReg_ = data.isSupportAutoReg_;

    return *this;
}

ZkClient::ChildWatchData::ChildWatchData()
{
    path_ = "";
    handler_ = NULL;
    context_ = NULL;
    childList_.clear();
    isSupportAutoReg_ = true;
}

ZkClient::ChildWatchData::ChildWatchData(const ZkClient::ChildWatchData& data)
{
    if (this == &data)
        return;

    this->path_ = data.path_;
    this->handler_ = data.handler_;
    this->context_ = data.context_;
    this->childList_.assign(data.childList_.begin(), data.childList_.end());
    this->isSupportAutoReg_ = data.isSupportAutoReg_;
}

ZkClient::ChildWatchData& ZkClient::ChildWatchData::operator= (const ZkClient::ChildWatchData& data)
{
    if (this == &data)
        return *this;

    this->path_ = data.path_;
    this->handler_ = data.handler_;
    this->context_ = data.context_;
    this->childList_.assign(data.childList_.begin(), data.childList_.end());
    this->isSupportAutoReg_ = data.isSupportAutoReg_;

    return *this;
}

int ZkClient::getSessStat()
{
    int retStat;
    sessStateMutex_.lock();
    retStat = sessionState_;
    sessStateMutex_.unlock();
    return  retStat;
};

void ZkClient::setSessStat(int stat)
{
    sessStateMutex_.lock();
    sessionState_ = stat;
    sessStateMutex_.unlock();
};

int ZkClient::getSessTimeout()
{
    int retTime;
    sessTimeoutMutex_.lock();
    retTime = sessionTimeout_;
    sessTimeoutMutex_.unlock();
    return  retTime;
};

void ZkClient::setSessTimeout(int time)
{
    sessTimeoutMutex_.lock();
    sessionTimeout_ = time;
    sessTimeoutMutex_.unlock();
};

int64_t ZkClient::getSessDisconn()
{
    int64_t disconn;
    sessDisconnMutex_.lock();
    disconn = sessionDisconnectMs_;
    sessDisconnMutex_.unlock();
    return disconn;
};

void ZkClient::setSessDisconn(int64_t disconn)
{
    sessDisconnMutex_.lock();
    sessionDisconnectMs_ = disconn;
    sessDisconnMutex_.unlock();
};

void ZkClient::setNodeWatchData(const std::string& path, const NodeWatchData& data)
{
    nodeWatchMutex_.lock();
    nodeWatchDatas_[path] = data;
    nodeWatchMutex_.unlock();
}

bool ZkClient::getNodeWatchData(const std::string& path, NodeWatchData& retNodeWatchData)
{
    bool result = false;
    nodeWatchMutex_.lock();
    if (nodeWatchDatas_.find(path) != nodeWatchDatas_.end())
    {
        retNodeWatchData = nodeWatchDatas_[path];
        result = true;
    }
    nodeWatchMutex_.unlock();
    return result;
}

bool ZkClient::isShouldNotifyNodeWatch(const std::string& path)
{
    bool result = false;
    nodeWatchMutex_.lock();
    //如果从map中找到缓存数据，就认为 是应该回调用户函数的.
    if (nodeWatchDatas_.find(path) != nodeWatchDatas_.end())
    {
        result = true;
    }
    nodeWatchMutex_.unlock();
    return result;
}

bool ZkClient::isShouldNotifyChildWatch(const std::string& path)
{
    bool result = false;
    childWatchMutex_.lock();
    //如果从map中找到缓存数据，就认为 是应该回调用户函数的.
    if (childWatchDatas_.find(path) != childWatchDatas_.end())
    {
        result = true;
    }
    childWatchMutex_.unlock();
    return result;
}

void ZkClient::setChildWatchData(const std::string& path, const ChildWatchData& data)
{
    childWatchMutex_.lock();
    childWatchDatas_[path] = data;
    childWatchMutex_.unlock();
}

bool ZkClient::getChildWatchData(const std::string& path, ChildWatchData& retChildWatchData)
{
    bool result = false;
    childWatchMutex_.lock();
    if (childWatchDatas_.find(path) != childWatchDatas_.end())
    {
        retChildWatchData = childWatchDatas_[path];
        result = true;
    }
    childWatchMutex_.unlock();
    return result;
}

void ZkClient::getNodeWatchPaths(std::vector<std::string>& data)
{
    nodeWatchMutex_.lock();
    std::map<std::string, NodeWatchData>::iterator iter = nodeWatchDatas_.begin();
    for (; iter != nodeWatchDatas_.end(); iter++)
    {
        data.push_back(iter->first);
    }
    nodeWatchMutex_.unlock();
}

void ZkClient::getChildWatchPaths(std::vector<std::string>& data)
{
    childWatchMutex_.lock();
    std::map<std::string, ChildWatchData>::iterator iter = childWatchDatas_.begin();
    for (; iter != childWatchDatas_.end(); iter++)
    {
        data.push_back(iter->first);
    }
    childWatchMutex_.unlock();
}


void ZkClient::defaultSessionExpiredHandler(const ZkClientPtr& client, void* context)
{
	exit(0);
};

ZkClient::ZkClient(uint32_t handle) : 
	stateMutex_(),
	stateCondition_(stateMutex_),
	zhandle_(NULL),
	expiredHandler_(NULL),
	userContext_(NULL),
	sessionState_(ZOO_CONNECTING_STATE),
    sessionDisconnectMs_(0),
    sessStateMutex_(),
    sessTimeoutMutex_(),
    sessDisconnMutex_(),
    nodeWatchMutex_(),
    childWatchMutex_(),
    isRetrying_(false),
    hasCallTimeoutFun_(false),
    host_(""),
    clientId_(NULL),
	isInitialized_(false)
{
	handle_ = handle;
	isSupportReconnect_ = true;    //默认支持重连
	retryDelay_ = ZkUtil::kInitRetryDelay;

    nodeWatchDatas_.clear();
    childWatchDatas_.clear();
};

bool ZkClient::init(const std::string& host, int timeout, SessionClientId *clientId /*= NULL*/,
                    ZkUtil::SessionExpiredHandler expired_handler /*= NULL*/, void* context /*= NULL*/)
{
	// 用户配置
	sessionTimeout_ = timeout;
	if (expired_handler) 
	{
		expiredHandler_ = expired_handler;
	}
	userContext_ = context;

    host_ = host;
    LOG_DEBUG << "session Handle:" << handle_ << ", host: "<< host_;
    if (clientId == NULL)
    {
        clientId_ = NULL;
    }
    else
    {
        clientId_ = new SessionClientId();
        clientId_->client_id = clientId->client_id;
        strncpy(clientId_->passwd, clientId->passwd, sizeof(clientId->passwd));
        LOG_DEBUG << "session Handle:" << handle_ << ", clientId.id: " << clientId_->client_id
                  << ", clientId.passwd:" << clientId_->passwd;
    }

	// zk初始化，除非参数有问题，否则总是可以立即返回
	//
	// 存在一个非常罕见的BUG场景：就是zookeeper_init返回赋值到zhandle_之前就完成了到
	// zookeeper的连接并回调了SessionWatcher，所以在SessionWatcher里一定要注意不要依赖
	// zhandle_，而是使用SessionWatcher被传入的zhandle参数。
	ZkZooInitCbData* cbfunData = new ZkZooInitCbData(handle_);
	zhandle_ = zookeeper_init(host_.c_str(), sessionWatcher, sessionTimeout_, reinterpret_cast<const clientid_t*>(clientId_), cbfunData, 0);
	if (!zhandle_) 
	{
		delete cbfunData;
		cbfunData = NULL;
        LOG_ERROR << "session Handle:" << handle_ << ", zookeeper_init failed! host: " << host_;
        return false;
	}

	/*
	 * 等待session初始化完成，两种可能返回值：
	 * 1，连接成功，会话建立.
	 * 2，会话过期，在初始化期间很难发生.
	 */
	stateMutex_.lock();
	while (getSessStat() != ZOO_CONNECTED_STATE && getSessStat() != ZOO_EXPIRED_SESSION_STATE)
	{
		stateCondition_.wait();
	}
	//int session_state = session_state_;
	stateMutex_.unlock();

	if (getSessStat() == ZOO_EXPIRED_SESSION_STATE)
	{ // 会话过期，fatal级错误
        LOG_ERROR << "session Handle:" << handle_ << ", session stat is session_expired! ";
		return false;
	}

	setIsInit(true);

	/*
	 * 会话建立，可以启动一个zk状态检测线程，主要是发现2种问题：
	 *	1，处于session_expire状态，那么回调SessionExpiredHandler，由用户终结程序（zkserver告知我们会话超时）。
	 *	2，处于非connected状态，那么判断该状态持续时间是否超过了session timeout时间，
	 *	超过则回调SessionExpiredHandler，由用户终结程序（client自己意识到会话超时）。
	 */
    isRetrying_ = false;
    hasCallTimeoutFun_ = false;
	//定时 每10ms 检查 是否会话超时
	double timeInterval = 0.01;
    ZkTimerManager::instance().runAfter(ZkClientManager::instance().getFirstThreadId(),
                                        timeInterval, boost::bind(&ZkClient::checkSessionState, handle_));
    return true;
};

void ZkClient::printClientInfo()
{
    LOG_WARN << "[ZkClient::printClientInfo] session Handle:" << handle_ << ", host: " << host_ << ", timeout: " << sessionTimeout_;
    if(clientId_ != NULL)
    {
        LOG_WARN << "[ZkClient::printClientInfo] clientId.id:" << clientId_->client_id << ", clientId.passwd:"
                << clientId_->passwd;
    }

    LOG_WARN << "[ZkClient::printClientInfo] isSupportReconn:" << isSupportReconnect_ << ", retryDelay: " << retryDelay_
            << ", isRetry:" << isRetrying_ << ", hasCallTimeoutFun:" << hasCallTimeoutFun_;

    LOG_WARN << "[ZkClient::printClientInfo] session_stat:" << getSessStatStr(getSessStat());
}

std::string ZkClient::getSessStatStr(int stat)
{
    if (stat == ZOO_EXPIRED_SESSION_STATE)
    {
        return "ZOO_EXPIRED_SESSION_STATE";
    }
    else if (stat == ZOO_AUTH_FAILED_STATE)
    {
        return "ZOO_AUTH_FAILED_STATE";
    }
    else if (stat == ZOO_CONNECTING_STATE)
    {
        return "ZOO_CONNECTING_STATE";
    }
    else if (stat == ZOO_ASSOCIATING_STATE)
    {
        return "ZOO_ASSOCIATING_STATE";
    }
    else if (stat == ZOO_CONNECTED_STATE)
    {
        return "ZOO_CONNECTED_STATE";
    }
    else
    {
        return "";
    }
}

bool ZkClient::reconnect()
{
	ZkZooInitCbData* cbfunData = new ZkZooInitCbData(handle_);
	zhandle_ = zookeeper_init(host_.c_str(), sessionWatcher, sessionTimeout_, (const clientid_t*)clientId_, cbfunData, 0);
	if (!zhandle_)
	{
		delete cbfunData;
		cbfunData = NULL;
		LOG_ERROR << "[ZkClient::reconnect] reconnnect failed, zookeeper_init failed. session Handle:" << handle_ ;
		printClientInfo();
		return false;
	}

	stateMutex_.lock();
	while (getSessStat() != ZOO_CONNECTED_STATE && getSessStat() != ZOO_EXPIRED_SESSION_STATE)
	{
		stateCondition_.wait();
	}
	stateMutex_.unlock();

	if (getSessStat() == ZOO_EXPIRED_SESSION_STATE)
	{
		LOG_ERROR << "[ZkClient::reconnect] reconnnect failed, session state is expire_state. session Handle:" << handle_;
		printClientInfo();
		return false;
	}
	return true;
}

void ZkClient::retry(uint32_t handle)
{
	ZkClientPtr client = ZkClientManager::instance().getZkClient(handle);
	if (!client)
	{
		return;  //如果找不到，说明此session已经销毁了，不用再定时check了.
	}

    if (client->reconnect() == false)
    {
        LOG_WARN << "[ZkClient::retry] reconnect failed. session Handle:" << handle << ", retryDelay: " << client->getRetryDelay();
        if (client->isSupportReconnect() == true)
        {
            //再重试
            ZkTimerManager::instance().runAfter(ZkClientManager::instance().getSecondThreadId(), client->getRetryDelay(), boost::bind(&ZkClient::retry, handle));
            client->setRetryDelay(std::min(client->getRetryDelay() * 2, ZkUtil::kMaxRetryDelay));
            client->setIsRetrying(true);
        }
    }
    else
    {
        LOG_WARN << "[ZkClient::retry] reconnnect succeed. session Handle:" << handle;
		client->setRetryDelay(ZkUtil::kInitRetryDelay);
		client->setIsRetrying(false);
		client->setHasCallTimeoutFun(false);

        //重建session成功后，重新 (过1秒后注册)注册所有的watcher  //这个重注册的流程 会由 zookeeper server 或zookeeper client c库自己做，不需要我们再做.
        //重新 注册所有watcher 耗的时间比较长，会在短时间内 阻塞 线程2，要不要考虑 分到另外的线程处理注册的事情？
//        ZkTimerManager::instance().runAfter(ZkClientManager::instance().getSecondThreadId(),
//                                            1, boost::bind(&ZkClient::regAllWatcher, handle));
    }
}

void ZkClient::checkSessionState(uint32_t handle) 
{
	ZkClientPtr client = ZkClientManager::instance().getZkClient(handle);
	if (!client)
	{
		return;  //如果找不到，说明此session已经销毁了，不用再定时check了.
	}

    bool session_expired = false;
    if (client->getSessStat() == ZOO_EXPIRED_SESSION_STATE)
    {
        session_expired = true;
    }
    else if (client->getSessStat() != ZOO_CONNECTED_STATE)
    {
        if (client->getCurrentMs() - client->getSessDisconn() > client->getSessTimeout())
        {
            LOG_WARN << "[ZkClient::CheckSessionState] sesssion disconnect expired! currMs:" << client->getCurrentMs()
				<< ", sessDisconn:" << client->getSessDisconn() << ", sessTimeout:" << client->getSessTimeout()
				<< ", session Handle:" << client->getHandle();
            session_expired = true;
        }
    }

    if (session_expired)
    {
        client->setSessStat(ZOO_EXPIRED_SESSION_STATE);

        // 会话过期，回调用户终结程序
		ZkUtil::SessionExpiredHandler& handler = client->getExpireHandler();
		if (client->hasCallTimeoutFun() == false && handler)
		{
            LOG_WARN << "[ZkClient::CheckSessionState] session expired, so call user handler."
                     << ", session Handle:" << client->getHandle();
            handler(client, client->getContext());  // 停止检测
			client->setHasCallTimeoutFun(true);
        }

        if (client->isRetrying() == false && client->isSupportReconnect() == true)
        {
            LOG_WARN << "[ZkClient::CheckSessionState] session expired, so retry create session. retryDelay: " << client->getRetryDelay()
                    << ", session Handle:" << client->getHandle();
            //重连zookeeper server
            ZkTimerManager::instance().runAfter(ZkClientManager::instance().getSecondThreadId(),
                                                client->getRetryDelay(), boost::bind(&ZkClient::retry, handle));
            client->setRetryDelay(std::min(client->getRetryDelay() * 2, ZkUtil::kMaxRetryDelay));
			client->setIsRetrying(true);
        }
    }

	//定时 每10ms 检查 是否会话超时
	double timeInterval = 0.01;
	ZkTimerManager::instance().runAfter(ZkClientManager::instance().getFirstThreadId(),
		timeInterval, boost::bind(&ZkClient::checkSessionState, handle));
};

void ZkClient::regAllWatcher(uint32_t handle)
{
	ZkClientPtr client = ZkClientManager::instance().getZkClient(handle);
	if (!client)
	{
		return;  //如果找不到，说明此session已经销毁了，不用再定时check了.
	}

    LOG_DEBUG << "register all watcher, session Handle:" << client->getHandle();
    std::vector<std::string> nodeWatchPaths;
    client->getNodeWatchPaths(nodeWatchPaths);

    std::vector<std::string> childWatchPaths;
    client->getChildWatchPaths(childWatchPaths);

    std::vector<std::string>::iterator iter = nodeWatchPaths.begin();
    for (; iter != nodeWatchPaths.end(); iter++)
    {
        client->autoRegNodeWatcher(*iter);
    }

    std::vector<std::string>::iterator iter_other = childWatchPaths.begin();
    for (; iter_other != childWatchPaths.end(); iter_other++)
    {
        client->autoRegChildWatcher(*iter_other);
    }
}

void ZkClient::sessionWatcher(zhandle_t *zh, int type, int state, const char *path, void *watcher_ctx)
{
	//assert(type == ZOO_SESSION_EVENT);

    if (type == ZOO_SESSION_EVENT)
	{

		ZkZooInitCbData* data = (ZkZooInitCbData* )watcher_ctx;
        assert(data != NULL);
		assert(data->handle_ != 0);

		ZkClientPtr zkclient = ZkClientManager::instance().__getZkClient(data->handle_);
		if (!zkclient)
		{
			delete data;
			data = NULL;
			return;  //如果找不到，说明此session已经销毁了.
		}

        LOG_WARN << "[SessionWatcher] session state:" << getSessStatStr(state)
                << ", session Handle:" << zkclient->getHandle();

        zkclient->getStateMutex().lock();
        zkclient->setSessStat(state);
        // 连接建立，记录协商后的会话过期时间，唤醒init函数（只有第一次有实际作用）
        if (state == ZOO_CONNECTED_STATE)
        {
            zkclient->setSessTimeout(zoo_recv_timeout(zh));
            LOG_WARN << "[SessionWatcher] set sessionTimeout:" << zkclient->getSessTimeout()
                    << ", session Handle:" << zkclient->getHandle();
            zkclient->getStateCondition().notify();
        }
        else if (state == ZOO_EXPIRED_SESSION_STATE)
        {
            // 会话过期，唤醒init函数
            zkclient->getStateCondition().notify();
        }
        else
        {// 连接异常，记录下异常开始时间，用于计算会话是否过期
            zkclient->setSessDisconn(zkclient->getCurrentMs());
            LOG_WARN << "[SessionWatcher] set sessionDisconnectMs:" << zkclient->getSessDisconn()
                    << ", session Handle:" << zkclient->getHandle();
        }
        zkclient->getStateMutex().unlock();
    }
};

int64_t ZkClient::getCurrentMs() 
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
};

ZkClient::~ZkClient()
{
    LOG_WARN << "distroy this zkclient, session Handle:" << handle_;
	std::cout << "[~ZkClient] distroy this zkclient, session Handle:" << handle_ << std::endl;
    if (clientId_)
    {
        delete clientId_;
        clientId_ = NULL;
    }

    isSupportReconnect_ = false;
    if (zhandle_)
    {
    	zookeeper_close(zhandle_);
    }

//    if (log_fp_)
//    {
//    	fclose(log_fp_);
//    }

    nodeWatchDatas_.clear();
    childWatchDatas_.clear();
};

bool ZkClient::getClientId(SessionClientId& cliId)
{
	if (isConnected() == true)
	{
		const SessionClientId* pClientId = reinterpret_cast<const SessionClientId*>(zoo_client_id(zhandle_));
		if (pClientId != NULL)
		{
			cliId.client_id = pClientId->client_id;
			strncpy(cliId.passwd, pClientId->passwd, sizeof(pClientId->passwd));
			return true;
		}
	}
	return false;
};

bool ZkClient::getNode(const std::string& path, ZkUtil::GetNodeHandler handler, void* context)
{
    if (handler == NULL)
    {
        return false;
    }
    if (isConnected() == false) { return  false;}

    ZkOperateAndWatchContext* watch_ctx = new ZkOperateAndWatchContext(path, context, shared_from_this());
    watch_ctx->getnode_handler_ = handler;

    int isWatch = 0;   //这里默认不触发zookeeper_init中注册的watch函数.
    int rc = zoo_aget(zhandle_, path.c_str(), isWatch, getNodeDataCompletion, watch_ctx);
    LOG_DEBUG << "[GetNode] zoo_aget  path:" << path << ", session Handle:" << handle_;
    return rc == ZOK ? true : false;
}

void ZkClient::getNodeDataCompletion(int rc, const char* value, int value_len,
                                     const struct Stat* stat, const void* data)
{
    assert(rc == ZOK || rc == ZCONNECTIONLOSS || rc == ZOPERATIONTIMEOUT ||
           rc == ZNOAUTH || rc == ZNONODE || rc == ZCLOSING);
    assert(data != NULL);

    const ZkOperateAndWatchContext* watch_ctx = (const ZkOperateAndWatchContext*)data;
    assert(watch_ctx->zkclient_);
    std::string strValue = "";

    LOG_DEBUG << "[ZkClient::GetNodeDataCompletion] rc: " << rc << ", getnode path:" << watch_ctx->path_
              << ", session Handle:" << watch_ctx->zkclient_->getHandle();
    if (rc == ZOK)
    {
        if (watch_ctx->getnode_handler_)
        {
            strValue.assign(value, value_len);
            watch_ctx->getnode_handler_(ZkUtil::kZKSucceed, watch_ctx->zkclient_, watch_ctx->path_,
                                       strValue, stat->version, watch_ctx->context_);
        }
    }
    else if (rc == ZNONODE)
    {
        if (watch_ctx->getnode_handler_)
        {
            watch_ctx->getnode_handler_(ZkUtil::kZKNotExist, watch_ctx->zkclient_, watch_ctx->path_,
                                       strValue, ZkUtil::kInvalidDataVersion, watch_ctx->context_);
        }
    }
    else
    {
        if (watch_ctx->getnode_handler_)
        {
            watch_ctx->getnode_handler_(ZkUtil::kZKError, watch_ctx->zkclient_, watch_ctx->path_,
                                       strValue, ZkUtil::kInvalidDataVersion, watch_ctx->context_);
        }
    }

    delete watch_ctx;
    watch_ctx = NULL;
}

bool ZkClient::getChildren(const std::string& path, ZkUtil::GetChildrenHandler handler, void* context)
{
    if (handler == NULL)
    {
        return false;
    }

    if (isConnected() == false) { return  false;}

    ZkOperateAndWatchContext* watch_ctx = new ZkOperateAndWatchContext(path, context, shared_from_this());
    watch_ctx->getchildren_handler_ = handler;

    int isWatch = 0;   //这里默认不触发zookeeper_init中注册的watch函数.
    int rc = zoo_aget_children(zhandle_, path.c_str(), isWatch, ZkClient::getChildrenStringCompletion, watch_ctx);
    LOG_DEBUG << "[GetChildren] zoo_aget_children path:" << path << ", session Handle:" << handle_;
    return rc == ZOK ? true : false;
}


void ZkClient::getChildrenStringCompletion(int rc, const struct String_vector* strings, const void* data)
{
    assert(rc == ZOK || rc == ZCONNECTIONLOSS || rc == ZOPERATIONTIMEOUT ||
           rc == ZNOAUTH || rc == ZNONODE || rc == ZCLOSING);
    assert(data != NULL);

    const ZkOperateAndWatchContext* watch_ctx = (const ZkOperateAndWatchContext*)data;
    assert(watch_ctx->zkclient_);

    LOG_DEBUG << "[ZkClient::GetChildrenStringCompleteion] rc:" << rc << ", getChildren path:" << watch_ctx->path_
              << ", session Handle:" << watch_ctx->zkclient_->getHandle();

    if (rc == ZOK)
    {
        if (watch_ctx->getchildren_handler_)
        {
            std::vector<std::string> childNodes(strings->data, strings->data + strings->count);
            watch_ctx->getchildren_handler_(ZkUtil::kZKSucceed, watch_ctx->zkclient_,
                                           watch_ctx->path_, childNodes, watch_ctx->context_);
        }
    }
    else if (rc == ZNONODE)
    {
        if (watch_ctx->getchildren_handler_)
        {
            std::vector<std::string> childNodes;
            watch_ctx->getchildren_handler_(ZkUtil::kZKNotExist, watch_ctx->zkclient_,
                                           watch_ctx->path_, childNodes, watch_ctx->context_);
        }
    }
    else
    {
        if (watch_ctx->getchildren_handler_)
        {
            std::vector<std::string> childNodes;
            watch_ctx->getchildren_handler_(ZkUtil::kZKError, watch_ctx->zkclient_,
                                           watch_ctx->path_, childNodes, watch_ctx->context_);
        }
    }
    delete watch_ctx;
    watch_ctx = NULL;
}

bool ZkClient::isExist(const std::string& path, ZkUtil::ExistHandler handler, void* context)
{
    if (handler == NULL)
    {
        return false;
    }
    if (isConnected() == false) { return  false;}

    ZkOperateAndWatchContext* watch_ctx = new ZkOperateAndWatchContext(path, context, shared_from_this());
    watch_ctx->exist_handler_ = handler;

    int isWatch = 0;   //这里默认不触发zookeeper_init中注册的watch函数.
    int rc = zoo_aexists(zhandle_, path.c_str(), isWatch, existCompletion, watch_ctx);
    LOG_DEBUG << "[IsExist] zoo_aexists path:" << path << ", session Handle:" << handle_;
    return rc == ZOK ? true : false;
}

void ZkClient::existCompletion(int rc, const struct Stat* stat, const void* data)
{
    assert(rc == ZOK || rc == ZCONNECTIONLOSS || rc == ZOPERATIONTIMEOUT ||
           rc == ZNOAUTH || rc == ZNONODE || rc == ZCLOSING);
    assert(data != NULL);

    const ZkOperateAndWatchContext* watch_ctx = (const ZkOperateAndWatchContext*)data;
    assert(watch_ctx->zkclient_);
    LOG_DEBUG << "[ZkClient::ExistCompletion] rc:" << rc << ", isExist path:" << watch_ctx->path_
              << ", session Handle:" << watch_ctx->zkclient_->getHandle();

    if (rc == ZOK || rc == ZNONODE)
    {
        if (watch_ctx->exist_handler_)
        {
            watch_ctx->exist_handler_(rc == ZOK ? ZkUtil::kZKSucceed : ZkUtil::kZKNotExist, watch_ctx->zkclient_,
                                     watch_ctx->path_, watch_ctx->context_);
        }
    }
    else
    {
        if (watch_ctx->exist_handler_)
        {
            watch_ctx->exist_handler_(ZkUtil::kZKError, watch_ctx->zkclient_,
                                     watch_ctx->path_, watch_ctx->context_);
        }
    }
    delete watch_ctx;
    watch_ctx = NULL;
}

bool ZkClient::createIfNeedCreateParents(const std::string& path, const std::string& value,
                    ZkUtil::CreateHandler handler, void* context, bool isTemp /*= false*/, bool isSequence /*= false*/)
{
    if (handler == NULL)
    {
        return false;
    }
    if (isConnected() == false) { return  false;}

    ContextInCreateParentAndNodes* watch_ctx = new ContextInCreateParentAndNodes(path,
                                               value, handler, context, isTemp, isSequence,shared_from_this());

    int flags = 0;
    if (isTemp == true)
    {
        flags |= ZOO_EPHEMERAL;
    }
    if (isSequence == true)
    {
        flags |= ZOO_SEQUENCE;
    }
    int rc = zoo_acreate(zhandle_, path.c_str(), value.c_str(), value.size(),
                         &ZOO_OPEN_ACL_UNSAFE, flags, createIfNeedCreateParentsCompletion, watch_ctx);
    LOG_DEBUG << "[CreateIfNeedCreateParents] path:" << path << ", value:" << value
              << ", isTemp:" << isTemp << ", isSeq:" << isSequence << ", session Handle:" << handle_;
    return rc == ZOK ? true : false;
}

void ZkClient::createIfNeedCreateParentsCompletion(int rc, const char* value, const void* data)
{
    assert(rc == ZOK || rc == ZNODEEXISTS || rc == ZCONNECTIONLOSS || rc == ZOPERATIONTIMEOUT ||
           rc == ZNOAUTH || rc == ZNONODE || rc == ZNOCHILDRENFOREPHEMERALS || rc == ZCLOSING);
    assert(data != NULL);

    const ContextInCreateParentAndNodes* watch_ctx = (const ContextInCreateParentAndNodes*)data;
    assert(watch_ctx->zkclient_);
    LOG_DEBUG << "[ZkClient::CreateIfNeedCreateParentsCompletion] rc:" << rc << ", create path:" << watch_ctx->path_
              << ", session Handle:" << watch_ctx->zkclient_->getHandle();

    if (rc == ZOK)
    {
        if (watch_ctx->create_handler_)
        {
            watch_ctx->create_handler_(ZkUtil::kZKSucceed, watch_ctx->zkclient_,
                                      watch_ctx->path_, value, watch_ctx->context_);
        }
        delete watch_ctx;
        watch_ctx = NULL;
    }
    else if (rc == ZNONODE)   //分支路径不存在
    {
        //先创建分支路径结点，再创建叶子结点
        //因为创建分支路径结点 需要一些时间，可能会阻塞当前线程，所以 转到其它线程来 执行这个操作
        CbFunManager::instance().runInThread(ZkClientManager::instance().getSecondThreadId(),
                                             boost::bind(&ZkClient::postCreateParentAndNode, watch_ctx->zkclient_, watch_ctx));
    }
    else if (rc == ZNODEEXISTS)
    {
        if (watch_ctx->create_handler_)
        {
            watch_ctx->create_handler_(ZkUtil::kZKExisted, watch_ctx->zkclient_,
                                      watch_ctx->path_, "", watch_ctx->context_);
        }
        delete watch_ctx;
        watch_ctx = NULL;
    }
    else
    {
        if (watch_ctx->create_handler_)
        {
            watch_ctx->create_handler_(ZkUtil::kZKError, watch_ctx->zkclient_,
                                      watch_ctx->path_, "", watch_ctx->context_);
        }
        delete watch_ctx;
        watch_ctx = NULL;
    }
}

void ZkClient::postCreateParentAndNode(const ContextInCreateParentAndNodes* watch_ctx)
{
    assert(watch_ctx != NULL);

    bool createDirSucc = true;
    string::size_type pos = watch_ctx->path_.rfind('/');
    if (pos == string::npos)
    {
        LOG_ERROR << "[ZkClient::postCreateParentAndNode] Can't find / character, create node failed! path:"
                    << watch_ctx->path_ << ", session Handle:" << handle_;

        createDirSucc = false;
        goto TAG_CREATE_DIR;
    }
    else
    {
        std::string parentDir = watch_ctx->path_.substr(0, pos);
        //同步 创建目录结点
        if (createPersistentDir(parentDir) == true)
        {
            //异步 创建叶子结点
            bool ret = create(watch_ctx->path_, watch_ctx->value_, watch_ctx->create_handler_, watch_ctx->context_,
                            watch_ctx->isTemp_, watch_ctx->isSequence_);
            if (ret == false)
            {
                LOG_ERROR << "[ZkClient::postCreateParentAndNode] create node failed! path:" << watch_ctx->path_
                            << ", isTemp_:" << watch_ctx->isTemp_ << ", isSeq:" << watch_ctx->isSequence_ << ", session Handle:" << handle_;

                createDirSucc = false;
                goto TAG_CREATE_DIR;
            }
        }
        else
        {
            LOG_ERROR << "[ZkClient::postCreateParentAndNode] create dir failed! dir:" << parentDir
                        << ", path:" << watch_ctx->path_ << ", session Handle:" << handle_;

            createDirSucc = false;
            goto TAG_CREATE_DIR;
        }
    }

TAG_CREATE_DIR:
    if (createDirSucc == false)
    {
        if (watch_ctx->create_handler_)
        {
            watch_ctx->create_handler_(ZkUtil::kZKError, watch_ctx->zkclient_,
                                       watch_ctx->path_, "", watch_ctx->context_);
        }
    }

    delete watch_ctx;
    watch_ctx = NULL;
}

bool ZkClient::create(const std::string& path, const std::string& value,
            ZkUtil::CreateHandler handler, void* context, bool isTemp /*= false*/, bool isSequence /*= false*/)
{
    if (handler == NULL)
    {
        return false;
    }
    if (isConnected() == false) { return  false;}

    ZkOperateAndWatchContext* watch_ctx = new ZkOperateAndWatchContext(path, context, shared_from_this());
    watch_ctx->create_handler_ = handler;

    int flags = 0;
    if (isTemp == true)
    {
        flags |= ZOO_EPHEMERAL;
    }
    if (isSequence == true)
    {
        flags |= ZOO_SEQUENCE;
    }
    int rc = zoo_acreate(zhandle_, path.c_str(), value.c_str(), value.size(),
                         &ZOO_OPEN_ACL_UNSAFE, flags, createCompletion, watch_ctx);
    LOG_DEBUG << "[ZkClient::Create] zoo_acreate path:" << path << ", value:" << value
              << ", isTemp:" << isTemp << ", isSeq:" << isSequence << ", session Handle:" << handle_;
    return rc == ZOK ? true : false;
}

void ZkClient::createCompletion(int rc, const char* value, const void* data)
{
    assert(rc == ZOK || rc == ZNODEEXISTS || rc == ZCONNECTIONLOSS || rc == ZOPERATIONTIMEOUT ||
           rc == ZNOAUTH || rc == ZNONODE || rc == ZNOCHILDRENFOREPHEMERALS || rc == ZCLOSING);
    assert(data != NULL);

    const ZkOperateAndWatchContext* watch_ctx = (const ZkOperateAndWatchContext*)data;
    assert(watch_ctx->zkclient_);

    LOG_DEBUG << "[ZkClient::CreateCompletion] rc:" << rc << ", create path:" << watch_ctx->path_
              << ", session Handle:" << watch_ctx->zkclient_->getHandle();

    if (rc == ZOK)
    {
        if (watch_ctx->create_handler_)
        {
            watch_ctx->create_handler_(ZkUtil::kZKSucceed, watch_ctx->zkclient_,
                                      watch_ctx->path_, value, watch_ctx->context_);
        }
    }
    else if (rc == ZNONODE)
    {
        if (watch_ctx->create_handler_)
        {
            //子路径不存在
            watch_ctx->create_handler_(ZkUtil::kZKNotExist, watch_ctx->zkclient_,
                                      watch_ctx->path_, "", watch_ctx->context_);
        }
    }
    else if (rc == ZNODEEXISTS)
    {
        if (watch_ctx->create_handler_)
        {
            watch_ctx->create_handler_(ZkUtil::kZKExisted, watch_ctx->zkclient_,
                                      watch_ctx->path_, "", watch_ctx->context_);
        }
    }
    else
    {
        if (watch_ctx->create_handler_)
        {
            watch_ctx->create_handler_(ZkUtil::kZKError, watch_ctx->zkclient_,
                                      watch_ctx->path_, "", watch_ctx->context_);
        }
    }
    delete watch_ctx;
    watch_ctx = NULL;
}

bool ZkClient::set(const std::string& path, const std::string& value, ZkUtil::SetHandler handler,
         void* context, int32_t version /*= -1*/)
{
    if (handler == NULL)
    {
        return false;
    }
    if (isConnected() == false) { return  false;}

    ZkOperateAndWatchContext* watch_ctx = new ZkOperateAndWatchContext(path, context, shared_from_this());
    watch_ctx->set_handler_ = handler;

    int rc = zoo_aset(zhandle_, path.c_str(), value.c_str(), value.size(), version, setCompletion, watch_ctx);
    LOG_DEBUG << "[ZkClient::Set] zoo_aset path:" << path << ", value: " << value << ", version:" << version
             << ", session Handle:" << handle_;
    return rc == ZOK ? true : false;
}

void ZkClient::setCompletion(int rc, const struct Stat* stat, const void* data)
{
    assert(rc == ZOK || rc == ZCONNECTIONLOSS || rc == ZOPERATIONTIMEOUT || rc == ZBADVERSION ||
           rc == ZNOAUTH || rc == ZNONODE || rc == ZCLOSING);

    const ZkOperateAndWatchContext* watch_ctx = (const ZkOperateAndWatchContext*)data;
    assert(watch_ctx->zkclient_);
    LOG_DEBUG << "[ZkClient::SetCompletion] rc:" << rc << ", set path:" << watch_ctx->path_
              << ", session Handle:" << watch_ctx->zkclient_->getHandle();

    if (rc == ZOK)
    {
        if (watch_ctx->set_handler_)
        {
            watch_ctx->set_handler_(ZkUtil::kZKSucceed, watch_ctx->zkclient_,
                                   watch_ctx->path_, stat->version, watch_ctx->context_);
        }
    }
    else if (rc == ZNONODE)
    {
        if (watch_ctx->set_handler_)
        {
            watch_ctx->set_handler_(ZkUtil::kZKNotExist, watch_ctx->zkclient_,
                                   watch_ctx->path_, ZkUtil::kInvalidDataVersion, watch_ctx->context_);
        }
    }
    else
    {
        if (watch_ctx->set_handler_)
        {
            watch_ctx->set_handler_(ZkUtil::kZKError, watch_ctx->zkclient_,
                                   watch_ctx->path_, ZkUtil::kInvalidDataVersion, watch_ctx->context_);
        }
    }
    delete watch_ctx;
    watch_ctx = NULL;
}

bool ZkClient::deleteRecursive(const std::string& path, ZkUtil::DeleteHandler handler, void* context, int32_t version /*= -1*/)
{
    if (handler == NULL)
    {
        return false;
    }
    if (isConnected() == false) { return  false;}

    ContextInDeleteRecursive* watch_ctx = new ContextInDeleteRecursive(path, handler, context, version, shared_from_this());
    int rc = zoo_adelete(zhandle_, path.c_str(), version, deleteRecursiveCompletion, watch_ctx);
    LOG_DEBUG << "[ZkClient::DeleteRecursive] zoo_adelete path:" << path << ", verson:" << version << ", session Handle:" << handle_;

    return rc == ZOK ? true : false;
}

void ZkClient::deleteRecursiveCompletion(int rc, const void* data)
{
    assert(rc == ZOK || rc == ZCONNECTIONLOSS || rc == ZOPERATIONTIMEOUT || rc == ZBADVERSION ||
           rc == ZNOAUTH || rc == ZNONODE || rc == ZNOTEMPTY || rc == ZCLOSING);

    const ContextInDeleteRecursive* watch_ctx = (const ContextInDeleteRecursive*)data;
    assert(watch_ctx->zkclient_);
    LOG_DEBUG << "[ZkClient::DeleteRecursiveCompletion] rc:" << rc << ", delete path:" << watch_ctx->path_
              << ", session Handle:" << watch_ctx->zkclient_->getHandle();

    if (rc == ZOK)
    {
        if (watch_ctx->delete_handler_)
        {
            watch_ctx->delete_handler_(ZkUtil::kZKSucceed, watch_ctx->zkclient_,
                                      watch_ctx->path_, watch_ctx->context_);
        }
        delete watch_ctx;
        watch_ctx = NULL;
    }
    else if (rc == ZNONODE)
    {
        if (watch_ctx->delete_handler_)
        {
            watch_ctx->delete_handler_(ZkUtil::kZKNotExist, watch_ctx->zkclient_,
                                      watch_ctx->path_, watch_ctx->context_);
        }
        delete watch_ctx;
        watch_ctx = NULL;
    }
    else if (rc == ZNOTEMPTY)   //含有叶子结点
    {
        //先删除子结点，再删除 分支结点
        //因为删除子结点 需要一些时间，可能会阻塞当前线程，所以 转到其它线程来 执行这个操作
        CbFunManager::instance().runInThread(ZkClientManager::instance().getSecondThreadId(),
                                             boost::bind(&ZkClient::postDeleteRecursive, watch_ctx->zkclient_, watch_ctx));
    }
    else
    {
        if (watch_ctx->delete_handler_)
        {
            watch_ctx->delete_handler_(ZkUtil::kZKError, watch_ctx->zkclient_,
                                      watch_ctx->path_, watch_ctx->context_);
        }
        delete watch_ctx;
        watch_ctx = NULL;
    }
}

void ZkClient::postDeleteRecursive(const ContextInDeleteRecursive* watch_ctx)
{
    assert(watch_ctx != NULL);
    assert(watch_ctx->delete_handler_ != NULL);

    bool deleteChildFailed = false;

    //获取child结点
    std::vector<std::string> childNodes;
    ZkUtil::ZkErrorCode ec = getChildren(watch_ctx->path_, childNodes);
    if (ec == ZkUtil::kZKNotExist)
    {
        watch_ctx->delete_handler_(ZkUtil::kZKSucceed, watch_ctx->zkclient_,
                                   watch_ctx->path_, watch_ctx->context_);
    }
    else if (ec != ZkUtil::kZKSucceed)
    {
        LOG_ERROR << "[ZkClient::postDeleteRecursive] GetChildren failed! ec:" << ec
                    << ", path:" << watch_ctx->path_ << ", session Handle:" << watch_ctx->zkclient_->getHandle();

        deleteChildFailed = true;
        goto TAG_DELETE_CHILD;
    }
    else  //ZkUtil::kZKSucceed
    {
        //同步 删除 child 结点
        std::vector<std::string>::iterator iter = childNodes.begin();
        for (; iter != childNodes.end(); iter++)
        {
            std::string childPath = watch_ctx->path_ + "/" + (*iter);
            //ZkUtil::ZkErrorCode ec1 = deleteRecursive(watch_ctx->path_, -1);   //删除子结点 用 最近的version
			ZkUtil::ZkErrorCode ec1 = deleteRecursive(childPath, -1);   //删除子结点 用 最近的version

            if (ec1 != ZkUtil::kZKSucceed &&
                ec1 != ZkUtil::kZKNotExist)
            {
                LOG_ERROR << "[ZkClient::postDeleteRecursive] GetChildren failed! ec:" << ec
                            << ", path:" << watch_ctx->path_ << ", session Handle:" << watch_ctx->zkclient_->getHandle();

                watch_ctx->delete_handler_(ZkUtil::kZKError, watch_ctx->zkclient_,
                                           watch_ctx->path_, watch_ctx->context_);

                deleteChildFailed = true;
                goto TAG_DELETE_CHILD;
            }
        }

        //异步 删除分支结点
        if (deleteNode(watch_ctx->path_, watch_ctx->delete_handler_, watch_ctx->context_,
                   watch_ctx->version_) == false)
        {
            LOG_ERROR << "[ZkClient::postDeleteRecursive] async delete failed! path:" << watch_ctx->path_
                        << ", version:" << watch_ctx->version_ << ", session Handle:" << watch_ctx->zkclient_->getHandle();

            deleteChildFailed = true;
            goto TAG_DELETE_CHILD;

        }
    }

TAG_DELETE_CHILD:
    if (deleteChildFailed == true)
    {
        if (watch_ctx->delete_handler_)
        {
            watch_ctx->delete_handler_(ZkUtil::kZKError, watch_ctx->zkclient_,
                                       watch_ctx->path_, watch_ctx->context_);
        }
    }

    delete watch_ctx;
    watch_ctx = NULL;
}

bool ZkClient::deleteNode(const std::string& path, ZkUtil::DeleteHandler handler, void* context, int32_t version /*= -1*/)
{
    if (handler == NULL)
    {
        return false;
    }
    if (isConnected() == false) { return  false;}

    ZkOperateAndWatchContext* watch_ctx = new ZkOperateAndWatchContext(path, context, shared_from_this());
    watch_ctx->delete_handler_ = handler;

    int rc = zoo_adelete(zhandle_, path.c_str(), version, deleteCompletion, watch_ctx);
    LOG_DEBUG << "[ZkClient::Delete] zoo_adelete path:" << path << ", version:" << version << ", session Handle:" << handle_;

    return rc == ZOK ? true : false;
}

void ZkClient::deleteCompletion(int rc, const void* data)
{
    assert(rc == ZOK || rc == ZCONNECTIONLOSS || rc == ZOPERATIONTIMEOUT || rc == ZBADVERSION ||
           rc == ZNOAUTH || rc == ZNONODE || rc == ZNOTEMPTY || rc == ZCLOSING);

    const ZkOperateAndWatchContext* watch_ctx = (const ZkOperateAndWatchContext*)data;
    assert(watch_ctx->zkclient_);
    LOG_DEBUG << "[ZkClient::DeleteCompletion] rc:" << rc << ", delete path:" << watch_ctx->path_
             << ", session Handle:" << watch_ctx->zkclient_->getHandle();

    if (rc == ZOK)
    {
        if (watch_ctx->delete_handler_)
        {
            watch_ctx->delete_handler_(ZkUtil::kZKSucceed, watch_ctx->zkclient_,
                                      watch_ctx->path_, watch_ctx->context_);
        }
    }
    else if (rc == ZNONODE)
    {
        if (watch_ctx->delete_handler_)
        {
            watch_ctx->delete_handler_(ZkUtil::kZKNotExist, watch_ctx->zkclient_,
                                      watch_ctx->path_, watch_ctx->context_);
        }
    }
    else if (rc == ZNOTEMPTY)
    {
        if (watch_ctx->delete_handler_)
        {
            watch_ctx->delete_handler_(ZkUtil::kZKNotEmpty, watch_ctx->zkclient_,
                                      watch_ctx->path_, watch_ctx->context_);
        }
    }
    else
    {
        if (watch_ctx->delete_handler_)
        {
            watch_ctx->delete_handler_(ZkUtil::kZKError, watch_ctx->zkclient_,
                                      watch_ctx->path_, watch_ctx->context_);
        }
    }
    delete watch_ctx;
    watch_ctx = NULL;
}

ZkUtil::ZkErrorCode ZkClient::getNode(const std::string& path, std::string& value, int32_t& version)
{
    if (isConnected() == false) { return  ZkUtil::kZKLostConnection;}

    int isWatch = 0;
    struct Stat stat;
    char buffer[ZkUtil::kMaxNodeValueLength] = {0};
    int buffer_len = sizeof(buffer);

    int rc = zoo_get(zhandle_, path.c_str(), isWatch, buffer, &buffer_len, &stat);
    LOG_DEBUG << "[ZkClient::GetNode] zoo_get path:" << path << ", version:" << version << ", rc:" << rc << ", session Handle:" << handle_;
    if (rc == ZOK)
    {
        if (buffer_len != -1)
        {
            value.assign(buffer, buffer_len);
        }
        else
        {
            value = "";
        }
        version = stat.version;
        return ZkUtil::kZKSucceed;
    }
    else if (rc == ZNONODE)
    {
        return ZkUtil::kZKNotExist;
    }
    else
    {
        return ZkUtil::kZKError;
    }
}

ZkUtil::ZkErrorCode ZkClient::getChildren(const std::string& path, std::vector<std::string>& childNodes)
{
    if (isConnected() == false) { return  ZkUtil::kZKLostConnection;}

    int isWatch = 0;
    struct String_vector strings = { 0, NULL };
    int rc = zoo_get_children(zhandle_, path.c_str(), isWatch, &strings);
    LOG_DEBUG << "[ZkClient::GetChildren] zoo_get_children path:" << path << ", rc:" << rc << ", session Handle:" << handle_;
    if (rc == ZOK)
    {
        for (int i = 0; i < strings.count; ++i)
        {
            childNodes.push_back(strings.data[i]);
        }
        deallocate_String_vector(&strings);
        return ZkUtil::kZKSucceed;
    }
    else if (rc == ZNONODE)
    {
        return ZkUtil::kZKNotExist;
    }
    return ZkUtil::kZKError;
}

ZkUtil::ZkErrorCode ZkClient::isExist(const std::string& path)
{
    if (isConnected() == false) { return  ZkUtil::kZKLostConnection;}

    int isWatch = 0;
    int rc = zoo_exists(zhandle_, path.c_str(), isWatch, NULL);
    LOG_DEBUG << "[ZkClient::IsExist] zoo_exists path:" << path << ", rc:" << rc << ", session Handle:" << handle_;
    if (rc == ZOK)
    {
        return ZkUtil::kZKSucceed;
    }
    else if (rc == ZNONODE)
    {
        return ZkUtil::kZKNotExist;
    }
    return ZkUtil::kZKError;
}

ZkUtil::ZkErrorCode ZkClient::create(const std::string& path, const std::string& value,
                           bool isTemp, bool isSequence, std::string& retPath)
{
    if (isConnected() == false) { return  ZkUtil::kZKLostConnection;}

    int flags = 0;
    if (isTemp == true)
    {
        flags |= ZOO_EPHEMERAL;
    }
    if (isSequence == true)
    {
        flags |= ZOO_SEQUENCE;
    }

    char buffer[ZkUtil::kMaxPathLength] = {0};
    int buffer_len = sizeof(buffer);
    int rc = zoo_create(zhandle_, path.c_str(), value.c_str(), value.size(), &ZOO_OPEN_ACL_UNSAFE,
                        flags, buffer, buffer_len);
    LOG_DEBUG << "[ZkClient::Create] zoo_create path:" << path << ", value:" << value
              << ", isTemp:" << isTemp << ", isSeq:" << isSequence
              << ", rc:" << rc << ", session Handle:" << handle_;
    if (rc == ZOK)
    {
        retPath.assign(buffer);
        return ZkUtil::kZKSucceed;
    }
    else if (rc == ZNONODE)
    {
        return ZkUtil::kZKNotExist;
    }
    else if (rc == ZNODEEXISTS)
    {
        return ZkUtil::kZKExisted;
    }
    return ZkUtil::kZKError;
}

ZkUtil::ZkErrorCode ZkClient::createIfNeedCreateParents(const std::string& path, const std::string& value,
                                              bool isTemp /*= false*/, bool isSequence /*= false*/, std::string& retPath)
{
    ZkUtil::ZkErrorCode ec = create(path, value, isTemp, isSequence, retPath);
    LOG_DEBUG << "ZkClient::CreateIfNeedCreateParents Create path:" << path << ", value:" << value << ", isTemp" << isTemp
            << ", isSeq:" <<isSequence << ", ec:" << ec << ", session Handle:" << handle_;
    if (ec == ZkUtil::kZKNotExist)  //分支结点不存在
    {
        string::size_type pos = path.rfind('/');
        if (pos == string::npos)
        {
            LOG_ERROR << "[ZkClient::CreateIfNeedCreateParents] Can't find / character, create node failed! path:"
                    << path << ", session Handle:" << handle_;
            return ZkUtil::kZKError;
        }
        else
        {
            std::string parentDir = path.substr(0, pos);
            //递归创建 所有 父目录结点
            if (createPersistentDir(parentDir) == true)
            {
                //创建叶子结点
                return create(path, value, isTemp, isSequence, retPath);
            }
            else
            {
                LOG_ERROR << "[ZkClient::CreateIfNeedCreateParents] create dir failed! dir:" << parentDir
                            << ", path:" << path << ", session Handle:" << handle_;
                return ZkUtil::kZKError;
            }
        }
    }
    else
    {
        return ec;
    }
}

ZkUtil::ZkErrorCode ZkClient::createPersistentDirNode(const std::string& path)
{
    if (isConnected() == false) { return  ZkUtil::kZKLostConnection;}

    int flags = 0;   //分支路径的结点 默认是 持久型、非顺序型
    int rc = zoo_create(zhandle_, path.c_str(), NULL, -1, &ZOO_OPEN_ACL_UNSAFE,
                        flags, NULL, 0);
    LOG_DEBUG << "[ZkClient::CreatePersistentDirNode] handle: "<< handle_ << "path:" << path << "rc:" << rc
              << ", session Handle:" << handle_;
    if (rc == ZOK)
    {
        return ZkUtil::kZKSucceed;
    }
    else if (rc == ZNONODE)
    {
        return ZkUtil::kZKNotExist;
    }
    else if (rc == ZNODEEXISTS)
    {
        return ZkUtil::kZKExisted;
    }
    return ZkUtil::kZKError;
}

//阻塞式 创建目录结点
bool ZkClient::createPersistentDir(const std::string& path)
{
    LOG_DEBUG << "[ZkClient::CreatePersistentDir] path:" << path  << ", session Handle:" << handle_;
    //先尝试创建 外层的 目录结点
    ZkUtil::ZkErrorCode ec = createPersistentDirNode(path);
    if (ec == ZkUtil::kZKSucceed || ec == ZkUtil::kZKExisted)
    {
        return true;
    }
    else if (ec == ZkUtil::kZKNotExist)  //如果失败，则先尝试 创建里层的 目录结点，然后创建 外层的目录结点
    {
        string::size_type pos = path.rfind('/');
        if (pos == string::npos)
        {
            LOG_ERROR << "[ZkClient::CreatePersistentDir] Can't find / character, create dir failed! path:"
                    << path  << ", session Handle:" << handle_;
            return false;
        }
        else
        {
            std::string parentDir = path.substr(0, pos);
            if (createPersistentDir(parentDir) == true)  //创建父目录成功
            {
                return createPersistentDir(path);
            }
            else
            {
                LOG_ERROR << "[ZkClient::CreatePersistentDir] create parent dir failed! dir:" << parentDir
                          << ", session Handle:" << handle_;
                return false;
            }
        }
    }
    else  //ZkUtil::kZKError
    {
        LOG_ERROR << "[ZkClient::CreatePersistentDir] CreatePersistentDirNode failed! path:" << path
                 << ", session Handle:" << handle_;
        return false;
    }
}

ZkUtil::ZkErrorCode ZkClient::set(const std::string& path, const std::string& value, int32_t version /*= -1*/)
{
    if (isConnected() == false) { return  ZkUtil::kZKLostConnection;}

    int rc = zoo_set(zhandle_, path.c_str(), value.c_str(), value.size(), version);
    LOG_DEBUG << "[ZkClient::Set] zoo_set path:" << path << ", value:" << value << ", version:" << version
              << ", rc:" << rc << ", session Handle:" << handle_;
    if (rc == ZOK)
    {
        return ZkUtil::kZKSucceed;
    }
    else if (rc == ZNONODE)
    {
        return ZkUtil::kZKNotExist;
    }
    return ZkUtil::kZKError;
}

ZkUtil::ZkErrorCode ZkClient::deleteNode(const std::string& path, int32_t version /*= -1*/)
{
    if (isConnected() == false) { return  ZkUtil::kZKLostConnection;}

    int rc = zoo_delete(zhandle_, path.c_str(), version);
    LOG_DEBUG << "[ZkClient::Delete] zoo_delete path:" << path << ", version:" << version << ", rc:" << rc
              << ", session Handle:" << handle_;
	if (rc == ZOK)
	{
		return ZkUtil::kZKSucceed;
	}
	else if (rc == ZNONODE)
	{
		return ZkUtil::kZKNotExist;
	}
    else if (rc == ZNOTEMPTY)
	{
		return ZkUtil::kZKNotEmpty;
    }
    return ZkUtil::kZKError;
}

/*
 * return:
 *      kZKSucceed: 删除成功
 *      kZKNotExist: 结点已不存在
 *      kZKError: 操作时出现错误
 */
ZkUtil::ZkErrorCode ZkClient::deleteRecursive(const std::string& path, int32_t version /*= -1*/)
{
    //获取child 结点
    std::vector<std::string> childNodes;
	childNodes.clear();
	ZkUtil::ZkErrorCode ec = getChildren(path, childNodes);
	if (ec == ZkUtil::kZKNotExist)
    {
        return ZkUtil::kZKSucceed;
    }
    else if (ec != ZkUtil::kZKSucceed)
    {
        LOG_ERROR << "[ZkClient::DeleteRecursive] GetChildren failed! ec:" << ec
                    << ", path:" << path << ", version:" << version << ", session Handle:" << handle_;
        return ZkUtil::kZKError;
    }
    else  //ZkUtil::kZKSucceed
    {
        //删除 child 结点
        std::vector<std::string>::iterator iter = childNodes.begin();
        for (; iter != childNodes.end(); iter++)
        {
            std::string childPath = path + "/" + (*iter);
            ZkUtil::ZkErrorCode ec1 = deleteRecursive(childPath, -1);   //删除子结点 用 最近的version

            if (ec1 != ZkUtil::kZKSucceed &&
                ec1 != ZkUtil::kZKNotExist)
            {
                LOG_ERROR << "[ZkClient::DeleteRecursive] GetChildren failed! ec:" << ec
                            << ", path:" << path << ", version:" << version << ", session Handle:" << handle_;
                return ZkUtil::kZKError;
            }
        }

        //删除分支结点
        return deleteNode(path, version);
    }
}

bool ZkClient::regNodeWatcher(const std::string& path, ZkUtil::NodeChangeHandler handler, void* context)
{
    if (isConnected() == false) { return  false;}
    if (handler == NULL) { return  false;};

    ZkOperateAndWatchContext* watch_ctx = new ZkOperateAndWatchContext(path, context, shared_from_this());
    watch_ctx->node_notify_handler_ = handler;

    int rc = zoo_wexists(zhandle_, path.c_str(), existWatcher, watch_ctx, NULL);
    LOG_DEBUG << "[ZkClient::regNodeWatcher] zoo_wexists path:" << path << ", rc:" << rc << ", session Handle:" << handle_;
    if (rc == ZOK || rc == ZNONODE)
    {
        //注册成功，则保存 watch数据
        NodeWatchData data;
        data.path_ = path;
        data.handler_ = handler;
        data.context_ = context;
        setNodeWatchData(path, data);

        LOG_DEBUG << "[ZkClient::regNodeWatcher] reg child watcher succeed, rc:" << rc << ", session Handle:" << handle_;
        return true;
    }
    else
    {
        LOG_ERROR << "[ZkClient::regNodeWatcher] reg child watcher failed, rc:" << rc << ", session Handle:" << handle_;
        return false;
    }
}

void ZkClient::existWatcher(zhandle_t* zh, int type, int state, const char* path, void* watcher_ctx)
{
    assert(type == ZOO_DELETED_EVENT || type == ZOO_CREATED_EVENT || type == ZOO_CHANGED_EVENT
           || type == ZOO_NOTWATCHING_EVENT || type == ZOO_SESSION_EVENT);

    ZkOperateAndWatchContext* context = (ZkOperateAndWatchContext*)watcher_ctx;
    assert(context->zkclient_);

	//再注册watch (用了阻塞的方式注册watch,为了避免阻塞线程，故转到另外的线程来注册)
    if (context->zkclient_->isShouldNotifyNodeWatch(context->path_) == true)
    {
        CbFunManager::instance().runInThread(ZkClientManager::instance().getSecondThreadId(),
            boost::bind(&ZkClient::autoRegNodeWatcher, context->zkclient_, context->path_));
    }

    LOG_DEBUG << "[ZkClient::ExistWatcher] type:" << type << ", regNodeWatcher path:" << context->path_
              << ", session Handle:" << context->zkclient_->getHandle();
    if (type == ZOO_SESSION_EVENT)
    { // 跳过会话事件,由zk handler的watcher进行处理
        return;
    }

    if (type == ZOO_NOTWATCHING_EVENT)
    {
        if (context->zkclient_->isShouldNotifyNodeWatch(context->path_) == true)
        {
            context->node_notify_handler_(ZkUtil::kTypeError, context->zkclient_,
                                         context->path_, "", ZkUtil::kInvalidDataVersion, context->context_);
        }
    }
    else if (type == ZOO_DELETED_EVENT)
    {
        if (context->zkclient_->isShouldNotifyNodeWatch(context->path_) == true)
        {
            context->node_notify_handler_(ZkUtil::kNodeDelete, context->zkclient_,
                                         context->path_, "", ZkUtil::kInvalidDataVersion, context->context_);
        }
    }
    else if (type == ZOO_CREATED_EVENT || type == ZOO_CHANGED_EVENT)
    {
        //节点创建或者元信息变动, 则向zookeeper获取节点最新的数据，再回调用户
        ZkUtil::ZkNotifyType eventType;
        if (type == ZOO_CREATED_EVENT)
        {
            eventType = ZkUtil::kNodeCreate;
        }
        else if (type == ZOO_CHANGED_EVENT)
        {
            eventType = ZkUtil::kNodeChange;
        }
        ContextInNodeWatcher* getDataContext = new ContextInNodeWatcher(
                context->path_, context->zkclient_, context->node_notify_handler_, eventType, context->context_);

        bool isWatch = 0;
        int rc = zoo_aget(zh, path, isWatch, getNodeDataOnWatcher, getDataContext);
        if (rc != ZOK)
        {
            LOG_ERROR << "[ZkClient::ExistWatcher] Get latest data failed! path:" << context->path_
                  << ", session Handle:" << context->zkclient_->getHandle();
            if (context->zkclient_->isShouldNotifyNodeWatch(context->path_) == true)
            {
                //如果获取数据失败，则回调用户，再注册watch
                context->node_notify_handler_(ZkUtil::kGetNodeValueFailed, context->zkclient_,
                                             context->path_, "", ZkUtil::kInvalidDataVersion, context->context_);
            }
        }
    }
    delete context;
    context = NULL;
}

void ZkClient::getNodeDataOnWatcher(int rc, const char* value, int value_len,
                                     const struct Stat* stat, const void* data)
{
    assert(rc == ZOK || rc == ZCONNECTIONLOSS || rc == ZOPERATIONTIMEOUT ||
           rc == ZNOAUTH || rc == ZNONODE || rc == ZCLOSING);
    assert(data != NULL);

    const ContextInNodeWatcher* watch_ctx = (const ContextInNodeWatcher*)data;
    assert(watch_ctx->zkclient_ != NULL);

    LOG_DEBUG << "[ZkClient::GetNodeDataOnWatcher] rc:" << rc << ", getNodeData path:" << watch_ctx->path_
              << ", session Handle:" << watch_ctx->zkclient_->getHandle();

    if (rc == ZOK)
    {
        NodeWatchData data;
        bool ret = watch_ctx->zkclient_->getNodeWatchData(watch_ctx->path_, data);
        if(ret == true)
        {
            //更新缓存
            data.value_.assign(value, value_len);
            data.version_ = stat->version;

            //回调用户函数
            watch_ctx->node_notify_handler_(watch_ctx->notifyType_, watch_ctx->zkclient_,
                                            watch_ctx->path_, data.value_, data.version_,
                                            watch_ctx->contextInOrignalWatcher_);
        }
        else
        {
            LOG_ERROR << "[ZkClient::GetNodeDataOnWatcher] Can't find this watch data. path: "
                        << watch_ctx->path_ << ", session Handle:" << watch_ctx->zkclient_->getHandle();
        }
    }
    else if (rc == ZNONODE)  //如果获取数据失败，则回调用户，再注册watch
    {
        LOG_ERROR << "[ZkClient::GetNodeDataOnWatcher] Get latest data failed! Don't have this znode. path: "
                    << watch_ctx->path_ << ", session Handle:" << watch_ctx->zkclient_->getHandle();

        if (watch_ctx->zkclient_->isShouldNotifyNodeWatch(watch_ctx->path_) == true)
        {
            watch_ctx->node_notify_handler_(ZkUtil::kGetNodeValueFailed_NodeNotExist, watch_ctx->zkclient_,
                                            watch_ctx->path_, "", ZkUtil::kInvalidDataVersion, watch_ctx->contextInOrignalWatcher_);
        }
    }
    else  //如果获取数据失败，则回调用户，再注册watch
    {
        LOG_ERROR << "[ZkClient::GetNodeDataOnWatcher] Get latest data failed! path: "
                    << watch_ctx->path_ << ", rc:" << rc << ", session Handle:" << watch_ctx->zkclient_->getHandle();

        if (watch_ctx->zkclient_->isShouldNotifyNodeWatch(watch_ctx->path_) == true)
        {
            watch_ctx->node_notify_handler_(ZkUtil::kGetNodeValueFailed, watch_ctx->zkclient_,
                                            watch_ctx->path_, "", ZkUtil::kInvalidDataVersion, watch_ctx->contextInOrignalWatcher_);
        }
    }

    delete watch_ctx;
    watch_ctx = NULL;
}

void ZkClient::autoRegNodeWatcher(std::string path)
{
    if (isConnected() == false) { return;}

    LOG_DEBUG << "[ZkClient::autoRegNodeWatcher] path: " << path << ", session Handle:" << handle_;
    NodeWatchData data;
    bool ret = getNodeWatchData(path, data);
    if(ret == false || data.isSupportAutoReg_ == false)
    {
        LOG_WARN << "[ZkClient::autoRegNodeWatcher] get watch data failed or not support auto register watcher! path:"
                << path << ", session Handle:" << handle_;
        return;
    }

    bool regRet = regNodeWatcher(data.path_, data.handler_, data.context_);
    if (regRet == false)
    {
        LOG_ERROR << "[ZkClient::autoRegNodeWatcher] regChildWatcher failed, so reg node watch again after 5 minutes. path:"
                << path << ", session Handle:" << handle_;

        //如果注册失败，则过5分钟之后再注册
        double timeAfter = 5 * 60;
        ZkTimerManager::instance().runAfter(ZkClientManager::instance().getSecondThreadId(),
                                            timeAfter, boost::bind(&ZkClient::autoRegNodeWatcher, shared_from_this(), path));
    }
}


bool ZkClient::regChildWatcher(const std::string& path, ZkUtil::ChildChangeHandler handler, void* context)
{
    if (isConnected() == false) { return  false;}
    if (handler == NULL) { return  false;};

    ZkOperateAndWatchContext* watch_ctx = new ZkOperateAndWatchContext(path, context, shared_from_this());
    watch_ctx->child_notify_handler_ = handler;

    struct String_vector strings = { 0, NULL };
    int rc = zoo_wget_children(zhandle_, path.c_str(), getChildrenWatcher, watch_ctx, &strings);
    LOG_DEBUG << "[ZkClient::regChildWatcher] zoo_wget_children path:" << path << ", rc:" << rc << ", session Handle:" << handle_;
    if (rc == ZOK)
    {
        LOG_DEBUG << "[ZkClient::regChildWatcher] reg child watcher succeed.";
        deallocate_String_vector(&strings);
        //注册成功，则保存 watch数据
        ChildWatchData data;
        data.path_ = path;
        data.handler_ = handler;
        data.context_ = context;
        setChildWatchData(path, data);
        return true;
    }
    else if (rc == ZNONODE)
    {
        LOG_ERROR << "[ZkClient::regChildWatcher] reg child watcher failed, znode not existed." << ", session Handle:" << handle_;
        return false;
    }
    else
    {
        LOG_ERROR << "[ZkClient::regChildWatcher] reg child watcher failed, rc: " << rc << ", session Handle:" << handle_;
        return false;
    }
}

void ZkClient::getChildrenWatcher(zhandle_t* zh, int type, int state, const char* path,void* watcher_ctx)
{
    assert(type == ZOO_DELETED_EVENT || type == ZOO_CHILD_EVENT
           || type == ZOO_NOTWATCHING_EVENT || type == ZOO_SESSION_EVENT);

    ZkOperateAndWatchContext* context = (ZkOperateAndWatchContext*)watcher_ctx;
    assert(context->zkclient_);

	//再注册watch (用了阻塞的方式注册watch,为了避免阻塞线程，故转到另外的线程来注册)
    if (context->zkclient_->isShouldNotifyChildWatch(context->path_) == true)
    {
        CbFunManager::instance().runInThread(ZkClientManager::instance().getSecondThreadId(),
            boost::bind(&ZkClient::autoRegChildWatcher, context->zkclient_, context->path_));
    }

    LOG_DEBUG << "[ZkClient::GetChildrenWatcher] type:" << type << ", path:" << context->path_
              << ", session Handle:" << context->zkclient_->getHandle();
    if (type == ZOO_SESSION_EVENT)
    { // 跳过会话事件,由zk handler的watcher进行处理
        return;
    }

    if (type == ZOO_NOTWATCHING_EVENT)
    {
        if (context->zkclient_->isShouldNotifyChildWatch(context->path_) == true)
        {
            std::vector<std::string> childNodes;
            context->child_notify_handler_(ZkUtil::kTypeError, context->zkclient_,
                                          context->path_, childNodes, context->context_);
        }
    }
    else if (type == ZOO_DELETED_EVENT)
    {
        //应该不会走到这里，因为zoo_get_children注册的watcher，不会返回这个type.
        LOG_DEBUG << "[ZkClient::GetChildrenWatcher] ZOO_DELETED_EVENT. path:" << context->path_
                  << ", session Handle:" << context->zkclient_->getHandle();
    }
    else if (type == ZOO_CHILD_EVENT)
    {
        //节点创建或者元信息变动, 则向zookeeper获取 最新的子节点列表，再回调用户
        ContextInChildWatcher* getDataContext = new ContextInChildWatcher(
                context->path_, context->zkclient_, context->child_notify_handler_, ZkUtil::kChildChange, context->context_);

        int isWatch = 0;   //这里默认不触发zookeeper_init中注册的watch函数.
        int rc = zoo_aget_children(zh, path, isWatch, getChildDataOnWatcher, getDataContext);
        if (rc != ZOK)
        {
            LOG_ERROR << "[ZkClient::GetChildrenWatcher] Get latest child data failed! path:" << context->path_
                      << ", session Handle:" << context->zkclient_->getHandle();
            //如果获取数据失败，则回调用户，再注册watch
            if (context->zkclient_->isShouldNotifyChildWatch(context->path_) == true)
            {
                std::vector<std::string> childNodes;
                context->child_notify_handler_(ZkUtil::kGetChildListFailed, context->zkclient_,
                                              context->path_, childNodes, context->context_);
            }
        }
    }
    delete context;
    context = NULL;
}

void ZkClient::getChildDataOnWatcher(int rc, const struct String_vector* strings, const void* data)
{
    assert(rc == ZOK || rc == ZCONNECTIONLOSS || rc == ZOPERATIONTIMEOUT ||
           rc == ZNOAUTH || rc == ZNONODE || rc == ZCLOSING);
    assert(data != NULL);

    const ContextInChildWatcher* watch_ctx = (const ContextInChildWatcher*)data;
    assert(watch_ctx->zkclient_ != NULL);
    LOG_DEBUG << "[ZkClient::GetChildDataOnWatcher] rc:" << rc << ", getChildList path:" << watch_ctx->path_
              << ", session Handle:" << watch_ctx->zkclient_->getHandle();

    if (rc == ZOK)
    {
        ChildWatchData data;
        bool ret = watch_ctx->zkclient_->getChildWatchData(watch_ctx->path_, data);
        if(ret == true)
        {
            //更新缓存数据
            data.childList_.clear();
            data.childList_.assign(strings->data, strings->data + strings->count);
            //回调用户函数
            watch_ctx->child_notify_handler(watch_ctx->notifyType_, watch_ctx->zkclient_,
                                            watch_ctx->path_, data.childList_,
                                            watch_ctx->contextInOrignalWatcher_);
        }
        else
        {
            LOG_ERROR << "[ZkClient::GetChildDataOnWatcher] Can't find this watch data. path: "
                    << watch_ctx->path_ << ", session Handle:" << watch_ctx->zkclient_->getHandle();
        }
    }
    else if (rc == ZNONODE)
    {
        LOG_ERROR << "[ZkClient::GetChildDataOnWatcher] Get latest child list failed! Don't have this znode. path: "
                    << watch_ctx->path_ << ", session Handle:" << watch_ctx->zkclient_->getHandle();

        if (watch_ctx->zkclient_->isShouldNotifyChildWatch(watch_ctx->path_) == true)
        {
            std::vector<std::string> childNodes;
            watch_ctx->child_notify_handler(ZkUtil::kGetChildListFailed_ParentNotExist, watch_ctx->zkclient_,
                                            watch_ctx->path_, childNodes, watch_ctx->contextInOrignalWatcher_);
        }
    }
    else
    {
        LOG_ERROR << "[ZkClient::GetChildDataOnWatcher] Get latest child list failed! path: "
                    << watch_ctx->path_ << ", rc:" << rc << ", session Handle:" << watch_ctx->zkclient_->getHandle();

        if (watch_ctx->zkclient_->isShouldNotifyChildWatch(watch_ctx->path_) == true)
        {
            std::vector<std::string> childNodes;
            watch_ctx->child_notify_handler(ZkUtil::kGetChildListFailed, watch_ctx->zkclient_,
                                            watch_ctx->path_, childNodes, watch_ctx->contextInOrignalWatcher_);
        }
    }

    delete watch_ctx;
    watch_ctx = NULL;
}

void ZkClient::autoRegChildWatcher(std::string path)
{
    if (isConnected() == false) { return;}

    LOG_DEBUG << "[ZkClient::autoRegChildWatcher] path: " << path << ", session Handle:" << handle_;
    ChildWatchData data;
    bool ret = getChildWatchData(path, data);
    if(ret == false || data.isSupportAutoReg_ == false)
    {
        LOG_ERROR << "[ZkClient::autoRegChildWatcher] get watch data failed or not support auto register watcher! path:"
            << path << ", session Handle:" << handle_;
        return;
    }

    bool regRet = regChildWatcher(data.path_, data.handler_, data.context_);
    if (regRet == false)
    {
        LOG_ERROR << "[ZkClient::autoRegChildWatcher] regChildWatcher failed, so reg child watch again after 5 minutes. path:"
            << path << ", session Handle:" << handle_;

        //如果注册失败，则过5分钟之后再注册
        double timeAfter = 5 * 60;
        ZkTimerManager::instance().runAfter(ZkClientManager::instance().getSecondThreadId(),
                                            timeAfter, boost::bind(&ZkClient::autoRegChildWatcher, shared_from_this(), path));
    }
}

void ZkClient::cancelRegNodeWatcher(const std::string& path)
{
    nodeWatchMutex_.lock();
    //在收到watcher回调、或者 定时注册 watcher时，如果从 map中找不到数据，就不会回调用户函数了，且不会再注册 watcher了。
    nodeWatchDatas_.erase(path);
    nodeWatchMutex_.unlock();
};

void ZkClient::cancelRegChildWatcher(const std::string& path)
{
    childWatchMutex_.lock();
    //在收到watcher回调、或者 定时注册 watcher时，如果从 map中找不到数据，就不会回调用户函数了，且不会再注册 watcher了。
    childWatchDatas_.erase(path);
    childWatchMutex_.unlock();
};





namespace ZkUtil
{
	__thread char t_errnobuf[512];
	__thread char t_time[32];
	__thread time_t t_lastSecond;

	const char* strerror_tl(int savedErrno)
	{
		return strerror_r(savedErrno, t_errnobuf, sizeof t_errnobuf);
	}

	int setNonBlock(int fd, bool value) 
	{
		int flags = fcntl(fd, F_GETFL, 0);
		if (flags < 0) 
		{
			return errno;
		}

		if (value) 
		{
			return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
		}
		return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
	}

	bool isReadEvent(int events) {return events & EPOLLIN;};
	bool isWriteEvent(int events) {return events & EPOLLOUT;};

	int getSocketError(int sockfd)
	{
		if (sockfd == 0)
			return 0;

		int optval;
		socklen_t optlen = static_cast<socklen_t>(sizeof optval);

		if (::getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
		{
			return errno;
		}
		else
		{
			return optval;
		}
	}

	void modifyEpollEvent(int operation, int epollfd, ZkNetClient* pClient, std::string printStr)
	{
		if (pClient == NULL || pClient->getChannel() == NULL)
		{
			return;
		}
		struct epoll_event ev;
		memset(&ev, 0, sizeof(ev));
		ev.events = pClient->getChannel()->events_;
		ev.data.ptr = pClient;

		LOG_DEBUG << printStr <<" fd: " << pClient->getChannel()->fd_ << " events: (read:" << isReadEvent(ev.events) << ", write:" << isWriteEvent(ev.events) << ") in epollfd: " 
			<< epollfd << " in zkHandle: " << pClient->getNetName();
		int r = epoll_ctl(epollfd, operation, pClient->getChannel()->fd_, &ev);
		if (r < 0)
		{
			LOG_DEBUG << "epoll_ctl operator(oper:" << operation << ", " << printStr<< ") failed! " << ",errorNo:" << errno << ", errDesc:" << strerror(errno)
				<< ", fd: " << pClient->getChannel()->fd_ << " events: (read:" << isReadEvent(ev.events) << ", write:" << isWriteEvent(ev.events) << "in epollfd: " 
				<< epollfd << " in zkHandle: " << pClient->getNetName();
		}
	}

	void addEpollFd(int epollfd, ZkNetClient* pClient) 
	{
		modifyEpollEvent(EPOLL_CTL_ADD, epollfd, pClient, "ADD ");
	}

	void modEpollFd(int epollfd, ZkNetClient* pClient) 
	{
		modifyEpollEvent(EPOLL_CTL_MOD, epollfd, pClient, "MOD ");
	}

	void delEpollFd(int epollfd, ZkNetClient* pClient) 
	{
		modifyEpollEvent(EPOLL_CTL_DEL, epollfd, pClient, "DEL ");
	}

	void enableReading(ZkNetClient* pClient) 
	{ 
		if (pClient == NULL || pClient->getChannel() == NULL)
		{
			return;
		}
		pClient->getChannel()->events_ |= kReadEvent;  
	}

	void enableWriting(ZkNetClient* pClient) 
	{ 
		if (pClient == NULL || pClient->getChannel() == NULL)
		{
			return;
		}
		pClient->getChannel()->events_ |= kWriteEvent;
	}

	void disableWriting(ZkNetClient* pClient) 
	{ 
		if (pClient == NULL || pClient->getChannel() == NULL)
		{
			return;
		}
		pClient->getChannel()->events_ &= ~kWriteEvent; 
	}

	void disableAll(ZkNetClient* pClient) 
	{ 
		if (pClient == NULL || pClient->getChannel() == NULL)
		{
			return;
		}
		pClient->getChannel()->events_ = kNoneEvent;
	}

	int createEventfd()
	{
		int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
		if (evtfd < 0)
		{
			LOG_SYSERR << "Failed in eventfd";
			abort();
		}
		return evtfd;
	}
};

}


