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

#define PORT 8888
#define MAX_SUGGESTIONS 5
#define PERIOD_OF_CONTROL 60

using namespace std;

class MosesPair;

map<string, MosesPair *> servers;
vector<MosesPair *> serversOrder;
string mosesPath = "moses";
struct MHD_Daemon *serverDaemon;

vector<string> explode(const string& s, const char& ch) {
    string str = s + ch;
    string next = "";
    vector<string> result;

    for (string::const_iterator it = str.begin(); it != str.end(); it++) {
        if (*it == ch) {
                if (next.length() > 0) {
                        result.push_back(next);
                        next = "";
                }
        }else{
                next += *it;
        }
    }

    return result;
}

string jsonify(string s) {
    stringstream ss;
    for (size_t i = 0; i < s.length(); ++i) {
        if (unsigned(s[i]) < '\x20' || s[i] == '\\' || s[i] == '"') {
            ss << "\\u" << setfill('0') << setw(4) << hex << unsigned(s[i]);
        } else {
            ss << s[i];
        }
    } 
    return ss.str();
}

string clear_input(string s){
	for (int i = 0; i < s.length(); ++i) {
		switch (s[i]) {
			case '\n':
			s[i] = ' ';
        	}
	}
	return s;
}

class Line {
private:
	string translation;
	int first_delimiter, second_delimiter;
public:
	string line;
	int order;
	Line(string l){
		line = l;
		first_delimiter = l.find(" ||| ");
		order = atoi(l.substr(0,first_delimiter).c_str());
	}
	
	string get_translation(){
		second_delimiter = line.find(" ||| ", first_delimiter + 1);
		
		translation = line.substr(first_delimiter + 5, second_delimiter - (first_delimiter + 5));
		
		return translation;
	}
	
	vector<string> get_alignment(){
		int last_sep = line.find_last_of("|");		
		return explode(line.substr(last_sep + 2), ' ');
	}
	
	vector<string> get_translation_vector(){
		return explode(get_translation(), ' ');
	}
};

class Request {
private:
	sem_t sem;
protected:
	string result;
public:
	string sentence;
	Request(string _sentence){
		sentence = clear_input(_sentence);
		sem_init(&sem, 0, 0);
		result = "";
	}
	
	void lock(){
		sem_wait(&sem);
	}
	
	void unlock(){
		sem_post(&sem);
	}
	
	virtual void process_line(Line l) = 0;
	virtual string get_result() = 0;
	
	~Request(){
	}
};

class Logger {
private:
	static string timestr(){
		time_t rawtime;
		struct tm * timeinfo;
		char buffer [80];
		time ( &rawtime );
		timeinfo = localtime ( &rawtime );
		strftime (buffer,80,"%x %X:",timeinfo);
		return buffer;
	}

public:
	static void log(string s){
		cout << timestr() << " " << s << endl;
	}

	static void clear_logs(){
		system("rm tmp/log.txt");
	}

};

class RawRequest : public Request {
public:
	RawRequest(string _sentence) : Request(_sentence){
	
	}
	
	virtual void process_line(Line l){
		result += l.line + "\n";
		return;
	}
	
	virtual string get_result(){
		return result;
	}
	
	~RawRequest(){
	}
	
};

class SimpleRequest : public Request {
private:
public:
	SimpleRequest(string _sentence) : Request(_sentence){
	}
	
	virtual void process_line(Line l){
		if(result == ""){
			result = l.get_translation();
		}
		return;
	}
	
	virtual string get_result(){
		return result;
	}
	
	~SimpleRequest(){	
	}
	
};

class SuggestionRequest : public Request {
private:
	string covered, translated, to_translate;
	int it;
	int max_results;
	string results[MAX_SUGGESTIONS];
	int translated_size;
public:
	SuggestionRequest(string _translated, string _covered, string _sentence) : Request(_sentence){
		it = 0;
		max_results = MAX_SUGGESTIONS;
		
		translated = _translated;
		covered = _covered;
		to_translate = _sentence;
		sentence = translated;
		sentence += " ||| ";
		sentence += covered;
		sentence += " ||| ";
		sentence += to_translate;
		translated_size = explode(translated, ' ').size();
	}
	
	virtual void process_line(Line l){
		if(it < max_results){
			string suf = get_suffix(explode(l.get_translation(), ' '), translated_size, 3);
			bool addnew = true;
			for (int i = 0; i < it; ++i){
				if(suf.compare(results[i]) == 0){
					addnew = false;
					break;
				}
			}
			if (addnew){
				results[it] = suf;
				++it;
			}
		}
		return;
	}
	
