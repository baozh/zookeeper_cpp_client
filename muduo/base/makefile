
LIB_INSTALL_COMM_PATH = ../../lib

LIB_BOOST_COMM_PATH := /export/newjpush/lib
LIB_BOOST_COMM := -L$(LIB_BOOST_COMM_PATH) -Wl,-Bstatic -lboost_thread  -Wl,-Bdynamic -lm -lc  -Wl,-Bstatic -lboost_system  -Wl,-Bdynamic -lm -lc
			
.SUFFIXES: .o .cc

cxx = g++

CFLAGS = -g -Wall -I../../ -I/export/newjpush/include -I/export/newjpush/

OI_OBJS  = AsyncLogging.o Condition.o CountDownLatch.o Date.o Exception.o \
           FileUtil.o LogFile.o Logging.o LogStream.o \
           ProcessInfo.o Thread.o ThreadPool.o Timestamp.o TimeZone.o  

OUTPUT := libbase.a

all:$(OUTPUT)

.cc.o:
	$(CXX) $(CFLAGS) -c $^ -o $@  -DMUDUO_STD_STRING $(LIB_BOOST_COMM)

libbase.a:$(OI_OBJS)
	ar -rs $@  $^

install:
	cp -f -R $(OUTPUT)  $(LIB_INSTALL_COMM_PATH)/

clean:
	rm -f *.o *.a
