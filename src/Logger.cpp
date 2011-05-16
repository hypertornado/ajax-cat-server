#include "Logger.h"

using namespace std;

string Logger::timestr(){
	time_t rawtime;
	struct tm * timeinfo;
	char buffer [80];
	time ( &rawtime );
	timeinfo = localtime ( &rawtime );
	strftime (buffer,80,"%x %X:",timeinfo);
	return buffer;
}


void Logger::log(string s){
	cout << timestr() << " " << s << endl;
}

void Logger::clear_logs(){
	system("rm tmp/log.txt");
}
