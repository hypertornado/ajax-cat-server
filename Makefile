HEADERS = Util.h

default: program

program:
	g++ server.cpp -o srv -lstdc++ -std=c++0x -Iboost_1_46_0 -I/home/ondra/s/http/libmicrohttpd/src/daemon -L/home/ondra/s/http/libmicrohttpd/src/daemon -lmicrohttpd

