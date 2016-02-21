#ifndef __HEARTSTAT_H
#define __HEARTSTAT_H
#include <muduo/base/Types.h>
#include <set>
#include <boost/weak_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <new>
#include <muduo/net/Callbacks.h>
namespace muduo{
namespace net{
class TcpConnection;
class HeartStat{
public:
HeartStat(uint64_t lastHeartTime,const int fd,const muduo::net::TcpConnectionPtr & conn):
																lastHeartTime_(lastHeartTime),
																fd_(fd),
																conn_(conn),
																isrm_(false){}
int getFd() const{ return fd_; }
uint64_t getLastHeartTime() const { return lastHeartTime_; }
void setLastHeartTime(uint64_t lastHeartTime){ lastHeartTime_ = lastHeartTime; }
muduo::net::TcpConnectionPtr getConnection(){ 
	if(conn_.expired())
	{
		return muduo::net::TcpConnectionPtr(); 
	}
	else
	{
		return conn_.lock();
	}
}
bool getRm() const { return isrm_; }
void setRm(bool isrm){ isrm_ = isrm; }
private:
uint64_t lastHeartTime_;
int fd_;
boost::weak_ptr<muduo::net::TcpConnection> conn_;
bool isrm_;//标识当前心跳状态是否从心跳状态集合中删除
};

//author by egypt
//just as the compare in the set
class HeartStatCmp
{
public:
bool operator()(const HeartStat * t1, const HeartStat * t2)
{
	if(t1->getLastHeartTime() == t2->getLastHeartTime())
	{
		if(t1->getFd()== t2->getFd())
		{
			return false;
		}
		else
		{
			return t1->getFd() < t2->getFd();
		}
	}
	else
	{
		return t1->getLastHeartTime() < t2->getLastHeartTime();
	}
}

};

typedef std::set<HeartStat *,HeartStatCmp> HeartSet;
typedef std::pair<HeartSet::iterator, bool> HeartResultPair;


class HeartStatFind
{
public:
HeartStatFind(HeartStat * stat):key_(stat){ }
bool operator()(const HeartStat *item)
{
	return item->getLastHeartTime() == key_->getLastHeartTime() && 
		   item->getFd() ==  key_->getFd();
}
private:
HeartStat* key_; 
};

// T1 equal as std::pair<muduo::base::HeartStat *, TcpConnection *>
class HeartStatLowBound
{
public:
template<class T1, class T2>
bool operator()(const T1 &t1, const T2 &t2)
{
	if(t1->getLastHeartTime() < t2->getLastHeartTime())
		return true;
	else
	{
		if(t1->getLastHeartTime() == t2->getLastHeartTime())
		{
			if(t1->getFd() < t2->getFd())
			{
				return true;
			}
			else
				return false;
		}
		else
		{
			return false;
		}

	}

}

};

}
}
#endif

