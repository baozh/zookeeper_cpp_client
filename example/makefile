
LIB_INSTALL_COMM_PATH = ../lib

INC_COMM_PATH =  -I./   -I../ -I/usr/local/include/zookeeper -I../src/ -I/export/newjpush/include -I/export/newjpush/

LIB_COMM =  -lzookeeper_mt -L$(LIB_INSTALL_COMM_PATH) -lbase -lpthread -lrt   -L$(LIB_INSTALL_COMM_PATH) -lnet

.SUFFIXES: .o .cc

C_ARGS := -g -O -Wall -fno-common

cxx = g++

CFLAGS = -g -O -Wall  $(INC_COMM_PATH) 

BINARY_TEST := test
OI_OBJS_TEST  = ../src/ZkTimerQueue.o ../src/ZkClient.o  ../src/ZkClientManager.o test.o 

BINARY_RECONN := test_reconn
OI_OBJS_RECONN  = ../src/ZkTimerQueue.o ../src/ZkClient.o  ../src/ZkClientManager.o test_reconnect.o 

BINARY_ELECTION := master_election
OI_OBJS_ELECTION  = ../src/ZkTimerQueue.o ../src/ZkClient.o  ../src/ZkClientManager.o master_election.o 

BINARY_LOCK := distribute_lock
OI_OBJS_LOCK  = ../src/ZkTimerQueue.o ../src/ZkClient.o  ../src/ZkClientManager.o distribute_lock.o 

BINARY_SHAREDLOCK := distribute_sharedlock
OI_OBJS_SHAREDLOCK  = ../src/ZkTimerQueue.o ../src/ZkClient.o  ../src/ZkClientManager.o distribute_sharedlock.o 

BINARY_BARRIER := distribute_barrier
OI_OBJS_BARRIER  = ../src/ZkTimerQueue.o ../src/ZkClient.o  ../src/ZkClientManager.o distribute_barrier.o 

all:$(BINARY_TEST) $(BINARY_RECONN) $(BINARY_ELECTION) $(BINARY_LOCK) $(BINARY_SHAREDLOCK) $(BINARY_BARRIER)

.cc.o:
	$(CXX) $(CFLAGS) -c $^ -o ./$@ -DMUDUO_STD_STRING -DUSE_CALL_RECORDER -DTHREADED $(LIB_COMM)
	
$(BINARY_TEST):$(OI_OBJS_TEST)
	$(CXX) $(C_ARGS) -o $@ $^ $(LIB_COMM)

$(BINARY_RECONN):$(OI_OBJS_RECONN)
	$(CXX) $(C_ARGS) -o $@ $^ $(LIB_COMM)

$(BINARY_ELECTION):$(OI_OBJS_ELECTION)
	$(CXX) $(C_ARGS) -o $@ $^ $(LIB_COMM)
	
$(BINARY_LOCK):$(OI_OBJS_LOCK)
	$(CXX) $(C_ARGS) -o $@ $^ $(LIB_COMM)
	
$(BINARY_SHAREDLOCK):$(OI_OBJS_SHAREDLOCK)
	$(CXX) $(C_ARGS) -o $@ $^ $(LIB_COMM)

$(BINARY_BARRIER):$(OI_OBJS_BARRIER)
	$(CXX) $(C_ARGS) -o $@ $^ $(LIB_COMM)
	
clean:
	rm -f *.o  ../src/*.o $(BINARY_TEST) $(BINARY_RECONN) $(BINARY_ELECTION) $(BINARY_LOCK) $(BINARY_SHAREDLOCK) $(BINARY_BARRIER)
