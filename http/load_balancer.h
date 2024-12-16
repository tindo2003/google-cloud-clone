#ifndef LOAD_BALANCER_H
#define LOAD_BALANCER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <time.h>
#include <vector>
#include <iostream>
#include "nlohmann/json.hpp"
#include "frontend_http.h"
#include <poll.h>
#include <set>

#define LB_PORT 8000                // Load balancer listens on this port
#define HEARTBEAT_PORT 9090         // Heartats are sent to this port
#define HEARTBEAT_INTERVAL 5        // Heartat interval in seconds
#define THRESHOLD (HEARTBEAT_INTERVAL * 3) // Threshold to consider a server down
#define MAX_FE_SERVERS 10           // Maximum numr of frontend servers
#define MAX_BUFFER_SIZE 1024        // Buffer size for socket communication
#define CONFIG_FILE "fe_servers.txt"   // Configuration file with FE server addresses

using namespace std; 

typedef struct {
    int sockfd;                  // Socket file descriptor for the server
    time_t last_heartbeat;       // Timestamp of the last received heartbeat
    string ip_addr;
    int port_no; 
} FE_Server;

FE_Server fe_servers[MAX_FE_SERVERS];
int num_fe_servers = 0;
std::set<std::pair<std::string, int>> possible_fe_servers; // All possible servers



int last_server_index = -1;
pthread_mutex_t fe_mutex = PTHREAD_MUTEX_INITIALIZER;
FE_Server* get_next_server();
size_t current_server_index = 0;

void* heartbeat_monitor(void* arg);
void parse_fe_servers(const char* config_file_path);
void handle_get_servers_request(int client_fd);
#endif // CLIENT_H