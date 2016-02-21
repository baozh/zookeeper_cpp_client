/*
 * ZkClient.h
 *
 *      Created on: 2016年2月20日
 *      Author: ZengHui Bao (bao_z_h@163.com)
 */

#ifndef __ZK_CLIENT_H
#define __ZK_CLIENT_H


#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <string>
#include <poll.h>
#include <sys/epoll.h>
#include <map>
#include <vector>
#include <algorithm>
#include "muduo/base/Condition.h"
#include "muduo/base/Mutex.h"
#include "muduo/base/Logging.h"
#include "muduo/net/SocketsOps.h"
#include "ZkTimerQueue.h"
#include "zookeeper.h"
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/noncopyable.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/shared_ptr.hpp>



using namespace muduo::net;
using namespace muduo;

namespace ZkCppClient
{

class ZkNetClient;
class ZkClient;
typedef boost::shared_ptr<ZkClient> ZkClientPtr;


namespace ZkUtil
{
    //common
	const int kThisEpollTimeMs = 10000;
	const int kNoneEvent = 0;
	const int kReadEvent = POLLIN | POLLPRI;
	const int kWriteEvent = POLLOUT;

	const int kMaxRetryDelay = 10*60;   //单位: 秒
	const int kInitRetryDelay = 5;      //单位: 秒

	const char* strerror_tl(int savedErrno);

	int setNonBlock(int fd, bool value);

	bool isReadEvent(int events);
	bool isWriteEvent(int events);

	void modifyEpollEvent(int operation, int epollfd, ZkNetClient* pClient, std::string printStr);

	void addEpollFd(int epollfd, ZkNetClient* pClient);

	void modEpollFd(int epollfd, ZkNetClient* pClient);

	void delEpollFd(int epollfd, ZkNetClient* pClient);

    void enableReading(ZkNetClient* pClient);
    void enableWriting(ZkNetClient* pClient);
    void disableWriting(ZkNetClient* pClient);
    void disableAll(ZkNetClient* pClient);

    int getSocketError(int sockfd);

    int createEventfd();


    //zookeeper client related
    const int32_t kInvalidDataVersion = -1;
    const int kMaxNodeValueLength = 32 * 1024;
    const int kMaxPathLength = 512;

    typedef boost::function<void (const ZkClientPtr& client, void* context)> SessionExpiredHandler;

    //操作的回调原型///////////////////////////////////////////////////////////////////////////////////////////
    enum ZkErrorCode
    {
        kZKSucceed= 0, // 操作成功,或者 结点存在
        kZKNotExist,  // 节点不存在, 或者 分支结点不存在
        kZKError,     // 请求失败
        kZKDeleted,   // 节点删除
        kZKExisted,   // 节点已存在
        kZKNotEmpty,   // 节点含有子节点
        kZKLostConnection   //与zookeeper server断开连接
    };
    /* errcode 返回：
        kZKSucceed: 获取成功, value 结点的值，version 结点的版本号(之后可根据这个值，在delete,set时，进行CAS操作)
        kZKNotExist: 结点不存在, value 为空串，version 为kInvalidDataVersion
        kZKError: 其它错误, value 为空串，version 为kInvalidDataVersion
    */
    typedef boost::function<void (ZkUtil::ZkErrorCode errcode, const ZkClientPtr& client, const std::string& path,
                                  const std::string& value, int32_t version, void* context)> GetNodeHandler;

    /* errcode 返回：
        kZKSucceed: 获取成功, childNode返回 所有子结点的 结点名，path 返回 分支路径
        kZKNotExist: 结点不存在, childNode 为空
        kZKError: 其它错误, childNode 为空
    */
    typedef boost::function<void (ZkUtil::ZkErrorCode errcode, const ZkClientPtr& client, const std::string& path,
                                  const std::vector<std::string>& childNodes, void* context)> GetChildrenHandler;

    /* errcode 返回：
        kZKSucceed: 结点存在
        kZKNotExist: 结点不存在
        kZKError: 其它错误
    */
    typedef boost::function<void (ZkUtil::ZkErrorCode errcode, const ZkClientPtr& client,
                                  const std::string& path, void* context)> ExistHandler;