	virtual string get_result(){
		string ret = "{\"suggestions\":[";
		bool start = true;
		for(int j = 0; j < it; ++j){
			if(start){
				start = false;
			}else{
				ret += ",";
			}
			ret += "\"" + results[j] + "\"";
		}
		ret += "]}";
		return ret;
	}
	
	bool is_input_correct(){
		//TODO zkontrolovat
		return true;
	}
	
	static string get_suffix(vector<string> result, int prefix_size, int max_words){
		string ret = "";
		int max = result.size();
		if(prefix_size + max_words < max){
			max = prefix_size + max_words;
		}
		
		for (int i = prefix_size; i < max; ++i){
			ret += result[i];
			if (i < max - 1){
				ret += " ";
			}
		}
		
		return ret;
	}
	
	~SuggestionRequest(){
	}
};

class Phrase{
public:
int s_s, s_e, t_s, t_e;
vector<string> target;
	Phrase(vector<string> _target, string align_source, string align_target){
		target = _target;
		vector<string> s_vector = explode(align_source, '-');
		s_s = atoi(s_vector[0].c_str());
		if(s_vector.size() == 2){
			s_e = atoi(s_vector[1].c_str());
		}else{
			s_e = atoi(s_vector[0].c_str());
		}
		
		vector<string> t_vector = explode(align_target, '-');
		t_s = atoi(t_vector[0].c_str());
		if(t_vector.size() == 2){
			t_e = atoi(t_vector[1].c_str());
		}else{
			t_e = atoi(t_vector[0].c_str());
		}
	}
	
	string get_phrase_text(){
		string ret = "";
		for (int i = t_s; i <= t_e; ++i){
			ret += target[i] + " ";
		}
		return ret;
	}
	
	string get_length(){
		//return t_e - t_s + 1;
		std::ostringstream str;
		str << t_e - t_s + 1;
		return str.str();
	}
		
};

//table of translation options
class Table{
private:
	vector<string> source;
	vector<vector<bool> > free_pos;
	vector<vector<Phrase *> > used_trans;
	int max_size;
public:

	~Table(){
		for(int i = 0; i < used_trans.size(); ++i){
			vector<Phrase *> v = used_trans[i];
			for (int j = 0; j < v.size(); ++j){
				delete v[j];
			}
		}
	}

	Table(string sentence){
		max_size = 5;
		source = explode(sentence, ' ');
		int length = source.size();
		
		for(int i = 0; i < max_size; ++i){
			vector<bool> v;
			for(int j = 0; j < length; ++j){
				v.push_back(true);
			}
			free_pos.push_back(v);
		}
		
		for(int i = 0; i < max_size; ++i){
			vector<Phrase *> v;
			for(int j = 0; j < length; ++j){
				v.push_back(NULL);
			}
			used_trans.push_back(v);
		}
	}
	
	int process_phrase(vector<string> target, string alignment){
		vector<string> al = explode(alignment, '=');
		string s_al = al[0];
		string t_al = al[1];
		
		Phrase * phrase = new Phrase(target, s_al, t_al);
		
		place_phrase(phrase);
		
		return 0;
	}
	
	bool place_phrase(Phrase *p){
		string phrase_str = p->get_phrase_text();
		for (int i = 0; i < max_size; ++i){
			if (used_trans[i][p->s_s] != NULL && phrase_str.compare(used_trans[i][p->s_s]->get_phrase_text()) == 0){
				delete p;
				return false;
			}
			bool can_place = true;
			for(int j = p->s_s; j <= p->s_e; ++j){
				if(free_pos[i][j] == false){
					can_place = false;
					break;
				}
			}
			if(can_place){
				for(int j = p->s_s; j <= p->s_e; ++j){
					free_pos[i][j] = false;
				}
				used_trans[i][p->s_s] = p;
				return true;
			}
		}
		delete p;
		return true;
	}
	
	void print_table(){
		cout << "Printing table." << endl;
		for(int i = 0; i < free_pos.size(); ++i){
			vector<bool> v = free_pos[i];
			for (int j = 0; j < v.size(); ++j){
				if(v[j] == true){
					cout << "T";
				}else{
					cout << "F";
				}
				cout << "(";
				if(used_trans[i][j] != NULL){
					cout << used_trans[i][j]->get_phrase_text();
				}
				cout << ")";
			}
			cout << endl;
		}
		cout << "End of table" << endl;
	}
	

