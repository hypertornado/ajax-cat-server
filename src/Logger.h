#include <string>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <microhttpd.h>
#include <stdlib.h>
#include <stdio.h>
#include <queue>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <sstream>
#include <iterator>
#include <fstream>
#include <vector>
#include <semaphore.h>
#include <deque>
#include <list>
#include <sstream>
#include <iomanip>
#include <map>
#include <cstring>

#ifndef LOGGER_H
#define LOGGER_H

using namespace std;

class Logger {
private:
	static string timestr();

public:
	static void log(string s);
	static void clear_logs();
};

#endif