    /* errcode 返回：
        kZKSucceed: 创建成功, value 结点的值
        kZKNotExist: 子路径不存在（创建失败 value 空串），需要先创建子路径，再创建结点
        kZKExisted: 结点已存在（创建失败 value 空串）
        kZKError: 其它错误（创建失败 value 空串）
		注：这里的 path 并不是 创建后的结点名，而是 指定创建的结点名。
			如果创建的是 顺序型 节点，则返回的路径 path 与 真实创建的结点名 会有不同(这时getChildren来获取它真实的结点名)，
			其它情况 返回的路径 path 与 真实创建的结点名 都相同.
    */
    typedef boost::function<void (ZkUtil::ZkErrorCode errcode, const ZkClientPtr& client, const std::string& path,
                                  const std::string& value, void* context)> CreateHandler;
    /* errcode 返回：
        kZKSucceed: set成功, version 所设置修改结点的版本号
        kZKNotExist: 结点不存在, version 为kInvalidDataVersion
        kZKError: 其它错误, version 为kInvalidDataVersion
    */
    typedef boost::function<void (ZkUtil::ZkErrorCode errcode, const ZkClientPtr& client, const std::string& path,
                                  int32_t version, void* context)> SetHandler;

    /* errcode 返回：
        kZKSucceed: 删除成功
        kZKNotExist: 要删除的节点 不存在
        kZKNotEmpty: 要删除的节点 含有 子节点，需要先删除子节点.
        kZKError: 其它错误
    */
    typedef boost::function<void (ZkUtil::ZkErrorCode errcode, const ZkClientPtr& client,
                                  const std::string& path, void* context)> DeleteHandler;

    //Watcher的回调原型///////////////////////////////////////////////////////////////////////////////////////////
    enum ZkNotifyType
    {
        kNodeDelete = 0,    // 节点删除
        kNodeCreate,    // 节点创建
        kNodeChange,    // 节点的数据变更
        kGetNodeValueFailed_NodeNotExist, //节点创建 或 数据变更时，再向zookeeper server获取最新数据时 结点已被删除
        kGetNodeValueFailed,  //节点创建 或 数据变更时，再向zookeeper server获取最新数据时 失败
        kChildChange,    // 子节点的变更（增加、删除子节点）
        kGetChildListFailed_ParentNotExist, //子节点的变更时，再向zookeeper server获取最新子节点列表时 父结点已被删除
        kGetChildListFailed,  //子节点的变更时，再向zookeeper server获取最新子节点列表时 失败
        kTypeError, //其它错误
    };
    /* type 返回：
        kNodeDelete = 0,    // path: 注册监听的路径 value: 空串 version: kInvalidDataVersion
        kNodeCreate,    // path: 注册监听的路径 value: 结点最新的值 version: 结点最新的版本号
        kNodeChange,    // path: 注册监听的路径 value: 结点最新的值 version: 结点最新的版本号
        kGetNodeValue_NodeNotExist, //path: 注册监听的路径 value: 空串 version: kInvalidDataVersion
        kGetNodeValueFailed,  //path: 注册监听的路径 value: 空串 version: kInvalidDataVersion
        kTypeError, // path 注册监听的路径 value 空串 version kInvalidDataVersion
    */
    typedef boost::function<void (ZkUtil::ZkNotifyType type, const ZkClientPtr& client,
                                  const std::string& path, const std::string& value,
                                  int32_t version, void* context)> NodeChangeHandler;
    /* type 返回：
        kChildChange,    // path: 注册监听的路径  childNodes: 最新的子结点列表(注:不是完整的路径，而是子结点的结点名)
        kGetChildListFailed_ParentNotExist， // path: 注册监听的路径  childNodes: 空集合
        kGetChildListFailed,  //path: 注册监听的路径  childNodes: 空集合
        kTypeError, //path: 注册监听的路径  childNodes: 空集合
    */
    typedef boost::function<void (ZkUtil::ZkNotifyType type, const ZkClientPtr& client,
                                  const std::string& path, const std::vector<std::string>& childNodes,
                                  void* context)> ChildChangeHandler;
};





//所有回调（操作、Watcher）都用这一个数据结构作为上下文
//通常是从 Zookeeper收到回调（context* 用这个数据结构）后，从ZKWatchContext中取出要用到的参数，再 回调 用户定义的函数
struct ZkOperateAndWatchContext
{
	ZkOperateAndWatchContext(const std::string& path, void* context, ZkClientPtr zkclient);

