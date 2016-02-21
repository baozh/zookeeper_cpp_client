
LIB_INSTALL_COMM_PATH = ../../lib

INC_COMM_PATH = -I../../  -I../../ -I/export/newjpush/include -I/export/newjpush/

LIB_BOOST_COMM_PATH := /export/newjpush/lib
LIB_BOOST_COMM := -L$(LIB_BOOST_COMM_PATH) -Wl,-Bstatic -lboost_thread  -Wl,-Bdynamic -lm -lc  -Wl,-Bstatic -lboost_system  -Wl,-Bdynamic -lm -lc

.SUFFIXES: .o .cc

cxx = g++

CFLAGS = -g -Wall  $(INC_COMM_PATH) 

OI_OBJS  = Acceptor.o  Boilerplate.o  Buffer.o \
Channel.o  Connector.o ConnectorOri.o EventLoop.o  EventLoopThread.o \
EventLoopThreadPool.o InetAddress.o Poller.o Socket.o \
SocketsOps.o TcpConnection.o  TcpServer.o TcpClient.o \
Timer.o TimerQueue.o ./poller/DefaultPoller.o ./poller/EPollPoller.o \
./poller/PollPoller.o UdpClient.o

OUTPUT := libnet.a

all:$(OUTPUT)

.cc.o:
	$(CXX) $(CFLAGS) -c $^ -o ./$@   -DMUDUO_STD_STRING
	
libnet.a:$(OI_OBJS)
	ar -rs $@ $^

install:
	cp -R -f $(OUTPUT)  $(LIB_INSTALL_COMM_PATH)/

clean:
	rm -f *.o *.a ./poller/*.o 
