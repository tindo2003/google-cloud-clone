#ifndef SERVER_H
#define SERVER_H

/* 
TODO: 
1) support persistent connection 
2) use the "100 Continue" response appropriately
4) handle requests with If-Modified-Since: or If-Unmodified-Since: headers
5) accept requests with chunked data
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include <iostream>
#include <signal.h>
#include <sstream>
#include <unordered_map>
#include <ctime>
#include <iomanip>
#include <vector>
#include <fstream>
#include <map>
#include "nlohmann/json.hpp"
#include "template_render.h"
#include "storage_api.h"
#include "storage_request.h"
#include "frontend_http.h"
#include <uuid/uuid.h>
#include <atomic>



using json = nlohmann::json;



volatile bool keep_running = true;

#define FE_SERVER_PORT 10000        // Frontend server listens on this port
#define LB_HEARTBEAT_IP "127.0.0.1" // IP of the load balancer for heartbeats
#define HEARTBEAT_PORT 9090         // Heartbeat port on the load balancer
#define HEARTBEAT_INTERVAL 5        // Heartbeat interval in seconds
#define MAX_BUFFER_SIZE 1024        // Buffer size for socket communication
#define CONFIG_FILE "fe_servers.txt"
#define BUFFER_SIZE 4*1024*1024

#define RECONNECT_INTERVAL 5 // Time in seconds between reconnection attempts
#define MAX_RETRIES 10 

using namespace std;
atomic<bool> allow_running(true); 


typedef struct {
    char server_ip[INET_ADDRSTRLEN];
    int server_port;
} ServerInfo;

struct ClientInfo {
    int client_sockfd;
    sockaddr_in client_addr;
};

// UTILS
void* heartbeat_sender(void* arg);
void signal_handler(int signum);
void* handle_client(void* arg);
void dispatch_request(int client_sockfd, const Request& request);
string get_current_date();
bool decode_chunked_body(std::istringstream& request_stream, ostream& output);
bool ends_with(const std::string& str, const std::string& suffix);
string handle_static_file(const string& path);
std::map<std::string, std::string> parse_form_data(const std::string& body);


// FILE UPLOAD
std::string get_disposition_value(const std::string& disposition, const std::string& key);
std::string sanitize_path(const std::string& path);
bool handle_file_upload(const Request& request, std::string& response_body, FileMetadata& metadata, string& content);
json entry_list_to_json(EntryNode* entry_list);

// JSON HANDLE
// std::map<std::string, std::string> parseJson(const std::string& jsonString);


// AUTH
string generate_session_id();
void post_login(const Request& request, string& set_cookie_header, bool& is_successful);
void post_logout(const Request& request, string& set_cookie_header);
void post_signup(const Request& request, string& set_cookie_header, bool& is_successful);
unordered_map<string, string> parse_cookies(const string& cookie_header);
bool is_logged_in();
void redirect_to_landing_page(int client_sockfd); 
bool post_change_pwd(string row_key_acct, string new_pwd);
void post_login_google(string session_id, string username, string& set_cookie_header, bool& is_successful);

// GET HTML PAGES
string loadPage(const string& filepath, Context& context);
string get_admin_page(const Request& request);
string get_email_page(const Request& request);
string get_email_compose(const Request& request);
string get_storage_page(const Request& request);
string get_landing_page(const Request& request);
string get_404_page(const Request& request);
string get_game_page(const Request& request);



int send_http_post(const char *host, const char *endpoint, int port, const char *post_data, char *response, size_t response_size);

#endif // CLIENT_H