	void* context_;
	std::string path_;
	ZkClientPtr zkclient_;

	ZkUtil::GetNodeHandler getnode_handler_;
	ZkUtil::GetChildrenHandler getchildren_handler_;
	ZkUtil::ExistHandler exist_handler_;
	ZkUtil::CreateHandler create_handler_;
	ZkUtil::SetHandler set_handler_;
	ZkUtil::DeleteHandler delete_handler_;
	ZkUtil::NodeChangeHandler node_notify_handler_;
	ZkUtil::ChildChangeHandler child_notify_handler_;
};

struct ContextInNodeWatcher
{
    ContextInNodeWatcher(const std::string&path,  ZkClientPtr zkclient, ZkUtil::NodeChangeHandler handler,
                         ZkUtil::ZkNotifyType type, void* context)
    {
        path_ = path;
        zkclient_ = zkclient;
        node_notify_handler_ = handler;
        notifyType_ = type;
        contextInOrignalWatcher_ = context;
    }

    std::string path_;
    ZkClientPtr zkclient_;
    ZkUtil::NodeChangeHandler node_notify_handler_;
    ZkUtil::ZkNotifyType notifyType_;
    void* contextInOrignalWatcher_;
};

struct ContextInChildWatcher
{
    ContextInChildWatcher(const std::string&path, ZkClientPtr zkclient, ZkUtil::ChildChangeHandler handler,
                         ZkUtil::ZkNotifyType type, void* context)
    {
        path_ = path;
        zkclient_ = zkclient;
        child_notify_handler = handler;
        notifyType_ = type;
        contextInOrignalWatcher_ = context;
    }

    std::string path_;
    ZkClientPtr zkclient_;
    ZkUtil::ChildChangeHandler child_notify_handler;
    ZkUtil::ZkNotifyType notifyType_;
    void* contextInOrignalWatcher_;
};

struct ContextInCreateParentAndNodes
{
    ContextInCreateParentAndNodes(const std::string& path, const std::string& value,
                                  ZkUtil::CreateHandler handler,void* context,
                                  bool isTemp, bool isSeq, ZkClientPtr zkclient)
    {
        path_ = path;
        value_ = value;
        create_handler_ = handler;
        context_ = context;
        isTemp_ = isTemp;
        isSequence_ = isSeq;
        zkclient_ = zkclient;
    }

    std::string path_;
    std::string value_;
    ZkUtil::CreateHandler create_handler_;
    void* context_;
    bool isTemp_;
    bool isSequence_;
    ZkClientPtr zkclient_;
};

struct ContextInDeleteRecursive
{
    ContextInDeleteRecursive(const std::string& path, ZkUtil::DeleteHandler handler, void* context,
                             int32_t version, ZkClientPtr zkclient)
    {
        path_ = path;
        delete_handler_ = handler;
        context_ = context;
        version_ = version;
        zkclient_ = zkclient;
    }

    std::string path_;
    ZkUtil::DeleteHandler delete_handler_;
    void* context_;
    int32_t version_;
    ZkClientPtr zkclient_;
};

struct ZkZooInitCbData
{
	ZkZooInitCbData(uint32_t handle)
	{
		handle_ = handle;
	}
	uint32_t handle_;
};






struct SessionClientId
{
    int64_t client_id;
    char passwd[16];

    SessionClientId()
    {
        memset(this, 0, sizeof(SessionClientId));
    }
};


struct ZkConnChannel : boost::noncopyable
{
public:
	ZkConnChannel(int epollfd, int fd, int events)
	{
		memset(this, 0, sizeof *this);
		fd_ = fd;
		events_ = events;
		epollfd_ = epollfd;
	}

	void update(ZkNetClient *client) {ZkUtil::modEpollFd(epollfd_, client);};

