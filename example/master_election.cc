/*
 * master_election.cc
 *
 *      Description: 利用zookeeper实现master选举
 *      Created on: 2016年2月21日
 *      Author: ZengHui Bao (bao_z_h@163.com)
 */

#include "ZkClient.h"
#include "ZkClientManager.h"
#include <stdio.h>
#include <string>
#include <vector>
#include <assert.h>
#include <algorithm>
#include <unistd.h>
#include "muduo/base/Thread.h"
#include <boost/noncopyable.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>

using namespace std;
using namespace ZkCppClient;

#define     ZOOKEEPER_SERVER_CONN_STRING        "127.0.0.1:2181,127.0.0.1:2182,127.0.0.1:2183"



class MasterElection : boost::noncopyable
{
public:
    typedef boost::function<void()> RunFunctor;

    //对外服务接口
    static MasterElection& instance() {return muduo::Singleton<MasterElection>::instance();};

    //注：masterFun 必须是非阻塞的．
    bool init(const std::string& zkConnStr, RunFunctor masterFun)
    {
        if (isInit_ == false)
        {
            zkConnStr_ = zkConnStr;
            masterFun_ = masterFun;
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

    bool start()
    {
        if (isInit_ == false)
            return false;

        //注册 watcher
        if (zkClient_->regChildWatcher(parentPath_,
            boost::bind(&MasterElection::regChildWatcher_cb, this, _1, _2, _3, _4, _5), NULL) == false)
        {
            printf("\n regChildWatcher failed! path:%s\n", parentPath_.c_str());
            return false;
        }

        return retry();
    }

    bool isMaster() {return isInit_ == true && isMaster_ == true;};

public:
    MasterElection()
    {
        zkConnStr_ = "";
        isInit_ = false;
        isMaster_ = false;
    };

    ~MasterElection()
    {
        //删除结点
        ZkUtil::ZkErrorCode ec = zkClient_->deleteRecursive(childPath_);
        //释放zookeeper handle
        ZkClientManager::instance().destroyClient(zkClient_->getHandle());
        zkClient_.reset();
        isInit_ = false;
        isMaster_ = false;
    };

private:

    bool retry()
    {
        //创建 子路径
        bool isTemp = true;  //临时结点
        bool isSeq = false;
        std::string retPath;
        ZkUtil::ZkErrorCode ec = zkClient_->create(childPath_, "", isTemp, isSeq, retPath);
        if (ec == ZkUtil::kZKSucceed)     //创建成功，说明 竞争到masterShip
        {
            isMaster_ = true;
            if (masterFun_)
                masterFun_();  //注：masterFun 必须是非阻塞的．
        }
        else if (ec == ZkUtil::kZKExisted)
        {
            isMaster_ = false;
        }
        else if (ec != ZkUtil::kZKExisted)
        {
            printf("create childPath failed! errCode:%d\n", ec);
            return false;
        }
        return true;
    }

    void regChildWatcher_cb(ZkUtil::ZkNotifyType type, const ZkClientPtr& client,
        const std::string& path, const std::vector<std::string>& childNodes, void* context)
    {
        assert(path == parentPath_);
        //如果　没有 "binding" 子结点，说明 mastership已经释放了，再重新竞争mastership
        if (std::find(childNodes.begin(), childNodes.end(), "binding") == childNodes.end())
        {
            retry();
        }
    }

private:
    std::string zkConnStr_;
    ZkClientPtr zkClient_;
    volatile bool isInit_;
    volatile bool isMaster_;
    RunFunctor masterFun_;
    const std::string parentPath_ = "/master_election";
    const std::string childPath_ = "/master_election/binding";
};


void masterFunCb()
{
    while(1)
    {
        printf("I'm master. Run in masterFunCb.\n");
        sleep(2);
    }
}

void masterFun()
{
    muduo::Thread* pThreadHandle = new muduo::Thread(boost::bind(&masterFunCb), "master_fun_thread");
    if (pThreadHandle != NULL)
    {
        pThreadHandle->start();
    }
}

int main()
{
    if (MasterElection::instance().init(ZOOKEEPER_SERVER_CONN_STRING, boost::bind(&masterFun)) == false)
    {
        printf("MasterElection init failed!\n");
        return 0;
    }

    MasterElection::instance().start();

    while(1)
    {
        printf("I'm %s. Run in main.\n", MasterElection::instance().isMaster() ? "Master" : "Slave");
        sleep(2);
    }

    return  0;
}

