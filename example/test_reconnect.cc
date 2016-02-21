/*
 * test_reconnect.cc
 *
 *      Description: 测试session重连
 *      Created on: 2016年2月20日
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
#include <muduo/base/CountDownLatch.h>



using namespace std;
using namespace ZkCppClient;

#define     ZOOKEEPER_SERVER_CONN_STRING        "127.0.0.1:2181,127.0.0.1:2182,127.0.0.1:2183"

uint32_t gZkClientHandle;



void printChild(const std::string& path, const std::vector<std::string>& childnodes)
{
    printf("\n\n-----------[printChild] ParentPath: %s, child size:%d-------------- \n", path.c_str(), childnodes.size());
    std::vector<std::string>::const_iterator iter = childnodes.begin();
    for(;iter != childnodes.end(); iter++)
    {
        printf("child name:%s\n", (*iter).c_str());
    }
    printf("-------------------------------------------------------------------- \n");
}

bool isEqualChildren(const std::vector<std::string>& left, const std::vector<std::string>& right)
{
    if (left.size() != right.size())
    {
        return false;
    }

    std::vector<std::string>::const_iterator iter = left.begin();
    for (; iter != left.end(); iter++)
    {
        if (find(right.begin(), right.end(), (*iter)) == right.end())
        {
            printf("[isEqualChildren] left vec elem: %s don't exist in right vec.\n", (*iter).c_str());
            return false;
        }
    }

    return true;
}

void printClientId(const SessionClientId& cliId)
{
    printf("\ncliendId:\n");
    printf("cliId.id:%d\n", cliId.client_id);
    printf("cliId.passwd:%s\n", cliId.passwd);
}

std::string getNotifyTypeStr(ZkUtil::ZkNotifyType type)
{
    if (ZkUtil::kNodeDelete == type)
    {
        return "ZkUtil::kNodeDelete";
    }
    else if (ZkUtil::kNodeCreate == type)
    {
        return "ZkUtil::kNodeCreate";
    }
    else if (ZkUtil::kNodeChange == type)
    {
        return "ZkUtil::kNodeChange";
    }
    else if (ZkUtil::kGetNodeValueFailed == type)
    {
        return "ZkUtil::kGetNodeValueFailed";
    }
    else if (ZkUtil::kChildChange == type)
    {
        return "ZkUtil::kChildChange";
    }
    else if (ZkUtil::kGetChildListFailed == type)
    {
        return "ZkUtil::kGetChildListFailed";
    }
    else if (ZkUtil::kTypeError == type)
    {
        return "ZkUtil::kTypeError";
    }
    else if (ZkUtil::kGetNodeValueFailed_NodeNotExist == type)
    {
        return "ZkUtil::kGetNodeValueFailed_NodeNotExist";
    }
    else if (ZkUtil::kGetChildListFailed_ParentNotExist == type)
    {
        return "ZkUtil::kGetChildListFailed_ParentNotExist";
    }
}









void regNodeWatcher_cb(ZkUtil::ZkNotifyType type, const ZkClientPtr& client,
    const std::string& path, const std::string& value, int32_t version, void* context);

void regNodeWatcher_regWatcher_test()
{
#define  NODEWATCHER_PATH_NAME     "/reconnect_regNodeWatcher_test_node"
    ZkUtil::ZkErrorCode ec;
    ZkClientPtr cli = ZkClientManager::instance().getZkClient(gZkClientHandle);

    //删除结点
    ec = cli->deleteNode(NODEWATCHER_PATH_NAME);
    if (ec != ZkUtil::kZKSucceed && ec != ZkUtil::kZKNotExist)
    {
        printf("\n[regNodeWatcher_regWatcher_test] delete path:%s failed! ec:%d \n", NODEWATCHER_PATH_NAME,ec);
        return;
    }

    //注册watcher
    std::string *oriPath = new std::string(NODEWATCHER_PATH_NAME);
    if (cli->regNodeWatcher(NODEWATCHER_PATH_NAME,
                            boost::bind(&regNodeWatcher_cb, _1, _2, _3, _4, _5, _6), oriPath) == false)
    {
        printf("\n[regNodeWatcher_regWatcher_test] regNodeWatcher failed! path:%s\n", NODEWATCHER_PATH_NAME);
        return;
    }
}

void regNodeWatcher_create_delete_set_test()
{
    ZkUtil::ZkErrorCode ec;
    ZkClientPtr cli = ZkClientManager::instance().getZkClient(gZkClientHandle);

    printf("[regNodeWatcher_create_delete_set_test] create node.\n");
    bool isTemp = false;
    bool isSeq = false;
    std::string retPath;
    ec = cli->create(NODEWATCHER_PATH_NAME, "test_value_create", isTemp, isSeq, retPath);
    if (ec != ZkUtil::kZKSucceed && ec != ZkUtil::kZKExisted)
    {
        printf("\n[regNodeWatcher_create_delete_set_test] create path:%s failed! \n", NODEWATCHER_PATH_NAME);
        return;
    }

    sleep(10);

    printf("[regNodeWatcher_create_delete_set_test] set node.\n");
    ec = cli->set(NODEWATCHER_PATH_NAME, "test_value_set");
    if (ec != ZkUtil::kZKSucceed)
    {
        printf("\n[regNodeWatcher_create_delete_set_test] set path:%s failed! \n", NODEWATCHER_PATH_NAME);
        return;
    }

    sleep(10);

    printf("[regNodeWatcher_create_delete_set_test] delete node.\n");
    ec = cli->deleteNode(NODEWATCHER_PATH_NAME);
    if (ec != ZkUtil::kZKSucceed)
    {
        printf("\n[regNodeWatcher_create_delete_set_test] delete path:%s failed! \n", NODEWATCHER_PATH_NAME);
        return;
    }
}

void regNodeWatcher_cb(ZkUtil::ZkNotifyType type, const ZkClientPtr& client,
    const std::string& path, const std::string& value, int32_t version, void* context)
{
    ZkClientPtr cli = ZkClientManager::instance().getZkClient(gZkClientHandle);
    assert(client == cli);

    assert(context != NULL);
    std::string *oriPath = (std::string*) context;
    assert(path == (*oriPath));

    printf("[regNodeWatcher_cb] notifyType:%d,%s, value:%s, version:%d \n",
            type, getNotifyTypeStr(type).c_str(), value.c_str(), version);
}








void regChildWatcher_cb(ZkUtil::ZkNotifyType type, const ZkClientPtr& client,
    const std::string& path, const std::vector<std::string>& childNodes, void* context);

void regChildWatcher_regWatcher_test()
{
#define  REGCHILDWATCH_PARENT_NAME     "/regChildWatcher_parent"

    ZkClientPtr cli = ZkClientManager::instance().getZkClient(gZkClientHandle);

    //删除结点
    ZkUtil::ZkErrorCode ec_other = cli->deleteRecursive(REGCHILDWATCH_PARENT_NAME);
    assert(ec_other == ZkUtil::kZKSucceed);

    //如果父结点没有创建，注册watcher 这时会注册失败
    std::string *oriPath = new std::string(REGCHILDWATCH_PARENT_NAME);
    if (cli->regChildWatcher(REGCHILDWATCH_PARENT_NAME,
        boost::bind(&regChildWatcher_cb, _1, _2, _3, _4, _5), oriPath) == false)
    {
        printf("\n[regChildWatcher_regWatcher_test] regChildWatcher failed! path:%s\n", REGCHILDWATCH_PARENT_NAME);
    }

    printf("create parent node, don't call watcher...\n");
    sleep(5);

    //创建父结点
    std::string retPath = "";
    ZkUtil::ZkErrorCode ec = cli->create(REGCHILDWATCH_PARENT_NAME, "", false, false, retPath);
    if (ec != ZkUtil::kZKSucceed)
    {
        printf("\n[regChildWatcher_regWatcher_test] create path:%s failed! \n", REGCHILDWATCH_PARENT_NAME);
        return;
    }

    //注册watcher 这次会成功
    if (cli->regChildWatcher(REGCHILDWATCH_PARENT_NAME,
        boost::bind(&regChildWatcher_cb, _1, _2, _3, _4, _5), oriPath) == false)
    {
        printf("\n[regChildWatcher_regWatcher_test] regChildWatcher failed! path:%s\n", REGCHILDWATCH_PARENT_NAME);
    }
    else
    {
        printf("\n[regChildWatcher_regWatcher_test] regChildWatcher succeed! path:%s\n", REGCHILDWATCH_PARENT_NAME);
    }
}

void regChildWatcher_change_child_test()
{
#define  REGCHILDWATCH_CHILD_NAME    "regChildWatcher_child"
#define  REGCHILDWATCH_CHILD_NUM      3

    ZkClientPtr cli = ZkClientManager::instance().getZkClient(gZkClientHandle);
    std::string retPath;
    //创建 子结点
    std::vector<std::string> childNodes;
    for (int i = 0; i < REGCHILDWATCH_CHILD_NUM; i++)
    {
        char path[100] = {0};
        char childName[100] = {0};
        snprintf(path, sizeof(path), "%s/%s_%d", REGCHILDWATCH_PARENT_NAME, REGCHILDWATCH_CHILD_NAME, i);

        printf("create child node(i=%d), call watcher...\n", i);
        sleep(5);

        ZkUtil::ZkErrorCode ec_chd = cli->create(path, "", false, false, retPath);
        if (ec_chd != ZkUtil::kZKSucceed)
        {
            printf("\n[regChildWatcher_change_child_test] create path:%s failed! \n", path);
            return;
        }
    }

    //删除子结点
    for (int i = 0; i < REGCHILDWATCH_CHILD_NUM; i++)
    {
        printf("delete child node(i=%d), call watcher...\n", i);
        sleep(5);

        char path[100] = {0};
        char childName[100] = {0};
        snprintf(path, sizeof(path), "%s/%s_%d", REGCHILDWATCH_PARENT_NAME, REGCHILDWATCH_CHILD_NAME, i);
        ZkUtil::ZkErrorCode ec_chd = cli->deleteNode(path);
        if (ec_chd != ZkUtil::kZKSucceed && ec_chd != ZkUtil::kZKNotExist)
        {
            printf("\n[regChildWatcher_change_child_test] create path:%s failed! \n", path);
            return;
        }
    }
}

void regChildWatcher_cb(ZkUtil::ZkNotifyType type, const ZkClientPtr& client,
    const std::string& path, const std::vector<std::string>& childNodes, void* context)
{
    ZkClientPtr cli = ZkClientManager::instance().getZkClient(gZkClientHandle);
    assert(client == cli);

    assert(context != NULL);
    std::string *oriPath = (std::string*) context;
    assert(path == (*oriPath));


    printf("[regChildWatcher_cb] notifyType:%d,%s, parentPath:%s \n",
        type, getNotifyTypeStr(type).c_str(), path.c_str());

    printChild(path, childNodes);
}






void supportReconnect_test()
{
    //通过session handle，获取ZkClient
    ZkClientPtr cli = ZkClientManager::instance().getZkClient(gZkClientHandle);

    printf("[supportReconnect_test] handle:%d, isConnect:%d, isSupportReconnect:%d\n", cli->getHandle(), cli->isConnected(), cli->isSupportReconnect());

    //设置不支持重连
    cli->setIsSupportReconnect(false);
    printf("[supportReconnect_test] handle:%d, isConnect:%d, isSupportReconnect:%d\n", cli->getHandle(), cli->isConnected(), cli->isSupportReconnect());

    //这个时候 kill掉所有zookeeper server进程，或者 拔掉 与zookeeper server的网掉，使它们断开连接
    printf("[supportReconnect_test] Please kill zookeeper server!!!! ------------------------------------------------------------------------------\n");

    sleep(60);

    bool ret = cli->isConnected();
    while(ret == false)
    {
        printf("[supportReconnect_test] handle:%d, isConnect:%d, isSupportReconnect:%d\n", cli->getHandle(), cli->isConnected(), cli->isSupportReconnect());
        sleep(5);
        ret = cli->isConnected();

        //看到 打印了 几次 isConnect:0 之后，再插上网线，或者 启动zookeeper server进程.
        //但是 由于不支持重连，所以这里会 一直打印下去...
    }
}

void reConnected_test()
{
    //通过session handle，获取ZkClient
    ZkClientPtr cli = ZkClientManager::instance().getZkClient(gZkClientHandle);

    printf("[reConnected_test] handle:%d, isConnect:%d\n", cli->getHandle(), cli->isConnected());

    //测试node watcher
    regNodeWatcher_regWatcher_test();
    regNodeWatcher_create_delete_set_test();

    //测试child watcher
    regChildWatcher_regWatcher_test();
    regChildWatcher_change_child_test();

    //这个时候 kill掉所有zookeeper server进程，或者 拔掉 与zookeeper server的网掉，使它们断开连接
    printf("[reConnected_test] Please kill zookeeper server!!!! ----------------------------------------------------------------\n");

    sleep(60);

    bool ret = cli->isConnected();
    while(ret == false)
    {
        printf("[reConnected_test] handle:%d, isConnect:%d\n", cli->getHandle(), cli->isConnected());
        sleep(5);
        ret = cli->isConnected();

        //看到 打印了 几次 isConnect:0 之后，再插上网线，或者 启动zookeeper server进程.
    }

    //zkclient默认是支持重连的，会由定时器重连，此时应该连接成功了
    printf("[reConnected_test] handle:%d, isConnect:%d\n", cli->getHandle(), cli->isConnected());

    printf("[reConnected_test] session is reconnected, it will register all nodeWatcher, all childWatcher...\n");
    sleep(10);

    //当session重新连接上之后，会再注册所有nodeWatch, childWatcher.
    //测试node watcher
    regNodeWatcher_create_delete_set_test();

    //测试child watcher
    regChildWatcher_change_child_test();

    printf("[reConnected_test] reConnected_test succeed!\n");
}


int main()
{
    //设置zookeeper日志路径
    if (ZkClientManager::setLogConf(true, "./zk_log") == false)
    {
        printf("setLogConf failed!\n\n");
    }

    //创建一个session
    uint32_t handle = ZkClientManager::instance().createZkClient(ZOOKEEPER_SERVER_CONN_STRING, 30000, NULL, NULL, NULL);
    if (handle == 0)
    {
        printf("create session failed! connStr:%s\n", ZOOKEEPER_SERVER_CONN_STRING);
        return 0;
    }

    gZkClientHandle = handle;

    reConnected_test();

    supportReconnect_test();

    sleep(UINT_MAX);

    return  0;
}


