/*
 * distribute_sharedlock.cc
 *
 *      Description: 利用zookeeper实现 分布式共享锁
 *      Created on: 2016年2月21日
 *      Author: ZengHui Bao (bao_z_h@163.com)
 */


#include "ZkClient.h"
#include "ZkClientManager.h"
#include <stdio.h>
#include <string>
#include<stdlib.h>
#include <vector>
#include <assert.h>
#include <algorithm>
#include <unistd.h>
#include <queue>
#include "muduo/base/Condition.h"
#include "muduo/base/Mutex.h"
#include <boost/noncopyable.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>

using namespace std;
using namespace ZkCppClient;

#define     ZOOKEEPER_SERVER_CONN_STRING        "127.0.0.1:2181,127.0.0.1:2182,127.0.0.1:2183"


bool isReadTypeLock(const std::string& nodeName)
{
    if (nodeName.find("-R-") != std::string::npos)
    {
        return true;
    }
    else
    {
        return false;
    }
}

uint64_t getSeqNo(const std::string& nodeName);

void printChild(const std::string& path, const std::vector<std::string>& childnodes)
{
    printf("\n\n-----------[printChild] ParentPath: %s, child size:%d-------------- \n", path.c_str(), childnodes.size());
    std::vector<std::string>::const_iterator iter = childnodes.begin();
    for(;iter != childnodes.end(); iter++)
    {
        printf("child name:%s, getSeqNo:%d\n", (*iter).c_str(), getSeqNo(*iter));
    }
    printf("-------------------------------------------------------------------- \n");
}

uint64_t getSeqNo(const std::string& nodeName)
{
    if (nodeName.find("-R-") != std::string::npos)
    {
        std::string seqStr = nodeName.substr(nodeName.find("-R-") + 3);
        return strtouq(seqStr.c_str(), NULL, 10);
    }
    else if (nodeName.find("-W-") != std::string::npos)
    {
        std::string seqStr = nodeName.substr(nodeName.find("-W-") + 3);
        return strtouq(seqStr.c_str(), NULL, 10);
    }
    return 0;
}

bool dataLess(const std::string& node1, const std::string& node2)
{
    return getSeqNo(node1) < getSeqNo(node2);
}























class DistriSharedLock : boost::noncopyable
{
public:
    enum RwLockerMode
    {
        RW_LOCKER_NONE,
        RW_LOCKER_WR,
        RW_LOCKER_RD,
    };

public:
    //对外服务接口
    DistriSharedLock() :
        mutexRLock_(),
        conditionRLock_(mutexRLock_),
        mutexWLock_(),
        conditionWLock_(mutexWLock_)
    {
        zkConnStr_ = "";
        isInit_ = false;
        childNodeName_ = "";
        leastNodePathInRLock_ = "";
        leastNodePathInWLock_ = "";
        curLockMode_ = RW_LOCKER_NONE;
        writerChildPath_ = "";
    };

    bool init(const std::string& zkConnStr, const std::string& childNodeName)
    {
        if (isInit_ == false)
        {
            childNodeName_ = childNodeName;
            zkConnStr_ = zkConnStr;
            //设置zookeeper日志路径
            if (ZkClientManager::setLogConf(true, "./zk_log") == false)
            {
                printf("setLogConf failed!\n\n");
                return false;
            }

            //创建一个session
            uint32_t handle = ZkClientManager::instance().createZkClient(zkConnStr_, 30000, NULL, NULL, NULL);
            if (handle == 0)
            {
                printf("create session failed! connStr:%s\n", zkConnStr_.c_str());
                return false;
            }

            //通过session handle，获取ZkClient
            zkClient_ = ZkClientManager::instance().getZkClient(handle);

            //创建 父路径
            bool isTemp = false;
            bool isSeq = false;
            std::string retPath;
            ZkUtil::ZkErrorCode ec = zkClient_->create(parentPath_, "", isTemp, isSeq, retPath);
            if (ec != ZkUtil::kZKSucceed && ec != ZkUtil::kZKExisted)
            {
                printf("\n create parent path:%s failed! \n", parentPath_.c_str());
                return false;
            }

            isInit_ = true;
        }

        return true;
    }

