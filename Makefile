MICROHTTPD_PATH = microhttpd

default: program

program:
	g++ src/server.cpp -o ajax-cat-server -lstdc++ -std=c++0x -l$(MICROHTTPD_PATH)

