// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_CURRENTTHREAD_H
#define MUDUO_BASE_CURRENTTHREAD_H
#include <iostream>
#include <stdint.h>

namespace muduo
{
namespace CurrentThread
{
 using std::cin;
 using std::cout;
 using std::cerr;
 using std::endl;

  // internal
  extern __thread int t_cachedTid;
  extern __thread char t_tidString[32];
  extern __thread int t_tidStringLength;
  extern __thread const char* t_threadName;
  void cacheTid();

  inline int tid()
  {
    if (__builtin_expect(t_cachedTid == 0, 0))
    {
     // cout<<"in builtin" <<__FUNCTION__ <<"---"<<__LINE__<<"----"<<t_cachedTid <<endl;
      cacheTid();
    }
   else
  {

  // cout<<"not in builtin" <<__FUNCTION__ <<"---"<<__LINE__<<"----"<<t_cachedTid <<endl;
  }
    return t_cachedTid;
  }

  inline const char* tidString() // for logging
  {
    return t_tidString;
  }

  inline int tidStringLength() // for logging
  {
    return t_tidStringLength;
  }

  inline const char* name()
  {
    return t_threadName;
  }

  bool isMainThread();

  void sleepUsec(int64_t usec);
}
}

#endif
