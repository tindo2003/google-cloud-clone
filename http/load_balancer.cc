#include "load_balancer.h"
using json = nlohmann::json;


int main() {
    const char* config_file_path = "fe_servers.txt"; // Path to the config file
    parse_fe_servers(config_file_path);
    int lb_sockfd;
    struct sockaddr_in lb_addr, client_addr;
    pthread_t hb_thread;
    socklen_t client_len = sizeof(client_addr);
    char buffer[MAX_BUFFER_SIZE];

    // Start the heartfeat monitor thread
    if (pthread_create(&hb_thread, NULL, heartbeat_monitor, NULL) != 0) {
        perror("Failed to create heartfeat monitor thread");
        exit(EXIT_FAILURE);
    }

    // Create socket for client TCP connections
    lb_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lb_sockfd < 0) {
        perror("Load balancer socket creation failed");
        exit(EXIT_FAILURE);
    }

    int option = 1;
    if (setsockopt(lb_sockfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        close(lb_sockfd);
        exit(EXIT_FAILURE);
    }

    // Bind the socket to LB_PORT
    bzero(&lb_addr, sizeof(lb_addr));
    lb_addr.sin_family = AF_INET;
    lb_addr.sin_addr.s_addr = INADDR_ANY;
    lb_addr.sin_port = htons(LB_PORT);

    if (bind(lb_sockfd, (struct sockaddr*)&lb_addr, sizeof(lb_addr)) < 0) {
        perror("Load balancer bind failed");
        close(lb_sockfd);
        exit(EXIT_FAILURE);
    }

    if (listen(lb_sockfd, 10) < 0) {
        perror("Load balancer listen failed");
        close(lb_sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Load balancer started on port %d\n", LB_PORT);

    while (1) {
        int client_fd = accept(lb_sockfd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("accept failed");
            continue;
        }

        Request request;
        string request_body;

        // Read and parse the client's HTTP request
        if (!read_request(client_fd, request)) {
            // If `read_request` fails, the response is already sent, so close the connection
            close(client_fd);
            continue; // Skip further processing for this client
        }

        cerr << "Received " << request.method << " request for " << request.path << endl;

        // Check for unsupported methods
        if (request.method != "GET" && request.method != "OPTIONS") {
            // Respond with 405 Method Not Allowed for unsupported methods
            send_response(client_fd, "405 Method Not Allowed", "text/plain", "Only GET and OPTIONS methods are supported.");
            close(client_fd);
            continue; // Skip further processing for this client
        }

        if (request.path == "/get_servers") {
            // Handle `/get_servers` endpoint
            handle_get_servers_request(client_fd);
            close(client_fd);
            continue; // Skip further processing for this client
        }

        // Default behavior: Redirect to an available frontend server
        FE_Server* selected_server = get_next_server();

        if (selected_server) {
            // Construct the HTTP redirect header using ip_addr and port_no
            string redirect_header =
                "Location: http://" + selected_server->ip_addr + ":" + to_string(selected_server->port_no);

            // Send the HTTP redirect response to the client
            send_response(client_fd, "307 Temporary Redirect", "", "", redirect_header);
            cout << "Redirected client to FE server " << selected_server->ip_addr << ":" << selected_server->port_no << endl;
        } else {
            // No servers available
            send_response(client_fd, "503 Service Unavailable", "text/plain",
                        "No frontend servers are available. Please try again later.", "");
            cerr << "No FE servers available, sent 503 to client" << endl;
        }

        close(client_fd); // Close the client connection after sending the response
    }


    close(lb_sockfd);
    pthread_join(hb_thread, NULL);
    return 0;
}

void parse_fe_servers(const char* config_file_path) {
    FILE* config_file = fopen(config_file_path, "r");
    if (!config_file) {
        perror("Error opening configuration file");
        exit(EXIT_FAILURE);
    }

    char line[256];
    while (fgets(line, sizeof(line), config_file)) {
        char* address = strtok(line, ":\n");
        char* port_str = strtok(NULL, ":\n");

        if (address && port_str) {
            int port = atoi(port_str);

            // Add to the set of all possible servers
            possible_fe_servers.insert({string(address), port});
        } else {
            fprintf(stderr, "Invalid format in configuration file\n");
            fclose(config_file);
            exit(EXIT_FAILURE);
        }
    }

    fclose(config_file);
}


void remove_fe_server(int index) {
    close(fe_servers[index].sockfd);
    for (int i = index; i < num_fe_servers - 1; i++) {
        fe_servers[i] = fe_servers[i + 1];
    }
    num_fe_servers--;
}

void add_fe_server(int new_sockfd, const char* ip_addr, int port_no) {
    if (num_fe_servers >= MAX_FE_SERVERS) {
        fprintf(stderr, "Maximum number of frontend servers reached!\n");
        close(new_sockfd);
        return;
    }

    fe_servers[num_fe_servers].sockfd = new_sockfd;
    fe_servers[num_fe_servers].last_heartbeat = time(NULL);
    fe_servers[num_fe_servers].ip_addr = string(ip_addr);
    fe_servers[num_fe_servers].port_no = port_no;

    num_fe_servers++;
    printf("Frontend server added: %s:%d\n", ip_addr, port_no);
}

void* heartbeat_monitor(void* arg) {
    struct pollfd fds[MAX_FE_SERVERS + 1];
    struct sockaddr_in hb_addr;
    char buffer[256]; // Increased size to accommodate IP:port message
    time_t now;

    int hb_sockfd;
    hb_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (hb_sockfd < 0) {
        perror("Heartbeat socket creation failed");
        pthread_exit(NULL);
    }

    int option = 1;
    if (setsockopt(hb_sockfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        close(hb_sockfd);
        exit(EXIT_FAILURE);
    }

    memset(&hb_addr, 0, sizeof(hb_addr));
    hb_addr.sin_family = AF_INET;
    hb_addr.sin_addr.s_addr = INADDR_ANY;
    hb_addr.sin_port = htons(HEARTBEAT_PORT);

    if (bind(hb_sockfd, (struct sockaddr*)&hb_addr, sizeof(hb_addr)) < 0) {
        perror("Heartbeat socket bind failed");
        close(hb_sockfd);
        pthread_exit(NULL);
    }

    if (listen(hb_sockfd, SOMAXCONN) < 0) {
        perror("Heartbeat socket listen failed");
        close(hb_sockfd);
        pthread_exit(NULL);
    }

    printf("Heartbeat monitor started on port %d\n", HEARTBEAT_PORT);

    while (1) {
        // Prepare pollfd structure
        fds[0].fd = hb_sockfd;
        fds[0].events = POLLIN;
        for (int i = 0; i < num_fe_servers; i++) {
            fds[i + 1].fd = fe_servers[i].sockfd;
            fds[i + 1].events = POLLIN;
        }

        // Poll with a timeout equal to HEARTBEAT_INTERVAL
        int ret = poll(fds, num_fe_servers + 1, HEARTBEAT_INTERVAL * 1000);
        if (ret < 0) {
            perror("poll failed");
            continue;
        }

        now = time(NULL);

        // Check for new connections on the bind socket
        if (fds[0].revents & POLLIN) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int new_sockfd = accept(hb_sockfd, (struct sockaddr*)&client_addr, &client_len);
            if (new_sockfd < 0) {
                perror("accept failed");
                continue;
            }

            // Step 1: Receive the length of the message
            int32_t net_len;
            ssize_t bytes_read = recv(new_sockfd, &net_len, sizeof(net_len), 0);
            if (bytes_read <= 0) {
                perror("Failed to receive message length");
                close(new_sockfd);
                continue;
            }
            int msg_len = ntohl(net_len);

            if (msg_len <= 0 || msg_len >= sizeof(buffer)) {
                fprintf(stderr, "Invalid message length received: %d\n", msg_len);
                close(new_sockfd);
                continue;
            }

            // Step 2: Receive the actual IP:port message
            bytes_read = recv(new_sockfd, buffer, msg_len, 0);
            if (bytes_read > 0) {
                buffer[bytes_read] = '\0'; // Null-terminate the message

                // Step 3: Parse the IP:port message
                char* ip_addr = strtok(buffer, ":");
                char* port_str = strtok(NULL, ":");
                if (ip_addr && port_str) {
                    int port_no = atoi(port_str);
                    add_fe_server(new_sockfd, ip_addr, port_no); // Add the new server
                    printf("New frontend server registered: %s:%d\n", ip_addr, port_no);
                } else {
                    fprintf(stderr, "Invalid initial heartbeat format: %s\n", buffer);
                    close(new_sockfd);
                }
            } else {
                perror("Failed to receive initial message");
                close(new_sockfd);
            }
        }


        // Check heartbeat messages from frontend servers
        for (int i = 0; i < num_fe_servers; i++) {
            if (fds[i + 1].revents & POLLIN) {
                char hb;
                ssize_t bytes_read = recv(fe_servers[i].sockfd, &hb, 1, 0);

                if (bytes_read > 0) {
                    // Update the last heartbeat timestamp
                    fe_servers[i].last_heartbeat = now;
                    printf("Heartbeat received from server: %s:%d\n",
                           fe_servers[i].ip_addr.c_str(), fe_servers[i].port_no);
                } else {
                    perror("recv failed or connection closed");
                    printf("Removing server %s:%d due to connection closure\n",
                           fe_servers[i].ip_addr.c_str(), fe_servers[i].port_no);
                    remove_fe_server(i);
                    i--;  // Adjust index due to removal
                }
            }
        }

        // Check for servers that missed the heartbeat threshold
        for (int i = 0; i < num_fe_servers; i++) {
            if (difftime(now, fe_servers[i].last_heartbeat) > THRESHOLD) {
                printf("Frontend server %s:%d marked as down (no heartbeat)\n",
                       fe_servers[i].ip_addr.c_str(), fe_servers[i].port_no);
                remove_fe_server(i);
                i--;  // Adjust index due to removal
            }
        }
    }

    return NULL;
}


FE_Server* get_next_server() {
    pthread_mutex_lock(&fe_mutex); // Lock the mutex

    if (num_fe_servers == 0) {
        pthread_mutex_unlock(&fe_mutex); // Unlock before returning
        cerr << "no available fe to pick" << endl;
        return nullptr; // No servers available
    }

    // Select the next server in round-robin fashion
    current_server_index = (current_server_index + 1) % num_fe_servers;
    FE_Server* selected_server = &fe_servers[current_server_index];

    pthread_mutex_unlock(&fe_mutex); // Unlock the mutex
    return selected_server;
}

void handle_get_servers_request(int client_fd) {
    pthread_mutex_lock(&fe_mutex);

    // Create JSON arrays for alive and unalive servers
    json alive_servers = json::array();
    json unalive_servers = json::array();

    // Add alive servers from fe_servers array
    for (int i = 0; i < num_fe_servers; i++) {
        json server_json = {
            {"address", fe_servers[i].ip_addr},
            {"port", fe_servers[i].port_no},
            {"is_alive", true}
        };

        alive_servers.push_back(server_json);
    }

    // Add unalive servers by checking against possible_fe_servers
    for (const auto& server : possible_fe_servers) {
        string address = server.first;
        int port = server.second;

        // Check if the server is not in fe_servers
        bool found = false;
        for (int i = 0; i < num_fe_servers; i++) {
            if (address == fe_servers[i].ip_addr && port == fe_servers[i].port_no) {
                found = true;
                break;
            }
        }

        if (!found) {
            json server_json = {
                {"address", address},
                {"port", port},
                {"is_alive", false}
            };

            unalive_servers.push_back(server_json);
        }
    }

    pthread_mutex_unlock(&fe_mutex);

    // Construct the final JSON response
    json json_response = {
        {"alive_servers", alive_servers},
        {"unalive_servers", unalive_servers}
    };

    // Send the response
    string response_body = json_response.dump();
    //cerr << "sending" << response_body << endl;
    send_response(client_fd, "200 OK", "application/json", response_body);
}
