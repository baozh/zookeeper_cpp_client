
LIB_INSTALL_COMM_PATH = ../lib

INC_COMM_PATH =  -I./   -I../ -I/usr/local/include/zookeeper -I/export/newjpush/include -I/export/newjpush/

LIB_BOOST_COMM_PATH := /export/newjpush/lib
LIB_BOOST_COMM := -L$(LIB_BOOST_COMM_PATH) -Wl,-Bstatic -lboost_thread  -Wl,-Bdynamic -lm -lc  -Wl,-Bstatic -lboost_system  -Wl,-Bdynamic -lm -lc

LIB_COMM = -lzookeeper_mt -L$(LIB_INSTALL_COMM_PATH) -lbase -lpthread -lrt   -L$(LIB_INSTALL_COMM_PATH) -lnet  -lboost_thread  -lboost_system $(LIB_BOOST_COMM)

.SUFFIXES: .o .cc

cxx = g++

CFLAGS = -g -O -Wall  $(INC_COMM_PATH) 

OI_OBJS  =  ZkClient.o  ZkClientManager.o   ZkTimerQueue.o 

OUTPUT := libzkcpp.a

all:$(OUTPUT)

.cc.o:
	$(CXX) $(CFLAGS) -c $^ -o ./$@ -DMUDUO_STD_STRING -DUSE_CALL_RECORDER -DTHREADED $(LIB_COMM)
		
mvHead:
	cp -R -f  $(HEADERS) $(INC_INSTALL_COMM_PATH)/
	
$(OUTPUT):$(OI_OBJS)
	ar -rs $@ $^

install:
	cp -R -f $(OUTPUT)  $(LIB_INSTALL_COMM_PATH)/

clean:
	rm -f *.o *.a