    //返回false，说明发生错误
    bool rlock()
    {
        if (isInit_ == false)
            return false;

        //创建子结点
        std::string retPath;
        ZkUtil::ZkErrorCode ec = createChild(true, retPath);
        if (ec != ZkUtil::kZKSucceed)  //创建过程中，出现错误
        {
            printf("create childPath failed! errCode:%d\n", ec);
            return false;
        }

        while(1)
        {
            //获取　所有子结点
            std::vector<std::string> childNodes;
            if (zkClient_->getChildren(parentPath_, childNodes) != ZkUtil::kZKSucceed)
            {
                printf("\n getChildren failed! path:%s \n", parentPath_.c_str());
                return false;
            }

            //对 子结点列表　按序号排序 (从小到大)
            std::sort(childNodes.begin(), childNodes.end(), dataLess);
            printChild(parentPath_, childNodes);

            std::vector<std::string>::iterator iter = childNodes.begin();
            bool isLeastNode = true;
            std::string leastNodeName;  //比 retPath 小的　最后一个(最大的那个) 写类型 结点
            for (; iter != childNodes.end(); iter++)
            {
                if (isReadTypeLock(*iter) == false &&
                    getSeqNo(*iter) < getSeqNo(retPath))
                {
                    isLeastNode = false;
                    leastNodeName = (*iter);
                }
            }

            if (isLeastNode)   //抢到锁了
            {
                readerChildPaths_.push(retPath);
                curLockMode_ = RW_LOCKER_RD;
                return true;
            }
            else
            {
                //注册 watcher
                leastNodePathInRLock_ = parentPath_ + "/" + leastNodeName;
                if (zkClient_->regNodeWatcher(leastNodePathInRLock_,
                    boost::bind(&DistriSharedLock::regWatcherRLock_cb, this, _1, _2, _3, _4, _5, _6), NULL) == false)
                {
                    printf("\n regWatcher failed! path:%s\n", leastNodePathInRLock_.c_str());
                    return false;
                }

                //没竞争到锁, 等待　leastNodePathInRLock_　结点删除
                printf("[rlock] wait nodename:%s\n", leastNodePathInRLock_.c_str());
                mutexRLock_.lock();
                conditionRLock_.wait();
                mutexRLock_.unlock();
            }
        }
        return true;
    }

    bool wlock()
    {
        if (isInit_ == false)
            return false;

        //创建子结点
        std::string retPath;
        ZkUtil::ZkErrorCode ec = createChild(false, retPath);
        if (ec != ZkUtil::kZKSucceed)  //创建过程中，出现错误
        {
            printf("create childPath failed! errCode:%d\n", ec);
            return false;
        }

        while(1)
        {
            //获取　所有子结点
            std::vector<std::string> childNodes;
            if (zkClient_->getChildren(parentPath_, childNodes) != ZkUtil::kZKSucceed)
            {
                printf("\n getChildren failed! path:%s \n", parentPath_.c_str());
                return false;
            }

            //对 子结点列表　按序号排序 (从小到大)
            std::sort(childNodes.begin(), childNodes.end(), dataLess);
            printChild(parentPath_, childNodes);

            std::vector<std::string>::iterator iter = childNodes.begin();
            bool isLeastNode = true;
            std::string leastNodeName;  //比 retPath 小的　最后一个(最大的那个) 结点
            for (; iter != childNodes.end(); iter++)
            {
                if (getSeqNo(*iter) < getSeqNo(retPath))
                {
                    isLeastNode = false;
                    leastNodeName = (*iter);
                }
            }

            if (isLeastNode)   //抢到锁了
            {
                writerChildPath_ = retPath;
                curLockMode_ = RW_LOCKER_WR;
                return true;
            }
            else
            {
                //注册 watcher
                leastNodePathInWLock_ = parentPath_ + "/" + leastNodeName;
                if (zkClient_->regNodeWatcher(leastNodePathInWLock_,
                    boost::bind(&DistriSharedLock::regWatcherWLock_cb, this, _1, _2, _3, _4, _5, _6), NULL) == false)
                {
                    printf("\n regWatcher failed! path:%s\n", leastNodePathInWLock_.c_str());
                    return false;
                }

                printf("[wlock] wait nodename:%s\n", leastNodePathInWLock_.c_str());
                //没竞争到锁, 等待　leastNodePathInWLock_　结点删除
                mutexWLock_.lock();
                conditionWLock_.wait();
                mutexWLock_.unlock();
            }
        }
        return true;
    }

