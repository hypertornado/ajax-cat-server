MICROHTTPD_PATH = microhttpd

default: program

program:
	g++ src/server.cpp src/Logger.cpp -o ajax-cat-server -lstdc++ -l$(MICROHTTPD_PATH)