	string get_result_table(){
		string ret = "";
		ret += "{\"source\":[";
		for(int i = 0; i < source.size(); ++i){
			if (i != 0){
				ret += ",";
			}
			ret += "\""+jsonify(source[i])+"\"";
		}
		ret += "],\"target\":[\n\n";
		bool first_row = true;
		for(int i = 0; i < free_pos.size(); ++i){
			if(first_row){
				first_row = false;
			}else{
				ret += ",";
			}
			ret += "[";
			vector<bool> v = free_pos[i];
			int empty = 0;
			bool first_col = true;
			for (int j = 0; j < v.size(); ++j){
				if(v[j] == false){
					if(empty > 0){
						std::ostringstream str;
						str << empty;
						if(first_col){
							first_col = false;
						}else{
							ret += ",";
						}
						ret += "{\"s\":\""+str.str()+"\",\"empty\":true}\n";
						empty = 0;
					}
					if(used_trans[i][j] != NULL){
						if(first_col){
							first_col = false;
						}else{
							ret += ",";
						}
						ret += "{\"t\":\"";
						ret += jsonify(used_trans[i][j]->get_phrase_text());
						ret += "\",\"s\":\"";
						ret += used_trans[i][j]->get_length();
						ret += "\"}";
						ret += "\n";
					}
				}else{
					++empty;
				}
			}
			ret += "]";
			ret += "\n\n";
		}
		ret += "]}\n";
		return ret;
	}

};

class TableRequest : public Request {
private:
	Table *table;
public:
	TableRequest(string _sentence) : Request(_sentence){
		table = new Table(_sentence);
	}
	
	virtual void process_line(Line l){
		result += l.line + "\n"; 
		vector<string> alignment = l.get_alignment();
		vector<string> translation_vector = l.get_translation_vector();
		for (int i = 0; i < alignment.size(); ++i){
			table->process_phrase(translation_vector, alignment[i]);
		}
		return;
	}
	
	virtual string get_result(){
		return table->get_result_table();
	}
	~TableRequest(){
		delete table;
	}
};


class MosesPair{
private:
	FILE * file;
	string name;
	string path;

public:

	pthread_mutex_t queue_mutex;
	pthread_mutex_t moses_mutex;
	queue<Request*> que;

	MosesPair(string _name, string _path){
		queue_mutex = PTHREAD_MUTEX_INITIALIZER;
		moses_mutex = PTHREAD_MUTEX_INITIALIZER;
		name = _name;
		path = _path;
		
		Logger::log("Starting thread " + name);
		
		string str = "rm tmp/"+name+"_out.fifo; mkfifo tmp/"+name+"_out.fifo";
		system(str.c_str());
		str = mosesPath+" -f "+path+" -n-best-list - 100 distinct -include-alignment-in-n-best true -continue-partial-translation true > tmp/"+name+"_out.fifo 2>/dev/null";
		
		file = popen(str.c_str(), "w");
		
		servers.insert(make_pair(name, this));
		serversOrder.push_back(this);
	
		pthread_t r_thread;
		long t = serversOrder.size() - 1;
		pthread_create(&r_thread, NULL, reader, (void *)t );
		
	}
	
	static void *reader(void *threadid){
	
		long tid;
   		tid = (long)threadid;		
		MosesPair *moses = serversOrder[tid];
		
		fstream file;
		string str = "tmp/"+moses->name+"_out.fifo";
		file.open(str.c_str(), ios::in);
	
		string line, out;
		int i = -1;
		Request *req = NULL;
	
		Logger::log("Thread " + moses->name + " ready.");
	
		while(getline(file,line)){
			Line l = Line(line);
			if(l.order != i){
				if(i % 2 == 0){
					req->unlock();
					req = NULL;
				}else{
					pthread_mutex_lock(&moses->queue_mutex);
					req = moses->que.front();
					moses->que.pop();
					
					pthread_mutex_unlock(&moses->queue_mutex);	
				}
				out = "";
			}
			out += line + "\n";
			i = l.order;
			if(req != NULL){
				req->process_line(l);
			}
		}	
		pthread_exit(NULL);
	}
	
	string get_translation(Request *req){
		pthread_mutex_lock(&queue_mutex);
		que.push(req);		
		pthread_mutex_unlock(&queue_mutex);
		pthread_mutex_lock(&moses_mutex);
		fprintf(file,"%s\nxxxxxxxxnonsensestringxxxxxxxx\n", req->sentence.c_str());
		fflush(file);
		pthread_mutex_unlock(&moses_mutex);
		req->lock();
		return req->get_result();
	}
};

