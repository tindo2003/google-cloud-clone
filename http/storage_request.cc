#include "storage_request.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <arpa/inet.h> 
#include <unistd.h> 
#include "cache.h"
#include <string>
#include <iostream>
#include "storage_api.h"


char SERVER_IP[16] = "127.0.0.1";
int SERVER_PORT = 9000;

ResponseResult init_response_result() {
    ResponseResult result = {
        .success = false,
        .content_length = 0,
        .content = NULL
    };
    return result;
}

void add_header(Request2_keyValue *request, const char *key, const char *value) {
    request->headers = (Header *) realloc(request->headers, sizeof(Header) * (request->header_count + 1));
    if (!request->headers) {
        fprintf(stderr, "Memory allocation failed for headers\n");
        return;
    }
    request->headers[request->header_count].key = strdup(key);
    request->headers[request->header_count].value = strdup(value);
    request->header_count++;
}

// Free request
void free_request(Request2_keyValue *request) {
    if (!request) return;
    if (request->path) free(request->path);
    for (size_t i = 0; i < request->header_count; i++) {
        if (request->headers[i].key) free(request->headers[i].key);
        if (request->headers[i].value) free(request->headers[i].value);
    }
    if (request->headers) free(request->headers);
    if (request->body) free(request->body);
    free(request);
}

void *build_cput_body_with_separator(const void *expected_value, size_t expected_size, const void *body, size_t body_size, size_t *total_size) {
    // Expect the size of the combined request body: expected_value + separator + body
    *total_size = expected_size + SEPARATOR_SIZE + body_size;
    void *combined_body = malloc(*total_size);
    if (!combined_body) {
        fprintf(stderr, "Memory allocation failed for combined body with separator\n");
        return NULL;
    }
    char *ptr = (char *)combined_body;
    //  Put the expected_value
    memcpy(ptr, expected_value, expected_size);
    ptr += expected_size;

    //Put SEPARATOR
    memcpy(ptr, SEPARATOR, SEPARATOR_SIZE);
    ptr += SEPARATOR_SIZE;
    // Put body
    memcpy(ptr, body, body_size);
    return combined_body;
}

// Build request function
Request2_keyValue *build_request(const char *method, const char *row_key, const char *column_key,
                                 const void *body, size_t body_size,
                                 const void *expected_value, size_t expected_size) {
    // Allocate and initialize the Request
    Request2_keyValue *request = (Request2_keyValue *)malloc(sizeof(Request2_keyValue));
    if (!request) {
        fprintf(stderr, "Memory allocation failed for request\n");
        return NULL;
    }
    memset(request, 0, sizeof(Request2_keyValue)); // Initialize

    // Set the HTTP version
    strncpy(request->http_version, HTTP_VERSION, sizeof(request->http_version) - 1);
    request->http_version[sizeof(request->http_version) - 1] = '\0';

    // Set Request method
    strncpy(request->method, method, sizeof(request->method) - 1);
    request->method[sizeof(request->method) - 1] = '\0';

    // Build request path
    size_t path_length = strlen(row_key) + strlen(column_key) + 3; // "/" + row_key + "/" + column_key + "\0"
    request->path = (char *)malloc(path_length);
    if (!request->path) {
        fprintf(stderr, "Memory allocation failed for path\n");
        free_request(request);
        return NULL;
    }
    snprintf(request->path, path_length, "/%s/%s", row_key, column_key);

    // debug message
    std::string path_as_string = std::string(request->path);
    std::cout << "the path is: " + path_as_string << std::endl;

    // initialize the Request Header
    request->headers = NULL;
    request->header_count = 0;

    // If it is a GET or DELETE method, the body should be empty
    if (strcmp(method, "GET") == 0 || strcmp(method, "DELETE") == 0) {
        if (body != NULL || body_size > 0) {
            fprintf(stderr, "Error: GET/DELETE request should not have a body\n");
            free_request(request);
            return NULL;
        }
        request->body = NULL;
        request->body_size = 0;
        //  Add a Content-Length :0  header
        add_header(request, "Content-Length", "0");
    }

    if (strcmp(method, "PUT") == 0) {
        if (body == NULL || body_size <= 0) {
            fprintf(stderr, "Error: Body cannot be NULL or have zero size for method %s\n", method);
            free_request(request);
            return NULL;
        }
        // Set request body
        request->body = malloc(body_size);
        if (!request->body) {
            fprintf(stderr, "Memory allocation failed for body\n");
            free_request(request);
            return NULL;
        }
        memcpy(request->body, body, body_size);
        request->body_size = body_size;

        // add Content-Length header
        char content_length[50];
        snprintf(content_length, sizeof(content_length), "%zu", body_size);
        add_header(request, "Content-Length", content_length);

        // add Content-Type header -binary data
        add_header(request, "Content-Type", "application/octet-stream");
    }

    if (strcmp(method, "CPUT") == 0) {
        if (expected_value == NULL || expected_size <= 0 || body == NULL || body_size <= 0) {
            fprintf(stderr, "Error: Both expected_value and body are required for CPUT method\n");
            free_request(request);
            return NULL;
        }
        // Combined expected_value and body to the request body
        size_t total_size;
        void *combined_body = build_cput_body_with_separator(expected_value, expected_size, body, body_size, &total_size);
        if (combined_body == NULL) {
            fprintf(stderr, "Failed to build combined CPUT body\n");
            return NULL; 
        }
        // Build CPUT Request
        request->body = combined_body;
        request->body_size = total_size;

        // Add Content-Length header
        char content_length[50];
        snprintf(content_length, sizeof(content_length), "%zu", total_size);
        add_header(request, "Content-Length", content_length);

        // Add Content-Type header
        add_header(request, "Content-Type", "application/octet-stream");
    }
    return request;
}

