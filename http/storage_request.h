#ifndef STORAGE_REQUEST_H
#define STORAGE_REQUEST_H
#include <stdbool.h>
#include <stddef.h>
#include "cache.h"

// Constants
#define HTTP_VERSION "HTTP/1.1"
#define SEPARATOR "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF"
#define SEPARATOR_SIZE 8
// default value
// extern char SERVER_IP[16] = "127.0.0.1";
// extern int SERVER_PORT = 9000;
extern char SERVER_IP[16];
extern int SERVER_PORT;

typedef struct {
    char *key;    // key of header
    char *value;  // value of header
} Header;

// Request（sending to key-value store) structure 
typedef struct {
    // Request Line 
    char method[20];        // Request methods, such as "GET", "PUT", "DELETE"
    char *path;             // Request path, such as "/row_key/column_key"
    char http_version[20];  // HTTP version, for example "HTTP/1.1"
    // Request header
    Header *headers;        // Request header array
    size_t header_count;    // Number of headers requested
    // Request body
    void *body;             // Request body (for storing binary data)
    size_t body_size;       // Request body size
} Request2_keyValue;

// Response
typedef struct {
    bool success;
    size_t content_length;
    void *content; // binary

    bool has_backend_address;  
    char backend_ip[16];     
    int backend_port; 
} ResponseResult;

ResponseResult init_response_result() ;
void add_header(Request2_keyValue *request, const char *key, const char *value);
void free_request(Request2_keyValue *request);
void *build_cput_body_with_separator(const void *expected_value, size_t expected_size, const void *body, size_t body_size, size_t *total_size);
Request2_keyValue *build_request(const char *method, const char *row_key, const char *column_key,
                                 const void *body, size_t body_size,
                                 const void *expected_value, size_t expected_size) ;
char *generate_request_string(Request2_keyValue *request, size_t *out_length);
int send_request(const char *ip_address, int port, Request2_keyValue *request, int *sockfd);
int read_line(int sockfd, char *buffer, size_t max_length);
bool parse_response(int sockfd, ResponseResult *result);
bool get_response(const char *ip_address, int port, const char *method, const char *row_key, const char *column_key,
                     const void *body, size_t body_size, const void *expected_value, size_t expected_size, ResponseResult *result);
                       
#endif 