static int
answer_to_connection (void *cls, struct MHD_Connection *connection, const char *url, const char *method, const char *version, const char *upload_data,
	size_t *upload_data_size, void **con_cls){
	
	
	string page = "";
	Request *req = NULL;
	const char * sentence  = MHD_lookup_connection_value (connection, MHD_GET_ARGUMENT_KIND, "q");
	const char * covered  = MHD_lookup_connection_value (connection, MHD_GET_ARGUMENT_KIND, "covered");
	const char * translated  = MHD_lookup_connection_value (connection, MHD_GET_ARGUMENT_KIND, "translated");
	const char * pair  = MHD_lookup_connection_value (connection, MHD_GET_ARGUMENT_KIND, "pair");
	
	string log = "";
	
	int status_code = 200;
	
	MosesPair *moses;
	if (pair != NULL){
		moses = servers[pair];
	}else{
		moses = NULL;
	}
	
	if (moses == NULL){
		status_code = 400;
	}
	
	if(status_code == 200 && strcmp(url,"/raw") == 0){
		log = "raw " + string(sentence);
		req = new RawRequest(sentence);
		page = moses->get_translation(req);
		delete dynamic_cast<RawRequest*> (req);
	}else if(status_code == 200 && strcmp(url,"/simple") == 0){
		log = "simple " + string(sentence);
		req = new SimpleRequest(sentence);
		page = moses->get_translation(req);
		delete dynamic_cast<SimpleRequest*> (req);
	}else if(status_code == 200 && strcmp(url,"/table") == 0){
		log = "table " + string(sentence);
		req = new TableRequest(sentence);
		page = moses->get_translation(req);
		delete dynamic_cast<TableRequest*> (req);
	}else if(status_code == 200 && strcmp(url,"/suggestion") == 0){
		req = new SuggestionRequest(translated, covered, sentence);
		log = "suggestion ||| " + string(translated) + " ||| " + string(covered) + " ||| " + string(sentence);
		SuggestionRequest *sug_req = dynamic_cast<SuggestionRequest*>(req);
		//TODO zkontrolovat
		if(sug_req->is_input_correct()){
			page = moses->get_translation(req);
		}else{
			status_code = 400;
		}
		delete dynamic_cast<SuggestionRequest*> (req);
	}else if(status_code == 200){
		status_code = 400;
	}
	
	if(status_code == 400){
		log = "bad request";
		page = "{\"error_message\":\"bad request\"}";
	}
	
	
	struct MHD_Response *response;
	int ret;
	response = MHD_create_response_from_data (strlen(page.c_str()), strdup(page.c_str()), MHD_YES, 0);
	ret = MHD_queue_response (connection, status_code, response);
	MHD_destroy_response (response);
	
	return ret;
}


void close_program(){
	for(int i = 0; i < serversOrder.size(); ++i){
		delete serversOrder[i];
	}
	Logger::log("Closing server.");
}


static void *control_thread(void *threadid){
	RawRequest * req;
	while(true){
		Logger::log("performing period check, if moses is running");
		for(int i = 0; i < serversOrder.size(); ++i){
			MosesPair *moses = serversOrder[i];
			req = new RawRequest("xxx_sentence_to_check_if_all_moses_servers_are_running");
			delete req;
		}
		sleep(PERIOD_OF_CONTROL);
	}
}

void * uri_logger(void * cls, const char * uri){
	Logger::log(uri);
}

int main (){
	Logger::clear_logs();
	Logger::log("Starting server.");

	system("mkdir tmp");
	
	fstream file;
	file.open("server.ini",ios::in|ios::out);
	file.seekg (0, ios::beg);
	string line;
	while(getline(file,line)){
		if (line.compare("[moses-path]") == 0){
			getline(file,line);
			mosesPath = line;
		}else if(line.compare("[translation-pair]") == 0){
			string name, path;
			getline(file,name);
			getline(file,path);
			new MosesPair(name,path);
		}
	}

	file.close();
	
	
	serverDaemon = MHD_start_daemon (MHD_USE_THREAD_PER_CONNECTION | MHD_USE_DEBUG, PORT, NULL, NULL, &answer_to_connection, NULL,
		MHD_OPTION_URI_LOG_CALLBACK,
                uri_logger,
                NULL, MHD_OPTION_END
                );
	if (NULL == serverDaemon){
		close_program();
		return 1;
	}
	
	pthread_t c_thread;
	long t = 0;
	pthread_create(&c_thread, NULL, control_thread, (void *)t );
	
	getchar();
	
	return 0;
}