	~ZkConnChannel() 
	{
		LOG_DEBUG << "[~ZkConnChannel] deleting fd:" << fd_;
		close(fd_);
	}

public:
	int fd_;
	int epollfd_;
	int events_;
};


class ZkNetClient  : boost::noncopyable
{
public:
	friend class ZkClientManager;
	friend class ZkTimerQueue;
	typedef boost::function<void()> ReadTimerCallback;

public:
	ZkNetClient(int epollfd, int threadId, int eventfd, std::string netName)
	{
#define  ZKCLIENT_LOOP_INDEX_INIT    0xFFFFFFFFFFFFFFFE
#define  FUNCTION_LOOP_INDEX_INIT    0xFFFFFFFFFFFFFFFF

		pConnChannel_ = new ZkConnChannel(epollfd, eventfd, ZkUtil::kNoneEvent);
		epollfd_ = epollfd;
		threadId_ = threadId;
		loopIndex_ = ZKCLIENT_LOOP_INDEX_INIT;
		loopIndexFunResetChannel_ = FUNCTION_LOOP_INDEX_INIT;
		loopIndexFunRetry_ = FUNCTION_LOOP_INDEX_INIT;
		netName_ = netName;
	}

	~ZkNetClient()
	{
		if (pConnChannel_)
		{
			delete pConnChannel_;
			pConnChannel_ = NULL;
		}
	}

	std::string getNetName() {return netName_;};
	ZkConnChannel* getChannel() {return pConnChannel_;};

	void handleRead();
	void handleEventFdRead(int eventfd);
	void handleTimerFdRead();
	void handleWrite();
	void setReadTimerCb(ReadTimerCallback cb);

private:
	ZkConnChannel *pConnChannel_;
	ReadTimerCallback timerReadCb_;

	std::string netName_;
	int threadId_;   //一个线程（thread_id）可以有多个sslClient，但一个sslclient只能属于一个线程
	int epollfd_;    //一个线程使用一个epollfd

