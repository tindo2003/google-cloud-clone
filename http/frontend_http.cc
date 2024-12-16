#include "frontend_http.h"
#include <iostream>
#include <sys/socket.h> // For recv()
#include <cstring>      // For memset

#define BUFFER_SIZE 1024

bool read_request(int client_sockfd, Request& request) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    std::string request_str;

    while ((bytes_received = recv(client_sockfd, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';
        request_str.append(buffer, bytes_received);

        // Check if we've received all headers (look for "\r\n\r\n")
        size_t header_end = request_str.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            header_end += 4; // Move past the "\r\n\r\n"

            // Parse the headers
            if (!parse_request(request_str, request)) {
                std::cerr << "Parsing request failed." << std::endl;
                send_response(client_sockfd, "400 Bad Request", "text/plain", "Bad Request");
                return false;
            }

            // Check for Content-Length to read the rest of the body
            auto content_length_it = request.headers.find("Content-Length");
            if (content_length_it != request.headers.end()) {
                size_t content_length = std::stoul(content_length_it->second);
                size_t already_received = request_str.size() - header_end;

                while (already_received < content_length) {
                    bytes_received = recv(client_sockfd, buffer, sizeof(buffer) - 1, 0);
                    if (bytes_received <= 0) {
                        std::cerr << "Connection closed before full body was received." << std::endl;
                        return false;
                    }
                    buffer[bytes_received] = '\0';
                    request_str.append(buffer, bytes_received);
                    already_received += bytes_received;
                }

                // Extract the body
                request.body = request_str.substr(header_end, content_length);
            }
            return true; // Request headers and body successfully read
        }
    }

    if (bytes_received <= 0) {
        std::cerr << "Error: Connection closed or failed during request reading." << std::endl;
        return false;
    }

    return true;
}

bool parse_request(const string& request_str, Request& request) {
    istringstream input_stream(request_str);
    string line;

    // Parse request line
    if (!getline(input_stream, line)) {
        return false;
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    istringstream line_stream(line);
    if (!(line_stream >> request.method >> request.path >> request.http_version)) {
        return false;
    }
    if (request.http_version.substr(0, 5) != "HTTP/") {
        return false;
    }

    size_t query_pos = request.path.find('?');
    if (query_pos != string::npos) {
        string query_string = request.path.substr(query_pos + 1); 
        request.path = request.path.substr(0, query_pos);             

        // Parse query string into key-value pairs
        istringstream query_stream(query_string);
        string key_value;
        while (getline(query_stream, key_value, '&')) {
            size_t equals_pos = key_value.find('=');
            if (equals_pos != string::npos) {
                // Decode both key and value
                string key = urlDecode(key_value.substr(0, equals_pos));
                string value = urlDecode(key_value.substr(equals_pos + 1));
                request.query_params[key] = value; // Add to query parameters map
            }
        }
    }

    // Cut off everything after '?' in the path
    if (query_pos != string::npos) {
        request.path = request.path.substr(0, query_pos); // Truncate path
    }

    // Parse headers
    while (getline(input_stream, line) && line != "\r") {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            break;
        }
        size_t colon_pos = line.find(':');
        if (colon_pos != string::npos) {
            string header_name = line.substr(0, colon_pos);
            string header_value = line.substr(colon_pos + 1);
            // Trim whitespace
            header_value.erase(0, header_value.find_first_not_of(" \t"));
            header_value.erase(header_value.find_last_not_of(" \t\r\n") + 1);
            request.headers[header_name] = header_value;
        }
    }

    /* Require the Host: header from HTTP 1.1 clients */
    if (request.http_version == "HTTP/1.1" && request.headers.find("Host") == request.headers.end()) {
        return false;
    }
    return true;
}


string urlDecode(const string& str) {
    string decoded;
    char hex_buffer[3] = {0}; // Buffer to hold hex values
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%') {
            if (i + 2 < str.length()) {
                hex_buffer[0] = str[i + 1];
                hex_buffer[1] = str[i + 2];
                decoded += static_cast<char>(strtol(hex_buffer, nullptr, 16));
                i += 2; // Skip the next two hex characters
            }
        } else if (str[i] == '+') {
            decoded += ' '; // Convert '+' to space
        } else {
            decoded += str[i];
        }
    }
    return decoded;
}


void send_response(int client_sockfd, const string& status_code, const string& content_type, const string& body, const string& additional_header) {
    ostringstream response;
    response << "HTTP/1.1 " << status_code << "\r\n";
    response << "Access-Control-Allow-Origin: *\r\n";
    response << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    response << "Access-Control-Allow-Headers: Content-Type\r\n";

    if (!additional_header.empty()) {
        response << additional_header << "\r\n";
    }

    if (!body.empty()) {
        response << "Content-Type: " << content_type << "\r\n"
                 << "Content-Length: " << body.length() << "\r\n"
                 << "\r\n"
                 << body;
    } else {
        response << "\r\n"; 
    }

    string response_str = response.str();
    ssize_t total_sent = 0;
    ssize_t to_send = response_str.size();
    const char* data = response_str.c_str();

    while (total_sent < to_send) {
        ssize_t sent = send(client_sockfd, data + total_sent, to_send - total_sent, 0);
        cout << "amt sent: " << sent << endl;
        if (sent == -1) {
            perror("Error: send");
            break;
        }
        total_sent += sent;
    }
}