    //返回false，说明发生错误
    bool unlock()
    {
        if (isInit_ == false)
            return false;

        std::string childPath = "";
        if (curLockMode_ == RW_LOCKER_RD)
        {
            assert(readerChildPaths_.empty() == false);

            childPath = readerChildPaths_.front();
            readerChildPaths_.pop();
            if (readerChildPaths_.empty() == true)
            {
                curLockMode_ = RW_LOCKER_NONE;
            }
        }
        else if (curLockMode_ == RW_LOCKER_WR)
        {
            assert(writerChildPath_ != "");

            childPath = writerChildPath_;
            writerChildPath_ = "";
            curLockMode_ = RW_LOCKER_NONE;
        }

        //删除子结点
        if (childPath != "")
        {
            printf("[unlock] release childPath:%s\n", childPath.c_str());
            ZkUtil::ZkErrorCode ec = zkClient_->deleteRecursive(childPath);
            if (ec == ZkUtil::kZKError)
            {
                 return false;
            }
        }
        return true;
    }

public:
    ~DistriSharedLock()
    {
        //删除所有　子结点
        std::string childPath = "";
        if (curLockMode_ == RW_LOCKER_RD)
        {
            while(readerChildPaths_.empty() != true)
            {
                childPath = readerChildPaths_.front();
                readerChildPaths_.pop();
                ZkUtil::ZkErrorCode ec = zkClient_->deleteRecursive(childPath);
            }
        }
        else if (curLockMode_ == RW_LOCKER_WR)
        {
            assert(writerChildPath_ != "");
            ZkUtil::ZkErrorCode ec = zkClient_->deleteRecursive(writerChildPath_);
            writerChildPath_ = "";
        }
        curLockMode_ = RW_LOCKER_NONE;

        //释放zookeeper handle
        ZkClientManager::instance().destroyClient(zkClient_->getHandle());
        zkClient_.reset();
        isInit_ = false;
        zkConnStr_ = "";
        leastNodePathInRLock_ = "";
        leastNodePathInWLock_ = "";
    };

private:

    ZkUtil::ZkErrorCode createChild(bool isRead, std::string& retPath)
    {
        //创建 子路径
        bool isTemp = true;  //临时结点
        bool isSeq = true;  //顺序结点
        std::string childPath;
        if (isRead)
        {
            childPath = parentPath_ + "/" + childNodeName_ + "-R-";
        }
        else
        {
            childPath = parentPath_ + "/" + childNodeName_ + "-W-";
        }
        return zkClient_->create(childPath, "", isTemp, isSeq, retPath);
    }

    void regWatcherRLock_cb(ZkUtil::ZkNotifyType type, const ZkClientPtr& client,
            const std::string& path, const std::string& value,
            int32_t version, void* context)
    {
        printf("[rlock watch cb] path:%s, type:%d, waitPath:%s\n", path.c_str(), type, leastNodePathInRLock_.c_str());
        if (path == leastNodePathInRLock_ && type == ZkUtil::kNodeDelete)
        {
            mutexRLock_.lock();
            conditionRLock_.notifyAll();
            mutexRLock_.unlock();
        }
    }

    void regWatcherWLock_cb(ZkUtil::ZkNotifyType type, const ZkClientPtr& client,
            const std::string& path, const std::string& value,
            int32_t version, void* context)
    {
        printf("[wlock watch cb] path:%s, type:%d, waitPath:%s\n", path.c_str(), type, leastNodePathInWLock_.c_str());
        if (path == leastNodePathInWLock_ && type == ZkUtil::kNodeDelete)
        {
            mutexWLock_.lock();
            conditionWLock_.notifyAll();
            mutexWLock_.unlock();
        }
    }

private:
    std::string zkConnStr_;
    ZkClientPtr zkClient_;
    volatile bool isInit_;
    const std::string parentPath_ = "/shared_lock";
    std::string childNodeName_;
    std::string leastNodePathInRLock_;
    std::string leastNodePathInWLock_;
    mutable MutexLock mutexRLock_;
    Condition conditionRLock_;
    mutable MutexLock mutexWLock_;
    Condition conditionWLock_;
    queue<std::string> readerChildPaths_;
    std::string writerChildPath_;
    RwLockerMode curLockMode_;
};


int main()
{
    DistriSharedLock distriLock;
    if (distriLock.init(ZOOKEEPER_SERVER_CONN_STRING, "rwlock") == false)
    {
        printf("init lock failed! \n");
        return 0;
    }

    distriLock.rlock();
    int count = 10;
    while (count > 0)
    {
        printf ("get reader lock! count:%d\n", count);
        sleep(2);
        count--;
    }
    distriLock.unlock();

    distriLock.wlock();
    count = 20;
    while (count > 0)
    {
        printf ("get writer lock! count:%d\n", count);
        sleep(2);
        count--;
    }
    distriLock.unlock();

    distriLock.rlock();
    count = 20;
    while (count > 0)
    {
        printf ("get reader lock! count:%d\n", count);
        sleep(2);
        count--;
    }
    distriLock.unlock();

    sleep(UINT_MAX);
    return  0;
}