	volatile uint64_t loopIndex_;
	volatile uint64_t loopIndexFunResetChannel_;
	volatile uint64_t loopIndexFunRetry_;
};



//管理一个session的状态，及提供各个操作接口的实现
//设置回调的时候要注意：线程是在原生Zookeeper C库的线程中运行的，需线程安全。
class ZkClient : boost::noncopyable,
				 public boost::enable_shared_from_this<ZkClient>
{
public:
        struct NodeWatchData
        {
                NodeWatchData();
                NodeWatchData(const NodeWatchData& data);
                NodeWatchData& operator= (const NodeWatchData& data);

                std::string path_;
                ZkUtil::NodeChangeHandler handler_;
                void *context_;
                std::string value_;
                int32_t version_;
                bool isSupportAutoReg_;   //当watcher触发后，是否支持自动再注册watcher
        };

        struct ChildWatchData
        {
                ChildWatchData();
                ChildWatchData(const ChildWatchData& data);
                ChildWatchData& operator= (const ChildWatchData& data);

                std::string path_;
                ZkUtil::ChildChangeHandler handler_;
                void *context_;
                std::vector<std::string> childList_;
                bool isSupportAutoReg_;   //当watcher触发后，是否支持自动再注册watcher
        };

public:
        friend class ZkClientManager;

        ZkClient(uint32_t handle);
        bool init(const std::string& host, int timeout, SessionClientId *clientId = NULL,
              ZkUtil::SessionExpiredHandler expired_handler = NULL, void* context = NULL);

        ~ZkClient();

        muduo::MutexLock& getStateMutex() {return stateMutex_;};
        muduo::Condition& getStateCondition() {return stateCondition_;};

        int getSessStat();
        void setSessStat(int stat);

        int getSessTimeout();
        void setSessTimeout(int time);

        int64_t getSessDisconn();
        void setSessDisconn(int64_t disconn);

        void setNodeWatchData(const std::string& path, const NodeWatchData& data);
        bool getNodeWatchData(const std::string& path, NodeWatchData& retNodeWatchData);
        bool isShouldNotifyNodeWatch(const std::string& path);
        void getNodeWatchPaths(std::vector<std::string>& data);

        void getChildWatchPaths(std::vector<std::string>& data);
        void setChildWatchData(const std::string& path, const ChildWatchData& data);
        bool getChildWatchData(const std::string& path, ChildWatchData& retChildWatchData);
        bool isShouldNotifyChildWatch(const std::string& path);

		int getRetryDelay() {return retryDelay_;};
		void setRetryDelay(int delay) {retryDelay_ = delay;};

		bool isRetrying() {return isRetrying_;};
		void setIsRetrying(bool retrying) {isRetrying_ = retrying;};

		bool hasCallTimeoutFun() {return hasCallTimeoutFun_;};
		void setHasCallTimeoutFun(bool isCall) {hasCallTimeoutFun_ = isCall;};

		ZkUtil::SessionExpiredHandler& getExpireHandler() {return expiredHandler_;};
		void* getContext() {return userContext_;};

		void autoRegNodeWatcher(std::string path);
		void autoRegChildWatcher(std::string path);

		bool isInit() {return isInitialized_;};
		void setIsInit(bool isInited){isInitialized_ = isInited;};

public:
        //对外服务接口
        uint32_t getHandle() {return handle_;};

        bool isSupportReconnect() {return isSupportReconnect_;};
        void setIsSupportReconnect(bool isReconn) {isSupportReconnect_ = isReconn;};

        bool isConnected() {return getSessStat() == ZOO_CONNECTED_STATE;};

        bool getClientId(SessionClientId& cliId);

        /* async operation api */
        // handle需 不为空.
        // 返回false，操作失败；返回true，有可能成功（要根据回调handler返回的rc参数确定是否成功）.
        bool getNode(const std::string& path, ZkUtil::GetNodeHandler handler, void* context);
        bool getChildren(const std::string& path, ZkUtil::GetChildrenHandler handler, void* context);
		//存在: kZKSucceed, 不存在: kZKNotExist 其它错误：kZKError
        bool isExist(const std::string& path, ZkUtil::ExistHandler handler, void* context);

        //创建结点的类型（默认持久型非顺序型，isTemp 临时型，isSequence 顺序型）
        //访问权限acl: 默认是都可访问
        bool create(const std::string& path, const std::string& value,
                ZkUtil::CreateHandler handler, void* context, bool isTemp = false, bool isSequence = false);
        bool createIfNeedCreateParents(const std::string& path, const std::string& value,
                ZkUtil::CreateHandler handler, void* context, bool isTemp = false, bool isSequence = false);

        //如果设置version，对指定版本的结点set操作 会是CAS操作；否则，默认是设置结点的最新版本的值(version: -1)
        bool set(const std::string& path, const std::string& value, ZkUtil::SetHandler handler,
             void* context, int32_t version = -1);

        bool deleteNode(const std::string& path, ZkUtil::DeleteHandler handler, void* context, int32_t version = -1);
        bool deleteRecursive(const std::string& path, ZkUtil::DeleteHandler handler, void* context, int32_t version = -1);

        /* sync operation api */
        ZkUtil::ZkErrorCode getNode(const std::string& path, std::string& value, int32_t& version);
        ZkUtil::ZkErrorCode getChildren(const std::string& path, std::vector<std::string>& childNodes);

		//存在: kZKSucceed, 不存在: kZKNotExist 其它错误：kZKError
        ZkUtil::ZkErrorCode isExist(const std::string& path);
        //如果创建的是 顺序型 节点，则返回的路径retPath 与 原路径path 会有不同，其它情况 retPath 与 path都相同.
        ZkUtil::ZkErrorCode create(const std::string& path, const std::string& value,
                               bool isTemp /*= false*/, bool isSequence /*= false*/, std::string& retPath);
        //创建时，如果路径的分支结点不存在，则会先创建分支结点，再创建叶子结点。（注：分支结点必须是 持久型的）
        ZkUtil::ZkErrorCode createIfNeedCreateParents(const std::string& path, const std::string& value,
                               bool isTemp /*= false*/, bool isSequence /*= false*/, std::string& retPath);
        ZkUtil::ZkErrorCode set(const std::string& path, const std::string& value, int32_t version = -1);
        ZkUtil::ZkErrorCode deleteNode(const std::string& path, int32_t version = -1);
        ZkUtil::ZkErrorCode deleteRecursive(const std::string& path, int32_t version = -1);

        /* register watcher */
        //默认用阻塞式api, 且触发watcher后，会自动再注册watcher.
		//注册三种事件(节点删除，节点创建，节点数据变更)的watcher.
		//注：当 path结点 不存在时，也可以注册成功.
        bool regNodeWatcher(const std::string& path, ZkUtil::NodeChangeHandler handler, void* context);
		//注册 子节点的变更（增加、删除子节点）事件的watcher.
		//注：当 path结点 不存在时，会注册失败，所以注册前，需先创建 path 结点.
        bool regChildWatcher(const std::string& path, ZkUtil::ChildChangeHandler handler, void* context);

        //取消 对path的watcher.
        void cancelRegNodeWatcher(const std::string& path);
        void cancelRegChildWatcher(const std::string& path);

private:
        void setHandle(uint32_t handle) {handle_ = handle;};

        static void defaultSessionExpiredHandler(const ZkClientPtr& client, void* context);
        static void sessionWatcher(zhandle_t *zh, int type, int state, const char *path, void *watcher_ctx);
        int64_t getCurrentMs();

        static void checkSessionState(uint32_t handle);

        //操作回调
        static void getNodeDataCompletion(int rc, const char* value, int value_len,
                                         const struct Stat* stat, const void* data);
        static void getChildrenStringCompletion(int rc, const struct String_vector* strings, const void* data);
        static void existCompletion(int rc, const struct Stat* stat, const void* data);
        static void createCompletion(int rc, const char* value, const void* data);
        static void setCompletion(int rc, const struct Stat* stat, const void* data);
        static void deleteCompletion(int rc, const void* data);
        static void existWatcher(zhandle_t* zh, int type, int state, const char* path, void* watcher_ctx);
        static void getNodeDataOnWatcher(int rc, const char* value, int value_len,
                                               const struct Stat* stat, const void* data);
        static void getChildrenWatcher(zhandle_t* zh, int type, int state,
                                                    const char* path,void* watcher_ctx);
        static void getChildDataOnWatcher(int rc, const struct String_vector* strings, const void* data);
        static void createIfNeedCreateParentsCompletion(int rc, const char* value, const void* data);
        static void deleteRecursiveCompletion(int rc, const void* data);

        bool reconnect();
		static void retry(uint32_t handle);
        static std::string getSessStatStr(int stat);
        void printClientInfo();
        ZkUtil::ZkErrorCode createPersistentDirNode(const std::string& path);
        bool createPersistentDir(const std::string& path);
        void postCreateParentAndNode(const ContextInCreateParentAndNodes* watch_ctx);
        void postDeleteRecursive(const ContextInDeleteRecursive* watch_ctx);
        static void regAllWatcher(uint32_t handle);

private:
        uint32_t handle_;    //在clientManager中的handle

		volatile bool isInitialized_;
        volatile bool isSupportReconnect_;   //是否支持重连
		volatile int retryDelay_;
		volatile bool isRetrying_;
		volatile bool hasCallTimeoutFun_;

        std::string host_;
        SessionClientId *clientId_;
        zhandle_t* zhandle_;
        //	FILE* log_fp_;
		ZkUtil::SessionExpiredHandler expiredHandler_;
		void* userContext_;

        // ZK会话状态
        volatile int sessionState_;
        muduo::MutexLock stateMutex_;  //配合stateCondition_
        muduo::Condition stateCondition_;
        muduo::MutexLock sessStateMutex_;   //保护对 session_state_的互斥访问

        volatile int sessionTimeout_;    //session保持的超时时间
        muduo::MutexLock sessTimeoutMutex_;   //保护对 session_timeout_的互斥访问

        volatile int64_t sessionDisconnectMs_;   //session发生异常的时间点
        muduo::MutexLock sessDisconnMutex_;   //保护对 session_disconnect_ms_的互斥访问

        std::map<std::string, NodeWatchData> nodeWatchDatas_;   //map<path, watchdata>
        muduo::MutexLock nodeWatchMutex_;
        std::map<std::string, ChildWatchData> childWatchDatas_; //map<path, watchdata>
        muduo::MutexLock childWatchMutex_;
};

}

#endif
