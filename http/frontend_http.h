#ifndef FRONTEND_HTTP_H
#define FRONTEND_HTTP_H

#include <string>
#include <unordered_map>
#include <sstream> 


using namespace std;

struct Request {
    string method;
    string path;
    string http_version;
    unordered_map<string, string> headers;
    string body;
    unordered_map<string, string> query_params; 

};

bool read_request(int client_sockfd, Request& request);
void send_response(int client_sockfd, const string& status_code, const string& content_type, const string& body, const string& additional_header = "");
string urlDecode(const string& str);
bool parse_request(const string& request_str, Request& request);

#endif // FRONTEND_HTTP_H