char *generate_request_string(Request2_keyValue *request, size_t *out_length) {
    // Calculates the length of the request line
    size_t request_line_length = strlen(request->method) + 1 + strlen(request->path) + 1 + strlen(request->http_version) + 2; // method SP path SP version CRLF

    // Calculates the length of the request header
    size_t headers_length = 0;
    for (size_t i = 0; i < request->header_count; i++) {
        headers_length += strlen(request->headers[i].key) + 2 + strlen(request->headers[i].value) + 2; // key: value CRLF
    }

    size_t total_length = request_line_length + headers_length + 2; // Plus the CRLF of the blank line

    // If there is a request body, add its length
    if (request->body && request->body_size > 0) {
        total_length += request->body_size;
    }

    // Allocate memory
    char *request_str = (char *)malloc(total_length + 1); // +1 for '\0'
    if (!request_str) {
        fprintf(stderr, "Memory allocation failed for request string\n");
        return NULL;
    }

    // Build request string
    size_t offset = 0;
    // add request line
    offset += snprintf(request_str + offset, total_length - offset + 1, "%s %s %s\r\n", request->method, request->path, request->http_version);

    // add request header
    for (size_t i = 0; i < request->header_count; i++) {
        offset += snprintf(request_str + offset, total_length - offset + 1, "%s: %s\r\n", request->headers[i].key, request->headers[i].value);
    }

    // add an empty line before request body
    offset += snprintf(request_str + offset, total_length - offset + 1, "\r\n");

    // add request body
    if (request->body && request->body_size > 0) {
        memcpy(request_str + offset, request->body, request->body_size);
        offset += request->body_size;
    }

    // Add a string terminator
    request_str[offset] = '\0';

    if (out_length) {
        *out_length = offset;
    }
    return request_str;
}
// TODO For every request, we currently create a new connection. Moving forward, we need to reuse sockets by implementing a socket pool.
int send_request(const char *ip_address, int port, Request2_keyValue *request, int *sockfd) {
    struct sockaddr_in server_addr;

    // Create socket
    if ((*sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        return -1;
    }

    // Set server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip_address, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid IP address: %s\n", ip_address);
        close(*sockfd);
        return -1;
    }

    // Connect to server
    if (connect(*sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("connect");
        close(*sockfd);
        return -1;
    }

    // Generate request
    size_t request_length;
    char *request_str = generate_request_string(request, &request_length);
    if (!request_str) {
        fprintf(stderr, "Failed to generate request string\n");
        close(*sockfd);
        return -1;
    }

    //fprintf(stderr, "[send_request] request is : %s\n", request_str);


    // Send Request
    ssize_t total_sent = 0;
    while (total_sent < request_length) {
        ssize_t sent = send(*sockfd, request_str + total_sent, request_length - total_sent, 0);
        if (sent == -1) {
            perror("send");
            free(request_str);
            close(*sockfd);
            return -1;
        }
        total_sent += sent;
    }
    free(request_str);
    // Socket is now open for further operations
    return 0;
}


int read_line(int sockfd, char *buffer, size_t max_length) {
    size_t total_read = 0;
    while (total_read < max_length - 1) {
        char c;
        ssize_t n = read(sockfd, &c, 1);
        if (n <= 0) {
            // Error or connection closed
            return -1;
        }
        buffer[total_read++] = c;
        if (total_read >= 2 && buffer[total_read - 2] == '\r' && buffer[total_read - 1] == '\n') {
            buffer[total_read] = '\0';
            return total_read;
        }
    }
    buffer[total_read] = '\0';
    return total_read;
}

bool parse_response(int sockfd, ResponseResult *result) {
    char buffer[4096];
    int n;
    // Initialize result
    result->success = false;
    result->content_length = 0;
    result->content = NULL;

    // Read status line

    n = read_line(sockfd, buffer, sizeof(buffer));
    if (n <= 0) {
        fprintf(stderr, "[parse_response] Error: Failed to read status line\n");
        return false; // Error reading from socket
    }

    // Debug: Full status line
    //fprintf(stderr, "[parse_response] Full status line: %s\n", buffer);

    // Parse status line
    char *http_version = strtok(buffer, " ");
    char *status_code = strtok(NULL, " ");
    char *status_message = strtok(NULL, "\r\n");

    // Debug: Parsed components
    fprintf(stderr, "[parse_response] Parsed HTTP version: %s\n", http_version ? http_version : "NULL");
    fprintf(stderr, "[parse_response] Parsed status code: %s\n", status_code ? status_code : "NULL");
    fprintf(stderr, "[parse_response] Parsed status message: %s\n", status_message ? status_message : "NULL");

    if (status_message == NULL || status_code == NULL) {
        fprintf(stderr, "[parse_response] Error: Malformed status line\n");
        return false; // Malformed status line
    }
    int status_code_num = atoi(status_code);

    if (status_code_num == 200) {
        result->success = true; 
        fprintf(stderr, "[parse_response] Success: HTTP status code is 200\n");
    } else {
        result->success = false; 
        fprintf(stderr, "[parse_response] Failure: HTTP status code is %d\n", status_code_num);
    }

    // Read headers until empty line
    size_t content_length = 0;
    while (1) {
        n = read_line(sockfd, buffer, sizeof(buffer));
        if (n <= 0) {
            fprintf(stderr, "[parse_response] Error: Failed to read headers\n");
            return false; // Error reading from socket
        }

        if (strcmp(buffer, "\r\n") == 0) {
            fprintf(stderr, "[parse_response] End of headers\n");
            break; // End of headers
        }

        // Debug: Header line
        fprintf(stderr, "[parse_response] Header line: %s\n", buffer);

        // Parse Content-Length header
        if (strncmp(buffer, "Content-Length: ", 16) == 0) {
            content_length = atoi(buffer + 16);
            result->content_length = content_length;
            fprintf(stderr, "[parse_response] Parsed Content-Length: %zu\n", content_length);
        }

        // Parse Backend-Address header
        if (strncmp(buffer, "Backend-Address: ", 17) == 0) {
            char *backend_address = buffer + 17;
            char ip[16];
            int port;
            if (sscanf(backend_address, "%15[^:]:%d", ip, &port) == 2) {
                strncpy(result->backend_ip, ip, sizeof(result->backend_ip));
                result->backend_port = port;
                result->has_backend_address = true;
                fprintf(stderr, "[parse_response] Found Backend-Address: %s:%d\n", result->backend_ip, result->backend_port);
            } else {
                fprintf(stderr, "[parse_response] Error: Failed to parse Backend-Address\n");
            }
        }
    }

    // Read content if any
    if (content_length > 0) {
        result->content = malloc(content_length);
        if (result->content == NULL) {
            fprintf(stderr, "[parse_response] Error: Memory allocation failed for content\n");
            return false; // Memory allocation failed
        }

        size_t total_read = 0;
        while (total_read < content_length) {
            ssize_t bytes_read = read(sockfd, (char *)result->content + total_read, content_length - total_read);
            if (bytes_read <= 0) {
                // Error or connection closed
                fprintf(stderr, "[parse_response] Error: Failed to read content, bytes_read = %zd\n", bytes_read);
                free(result->content);
                result->content = NULL;
                return false;
            }
            total_read += bytes_read;

            // Debug: Content read progress
            fprintf(stderr, "[parse_response] Content read progress: %zu/%zu bytes\n", total_read, content_length);
        }

        // Debug: Full content
        //fprintf(stderr, "[parse_response] response length is : '%zu'  Full response content: '%s'\n", result->content_length, binary_to_string(result->content, result->content_length ) );
    } else {
        fprintf(stderr, "[parse_response] No content to read (Content-Length = 0)\n");
    }

    return true;
}



bool get_response(const char *ip_address, int port, const char *method, const char *row_key, const char *column_key,
                     const void *body, size_t body_size, const void *expected_value, size_t expected_size, ResponseResult *result) {
    // Build request
    Request2_keyValue *request = build_request(method, row_key, column_key, body, body_size, expected_value, expected_size);
    if (!request) {
        fprintf(stderr, "Failed to build request\n");
        return false;
    }

    // Send request and get socket
    int sockfd;
    if (send_request(ip_address, port, request, &sockfd) == -1) {
        fprintf(stderr, "Failed to send request\n");
        free_request(request);
        return false;
    }

    // Receive response using the same socket
    if (!parse_response(sockfd, result)) {
        fprintf(stderr, "Error: Failed to parse response from server.\n");
        close(sockfd);  

        return false;
    }
    
    close(sockfd);  

    int sockfd2;

    if (result->has_backend_address) {
        fprintf(stderr, "[get_response] Detected backend address: %s:%d. Resending request.\n",
                result->backend_ip, result->backend_port);

        if (send_request(result->backend_ip, result->backend_port, request, &sockfd2) == -1) {
            fprintf(stderr, "Failed to send request to backend address\n");
            free_request(request);
            return false;
        }

        if (!parse_response(sockfd2, result)) {
            fprintf(stderr, "Error: Failed to parse response from backend.\n");
            close(sockfd2);
            free_request(request);
            return false;
        }
        close(sockfd2);
    }
    
    free_request(request);
    
    // only result->success==true, we can get response successful
    if (result->success==false){
        fprintf(stderr, "Error: Response indicates failure (success = false).\n");
        return false;
    }else{
        fprintf(stderr, "Debug: Response indicates success (success = true).\n");
        return true;
    }
    
}




