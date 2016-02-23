# ZkCppClient

## ZkCppClient 解决的问题

ZkCppClient 类似Java客户端ZkClient、Curator，是对ZookeeperLib c api的C++封装，主要解决以下几个问题：

1. 支持Watcher的永久注册  
Client收到Watcher通知后，会再向Zookeeper注册Watcher。并且，也提供了接口 `取消Watcher的重注册`。

2. 支持session重连  
当session超时后，Client会启一个定时器定时重连(默认支持重连)。并且，也提供了接口 `不支持重连`。

3. 提供接口，支持递归创建父子结点、递归删除分支结点。  
创建结点的时候 会判断 父结点是否存在，如果不存在，会先创建父结点。
删除结点的时候 会判断 是否有子结点，如果存在，会先删除子结点。

4. 接口友好、错误码归类  
分离了获取数据、注册Watcher的接口，并只对用户提供用得到的参数，对各种错误码做了归类、删减。

## 特点

1. Client对象利用shared_ptr的形式来对外提供访问。  
由于Client对象 客户端库和用户代码都会访问，它的生命期比较模糊，所以内部、外部都使用boost::shared_ptr来访问Client。

2. 借鉴Curator，将Watcher归类成两种：NodeWatcher, ChildWatcher。分离出注册Watcher和获取数据两种接口。  
NodeWatcher监听节点的变更(节点删除，节点创建，节点数据变更)，ChildWatcher监听子节点的变更(增加、删除子结点)，且回调Watcher时会 提供最新的结点值 或 子结点列表。

3. 回调函数（Watcher回调、数据回调）采用了boost::function/boost::bind形式，并提供了同步、异步两种接口。

4. 抽象出了结点的version，提供指定version的CAS操作。  
在Get结点数据时，会获取到结点的Stat结构，从其中抽取了version，之后在Set,Delete时，可根据指定的version做设置、删除操作。


## 使用方法

ZkCppClient使用了boost库和muduo库，在编译时需要添加编译选项-DTHREADED，链接zookeeper_mt库。具体的使用方法见examle目录。

