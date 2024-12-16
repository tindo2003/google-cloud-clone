#include "server.h"
#include "cache.h"
#include "storage_api.h"
#include "email_api.h"
#include "storage_request.h"
using json = nlohmann::json;
using namespace std;
std::string sanitizeJsonString(const std::string& input);

Context context;

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <server_index>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int server_index = atoi(argv[1]) - 1; // Zero-based index

    // Read the configuration file to get the bind address and port
    FILE* config_file = fopen("fe_servers.txt", "r");
    //  /home/cis5050/Desktop/24fa-CIS5050-tindo2003/final_project/fa24-cis5050-T07/http/fe_servers.txt
    if (!config_file) {
        perror("Failed to open configuration file");
        exit(EXIT_FAILURE);
    }

    char line[MAX_BUFFER_SIZE];
    int current_index = 0;
    char server_address[INET_ADDRSTRLEN];
    int server_port = 0;

    while (fgets(line, sizeof(line), config_file)) {
        if (current_index == server_index) {
            // Parse the address and port
            char* token = strtok(line, ":\n");
            if (token != NULL) {
                strcpy(server_address, token);
                token = strtok(NULL, ":\n");
                if (token != NULL) {
                    server_port = atoi(token);
                } else {
                    fprintf(stderr, "Invalid format in configuration file\n");
                    exit(EXIT_FAILURE);
                }
            }
            break;
        }
        current_index++;
    }

    fclose(config_file);

    if (server_port == 0) {
        fprintf(stderr, "Server index out of range\n");
        exit(EXIT_FAILURE);
    }

    printf("Starting FE server at %s:%d\n", server_address, server_port);
    signal(SIGINT, signal_handler);

    int fe_sockfd;
    struct sockaddr_in fe_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[MAX_BUFFER_SIZE];
    pthread_t hb_thread;

    ServerInfo server_info;
    strcpy(server_info.server_ip, server_address);
    server_info.server_port = server_port;

    // Start the heartbeat sender thread
    if (pthread_create(&hb_thread, NULL, heartbeat_sender, &server_info) != 0) {
        perror("Failed to create heartbeat sender thread");
        exit(EXIT_FAILURE);
    }

    // Create socket for client connections
    fe_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (fe_sockfd < 0) {
        perror("Frontend server socket creation failed");
        exit(EXIT_FAILURE);
    }
    int option = 1;
    if (setsockopt(fe_sockfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        close(fe_sockfd);
        exit(EXIT_FAILURE);
    }

    // Bind the socket to FE_SERVER_PORT
    bzero(&fe_addr, sizeof(fe_addr));
    fe_addr.sin_family = AF_INET;
    fe_addr.sin_addr.s_addr = INADDR_ANY;
    fe_addr.sin_port = htons(server_port);

    if (bind(fe_sockfd, (struct sockaddr*)&fe_addr, sizeof(fe_addr)) < 0) {
        perror("Frontend server bind failed");
        close(fe_sockfd);
        exit(EXIT_FAILURE);
    }

    if (listen(fe_sockfd, 10) < 0) {
        perror("Frontend server listen failed");
        close(fe_sockfd);
        exit(EXIT_FAILURE);
    }


    while (keep_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_sockfd = accept(fe_sockfd, (struct sockaddr*)&client_addr, &client_len);
        if (client_sockfd < 0) {
            if (keep_running) {
                perror("Error: accept");
            }
            continue;
        }

        // Create client info structure
        ClientInfo* client_info = new ClientInfo();
        client_info->client_sockfd = client_sockfd;
        client_info->client_addr = client_addr;

        // Create a new thread to handle the client
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, client_info) != 0) {
            perror("Error: pthread_create");
            close(client_sockfd);
            delete client_info;
            continue;
        } else {
            pthread_detach(thread_id); 
        }
        // try to manage the lifecycle. 
    }

    // Close the server socket
    close(fe_sockfd);
    std::cout << "Server shutting down." << std::endl;
}


void* heartbeat_sender(void* arg) {
    signal(SIGPIPE, SIG_IGN);
    int retry_count = 0;
    int hb_sockfd;
    struct sockaddr_in lb_addr;
    char heartbeat_msg[MAX_BUFFER_SIZE];

    ServerInfo* server_info = (ServerInfo*)arg;
    char* server_ip = server_info->server_ip;
    int server_port = server_info->server_port;
    cerr << "inside heartbeat sender " << endl;

    // Connect to the load balancer's heartbeat port
    while (keep_running) {
        if (!allow_running) {
            fprintf(stderr, "Heartbeat paused. Waiting for RESTART command.\n");
            sleep(1); // Polling interval while paused
            continue;
        }
        hb_sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (hb_sockfd < 0) {
            perror("Heartbeat socket creation failed");
            pthread_exit(NULL);
        }

        // prevent addr in use error 
        int option = 1;
        if (setsockopt(hb_sockfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) < 0) {
            perror("setsockopt(SO_REUSEADDR) failed");
            close(hb_sockfd);
            exit(EXIT_FAILURE);
        }

        bzero(&lb_addr, sizeof(lb_addr));
        lb_addr.sin_family = AF_INET;
        lb_addr.sin_port = htons(HEARTBEAT_PORT);

        if (inet_pton(AF_INET, LB_HEARTBEAT_IP, &lb_addr.sin_addr) <= 0) {
            perror("Invalid address/ Address not supported");
            close(hb_sockfd);
            pthread_exit(NULL);
        }


        while (connect(hb_sockfd, (struct sockaddr*)&lb_addr, sizeof(lb_addr)) < 0) {
            perror("Connection to load balancer failed");
            if (++retry_count >= MAX_RETRIES) {
                fprintf(stderr, "Max retries reached. Exiting heartbeat sender.\n");
                close(hb_sockfd);
                pthread_exit(NULL);
            }
            sleep(RECONNECT_INTERVAL);
        }
        retry_count = 0;

        printf("Connected to load balancer at %s:%d for heartbeats\n", LB_HEARTBEAT_IP, HEARTBEAT_PORT);

        snprintf(heartbeat_msg, sizeof(heartbeat_msg), "%s:%d", server_ip, server_port);
        int msg_len = strlen(heartbeat_msg);
        int32_t net_len = htonl(msg_len);

        // Send the length first
        if (send(hb_sockfd, &net_len, sizeof(net_len), 0) < 0) {
            perror("Failed to send initial message length");
            close(hb_sockfd);
            pthread_exit(NULL);
        }
        // Send the initial message
        if (send(hb_sockfd, heartbeat_msg, msg_len, 0) < 0) {
            perror("Failed to send initial message");
            close(hb_sockfd);
            pthread_exit(NULL);
        }

        while (keep_running) {
            if (!allow_running) {
                fprintf(stderr, "Heartbeat paused. Waiting for RESTART command.\n");
                sleep(1); // Polling interval while paused
                continue;
            }
            char hb = '1';
            if (send(hb_sockfd, &hb, 1, 0) < 0) {
                perror("Failed to send heartbeat");
                close(hb_sockfd);
                break; // Exit heartbeat loop to reconnect
            }
            sleep(HEARTBEAT_INTERVAL);
            fprintf(stderr, "sending heartbeat\n");
        }
    }
    close(hb_sockfd);
    cerr << "stop sending hb " << endl;
    pthread_exit(NULL);
}


void signal_handler(int signum) {
    keep_running = false;
    exit(0);
}


void* handle_client(void* arg) {
    ClientInfo* client_info = (ClientInfo*)(arg);
    int client_sockfd = client_info->client_sockfd;
    sockaddr_in client_addr = client_info->client_addr;

    // Clean up client_info
    delete client_info;

    Request request;

    // Use the read_request function
    if (!read_request(client_sockfd, request)) {
        close(client_sockfd);
        pthread_exit(NULL);
    }

    cerr << "Received " << request.method << " request for " << request.path << std::endl;
    cerr << request.body << endl;

    // Dispatch the request to the appropriate handler
    dispatch_request(client_sockfd, request);

    close(client_sockfd);
    pthread_exit(NULL); 
}

bool decode_chunked_body(istringstream& request_stream, std::ostream& output) {
    std::string line;
    int chunk_size = 0;

    while (true) {
        // Read the chunk size line
        if (!std::getline(request_stream, line) || line.empty()) {
            return false;  // Failure: Couldn't read chunk size
        }
        line.pop_back();  // Remove '\r'
        
        // Convert the chunk size from hexadecimal to integer
        chunk_size = strtoul(line.c_str(), nullptr, 16);
        if (chunk_size == 0) break;  // End of chunks

        char buffer[BUFFER_SIZE];
        int bytes_remaining = chunk_size;

        // Read the chunk data
        while (bytes_remaining > 0 && request_stream.read(buffer, std::min(bytes_remaining, BUFFER_SIZE))) {
            output.write(buffer, request_stream.gcount());
            bytes_remaining -= request_stream.gcount();
        }

        // Check if we couldn't read the expected chunk size
        if (bytes_remaining > 0) {
            return false;  // Failure: Couldn't read full chunk
        }

        // Consume the trailing CRLF after each chunk
        if (!std::getline(request_stream, line) || line != "\r") {
            return false;  // Failure: Couldn't read CRLF
        }
    }

    // Optional: Handle trailer headers here if needed

    return true;  // Success
}


void dispatch_request(int client_sockfd, const Request& request) {
    string response_body;
    string status_code = "200 OK";
    std::string email_status_code = "200 OK";
    string content_type = "text/html";
    char *storage_status_code = strdup("200 OK");
    const char *existing_data = NULL;

    cout << request.method << request.path << endl;

    if (request.headers.find("Cookie") != request.headers.end() && !request.headers.at("Cookie").empty()) {
        string cookie_header = request.headers.at("Cookie");
        auto cookies = parse_cookies(cookie_header);
        context.variables["sessionid"] = cookies["sessionid"];
        context.variables["username"] = cookies["user"];

        //Construct account key
        string account_key = cookies["user"] + "#account";

        ResponseResult result = init_response_result();
        if (!get_response(SERVER_IP, SERVER_PORT, "GET", account_key.c_str(), "sid", NULL, 0, NULL, 0, &result)) {
            std::cerr << "[dispatch_request] Error: Get(username, sid) failed" << std::endl;
            context.variables["sessionid"] = "";
            context.variables["username"] = "";
            //return;
        }

        // Convert binary response to string
        char *existing_data_cstr = binary_to_string(result.content, result.content_length);
        if (existing_data_cstr == NULL) {
            std::cerr << "[dispatch_request] Error: binary_to_string failed and returned NULL." << std::endl;
            context.variables["sessionid"] = "";
            context.variables["username"] = "";
            return;
        }
        string existing_data(existing_data_cstr);
        free(existing_data_cstr);

        std::cout << "[dispatch_request] existing_data: " << existing_data << std::endl;
        // string sid = cookies["sessionid"];
        // existing_data = sid.c_str(); 

        // Validate session ID
        if (existing_data != cookies["sessionid"]) {
            context.variables["sessionid"] = ""; 
            context.variables["username"] = "";
        }
    } else {
        // Handle the case where the Cookie header is missing or empty
        context.variables["sessionid"] = "";
        context.variables["username"] = "";
    }

    std::string temp = context.variables["username"] + "#" + "storage";
    const char* row_key_storage = temp.c_str();
    std::string temp_acct = context.variables["username"] + "#" + "account";
    const char* row_key_acct = temp_acct.c_str();
    

    cerr << "session id" << context.variables["sessionid"] << " " << "user name" << context.variables["username"] << endl; 
    
    if (request.method == "GET" && request.path == "/") {
        auto it = request.query_params.find("code");
        if (it != request.query_params.end()) {
            context.variables["auth_token"] = it->second;
            response_body = loadPage("views/fake_landing_page.html", context);
            send_response(client_sockfd, status_code, content_type, response_body);
            return;
        }
        // extract session id and user id
        if (!context.variables["sessionid"].empty() && !context.variables["username"].empty()) {
            context.boolVars["logged_in"] = true;
        } else {
            context.boolVars["logged_in"] = false;
        }
        response_body = get_landing_page(request);
        send_response(client_sockfd, status_code, content_type, response_body);
    } else if (request.method == "GET" && request.path == "/email") {
        if (!is_logged_in()) {
            redirect_to_landing_page(client_sockfd);
        } else {
            response_body = get_email_page(request);
            send_response(client_sockfd, status_code, content_type, response_body);
        }
    } else if (request.path == "/restart") {
        allow_running.store(true); 
        json response_body = {
            {"status", "success"},
            {"message", "Node restarted successfully."}
        };
        send_response(client_sockfd, "200 OK", "application/json", response_body.dump(), "");
    } else if (request.path == "/stop") {
        cerr << "stop my node " << endl;
        allow_running.store(false); 
        json response_body = {
            {"status", "success"},
            {"message", "Node stopped successfully."}
        };
        send_response(client_sockfd, "200 OK", "application/json", response_body.dump(), "");
    } else if (request.method == "POST" && request.path == "/login") {
        string set_cookie_header;
        bool is_successful;
        post_login(request, set_cookie_header, is_successful);
        if (is_successful) {
            // Successful login, redirect with Set-Cookie header
            send_response(client_sockfd, "302 Found", "", "", set_cookie_header);
        } else {
            // Failed login, respond with 401 Unauthorized
            send_response(client_sockfd, "302 Found", "", "", "Location: /\r\n");
        }
    } else if (request.method == "POST" && request.path == "/signup") {
        string set_cookie_header;
        bool is_successful;
        post_signup(request, set_cookie_header, is_successful);
        if (is_successful) {
            // Successful sign-up, redirect with Set-Cookie header
            send_response(client_sockfd, "302 Found", "", "", set_cookie_header);
        } else {
            // Failed sign-up, respond with 400 Bad Request
            send_response(client_sockfd, "400 Bad Request", "text/plain", "Sign-up failed. Username already exists.", "");
        }
    } else if (request.method == "POST" && request.path == "/logout") {
        string set_cookie_header;
        post_logout(request, set_cookie_header);
        send_response(client_sockfd, "302 Found", "", "", set_cookie_header);
    } else if (request.method == "POST" && request.path == "/change_password") {
        json res = nlohmann::json::parse(request.body); 
        string new_pwd = res["newPassword"];
        bool is_success = post_change_pwd(row_key_acct, new_pwd);
        if (is_success) {
           send_response(client_sockfd, "200 OK", content_type, "success"); 
        }  else {
           send_response(client_sockfd, "400 Bad Request", content_type, "failed");  
        }
    } else if (request.method == "POST" && request.path == "/forgot_password") {
        json res = nlohmann::json::parse(request.body); 
        string new_pwd = res["newPassword"];
        string forgot_password_username = res["username"]; 
        string rowKey = forgot_password_username + "#" + "account";
        bool is_success = post_change_pwd(rowKey, new_pwd);
        if (is_success) {
            string set_cookie_header;
            bool is_successful;
            post_login(request, set_cookie_header, is_successful);
            if (is_successful) {
                // Successful login, redirect with Set-Cookie header
                send_response(client_sockfd, "302 Found", "", "", set_cookie_header);
            } else {
                // Failed login, respond with 401 Unauthorized
                send_response(client_sockfd, "302 Found", "", "", "Location: /\r\n");
            }
        } else {
           send_response(client_sockfd, "400 Bad Request", content_type, "failed");  
        } 
    } else if (request.method == "GET" && request.path == "/email_compose") {
        response_body = get_email_compose(request);   
        send_response(client_sockfd, status_code, content_type, response_body);
    } else if (request.method == "GET" && request.path == "/single_email") {
        auto it = request.query_params.find("uuid");
        string curr_user =  context.variables["username"]; // temp measure
        if (it == request.query_params.end()) {
            std::cerr << "Error: 'uuid' not found in query_params!" << std::endl;
        }
        auto it2 = request.query_params.find("subject");
        if (it2 == request.query_params.end()) {
            std::cerr << "Error: 'subject' not found in query_params!" << std::endl;
        } 
        auto it3 = request.query_params.find("target");
        if (it3 == request.query_params.end()) {
            std::cerr << "Error: 'target' not found in query_params!" << std::endl;
        }
        string uuid = it->second;
        string subject = it2->second;
        string target = it3->second;
        string message = get_mailbox_or_email(curr_user, uuid, email_status_code);

        if (message.empty()) {
            std::cout << "failed to retrieve email" << std::endl;
            send_response(client_sockfd, "400 Bad Request", "text/plain", "Failed to retrieve this email.", "");
        } else {
            json emailJson;
            emailJson["uuid"] = uuid;            
            emailJson["target"] = target;
            emailJson["subject"] = subject;
            cerr << "I AM HERE";
            cerr << "uid" << uuid << "target" << target << "subject" << subject << "message" << message << endl;
            auto sanitizedInput = sanitizeJsonString(message);
            cerr << "after sanitize" << endl;
            json parsedMessage = json::parse(sanitizedInput);
            cerr << "I AM NOT HERE";
            emailJson["message"] = parsedMessage;
            //response_body = assemble_email_display(uuid, target, subject, message);
            send_response(client_sockfd, status_code, "application/json", emailJson.dump());
        }

    } else if (request.method == "DELETE" && request.path == "/send_email") {
        auto it = request.query_params.find("uuid");
        string curr_user = context.variables["username"];  // temp measure
        if (it == request.query_params.end()) {
            std::cerr << "Error: 'uuid' not found in query_params!" << std::endl;
        }
        auto it2 = request.query_params.find("type");
        if (it2 == request.query_params.end()) {
            std::cerr << "Error: 'type' not found in query_params!" << std::endl;
        }
        string email_id = it->second;
        string which_mailbox = it2->second;
        bool the_epic_result = delete_email(curr_user, which_mailbox, email_id, email_status_code, false);
        if (!the_epic_result) {
            send_response(client_sockfd, "400 Bad Request", "text/plain", "", "");
        } else {
            string second_mailbox = which_mailbox == "inbox" ? "sent" : "inbox";
            bool round_2 = delete_email(curr_user, second_mailbox, email_id, email_status_code, true);
            if (round_2) {
                send_response(client_sockfd, "200 OK", "text/plain", "", "");
            } else {
                send_response(client_sockfd, "400 Bad Request", "text/plain", "", "");
            }
        }
    } else if (request.method == "GET" && request.path == "/box") {
        // request.body

        // inbox or sent?
        auto it = request.query_params.find("type");
        if (it == request.query_params.end()) {
            std::cerr << "Error: 'type' not found in query_params!" << std::endl;
        }
        string the_box = it->second;
        string user_id = context.variables["username"];     // temp measure

        
        string mailbox = get_mailbox_or_email(user_id, the_box, email_status_code);
        if (mailbox.empty()) {
            std::cout << "failed to retrieve mailbox" << std::endl;
            
            //send_response(client_sockfd, "400 Bad Request", "text/plain", "Failed to retrieve this email.", "");
            json empty_string = "[]";
            send_response(client_sockfd, "200 OK", "application/json", empty_string.dump(), ""); 
        } else {
            response_body = assemble_mailbox(mailbox);
            cout << "here is the response body::::: " << response_body << endl;
            // send response, which is HTML code
            send_response(client_sockfd, email_status_code, content_type, response_body);  
        }

    } else if (request.method == "POST" && request.path == "/email_reply") {
        cout << request.body << endl;
        json res = nlohmann::json::parse(request.body);
        string the_user = res["sender"].get<std::string>();
        string recp = res["recipient"].get<std::string>();
        string uuid = res["uuid"].get<std::string>();
        cout << "DELETING FROM THE SENT BOX !!!!!! YA" << endl;
        bool win = delete_email(the_user, "sent", uuid, email_status_code, true);
        std::string stringified_array = res["message"].dump();
        // update our account
        std::string metadata = res["subject"].get<std::string>() + ";" + res["recipient"].get<std::string>() + ";" + res["timestamp"].get<std::string>();
        bool win2 = put_mailbox(the_user, "sent", metadata, uuid, stringified_array, email_status_code, false);
        // bool win3 = put_mailbox(the_user, "inbox", NULL, uuid, new_contents, email_status_code, true);
        
        // update their account
        std::string their_metadata = res["subject"].get<std::string>() + ";" + res["sender"].get<std::string>() + ";" + res["timestamp"].get<std::string>();
        bool win4 = delete_email(recp, "inbox", uuid, email_status_code, true);
        bool win5 = put_mailbox(recp, "inbox", their_metadata, uuid, stringified_array, email_status_code, false);
        // bool win6 = put_mailbox(recp, "sent", NULL, uuid, new_contents, email_status_code, true);

        if (win && win2 && win4 && win5) {
            response_body = strdup("<html><body><h1>Reply posted successfully.</h1></body></html>");
            send_response(client_sockfd, email_status_code, content_type, response_body); 
        } else {
            email_status_code = "400 Bad Request";
            response_body = "<html><body><h1>Error: Bad Request.</h1></body></html>";
            send_response(client_sockfd, email_status_code, content_type, response_body);  
        }
    } else if (request.method == "POST" && request.path == "/email_forward") {
      // request.body

      cout << request.body << endl;
      json res = nlohmann::json::parse(request.body);

      string the_recipient = res["recipient"].get<std::string>();
      bool is_external = false;
      size_t atPosition = the_recipient.find('@');
      string expectedDomain = "localhost.com";
      if (atPosition != std::string::npos && the_recipient.substr(atPosition + 1, expectedDomain.length()) != expectedDomain) {
        is_external = true;
      }

      // dummy: user = edmund
      std::string metadata = res["subject"].get<std::string>() + ";" + res["recipient"].get<std::string>() + ";" + res["timestamp"].get<std::string>();

      std::string uuid = compute_UUID(metadata);
      std::string the_box = "sent";
      // put in our mailbox
      cout << "The sender of our email is: " << res["sender"].get<std::string>() << endl;
      string forwarded_email = res["message"];
      string final_email = assemble_single_email(res["sender"].get<std::string>(), res["recipient"].get<std::string>(), res["timestamp"].get<std::string>(), forwarded_email);
      bool win = put_mailbox(res["sender"].get<std::string>(), the_box, metadata, uuid, final_email, email_status_code, false);
      // put in their mailbox
      bool win_put = false;
      if (!is_external) {
        the_box = "inbox";
        std::string their_metadata = res["subject"].get<std::string>() + ";" + res["sender"].get<std::string>() + ";" + res["timestamp"].get<std::string>();
        win_put = put_mailbox(res["recipient"].get<std::string>(), the_box, their_metadata, uuid, final_email, email_status_code, false);
      } else {
        win_put = send_external(the_recipient, uuid, forwarded_email, res["sender"].get<std::string>());
      }


      

      // send response, which is HTML code
      if (win && win_put) {
        response_body = strdup("<html><body><h1>Email forwarded successfully.</h1></body></html>");
        send_response(client_sockfd, email_status_code, content_type, response_body);         
      } else {
        email_status_code = "400 Bad Request";
        response_body = "<html><body><h1>Error: Bad Request.</h1></body></html>";
        send_response(client_sockfd, email_status_code, content_type, response_body);   
      }
    } else if (request.method == "POST" && request.path == "/send_email") {
      // request.body

      cout << request.body << endl;
      json res = nlohmann::json::parse(request.body);
      bool is_external = false;
      string the_recipient = res["recipient"].get<std::string>();
      size_t atPosition = the_recipient.find('@');
      string expectedDomain = "localhost.com";
      if (atPosition != std::string::npos && the_recipient.substr(atPosition + 1, expectedDomain.length()) != expectedDomain) {
        is_external = true;
      }

      // dummy: user = edmund
      std::string metadata = res["subject"].get<std::string>() + ";" + res["recipient"].get<std::string>() + ";" + res["timestamp"].get<std::string>();

      std::string uuid = compute_UUID(metadata);
      std::string the_box = "sent";
      string my_sender_name = context.variables["username"];
      string the_mess = assemble_single_email(my_sender_name, res["recipient"].get<std::string>(), res["timestamp"].get<std::string>(), res["message"].get<std::string>());
      // put in our mailbox
      cout << "The sender of our email is: " << res["sender"].get<std::string>() << endl;
      bool win = put_mailbox(my_sender_name, the_box, metadata, uuid, the_mess, email_status_code, false);
      bool win_put = false;
      // put in their mailbox
      if (!is_external) {
        the_box = "inbox";
        std::string their_metadata = res["subject"].get<std::string>() + ";" + res["sender"].get<std::string>() + ";" + res["timestamp"].get<std::string>();
        win_put = put_mailbox(res["recipient"].get<std::string>(), the_box, their_metadata, uuid, the_mess, email_status_code, false);
      } else {
        // external logic: send external
        win_put = send_external(the_recipient, uuid, res["message"].get<std::string>(), res["sender"].get<std::string>());
      }

      // send response, which is HTML code
      if (win && win_put) {
        response_body = strdup("<html><body><h1>Email sent successfully.</h1></body></html>");
        send_response(client_sockfd, email_status_code, content_type, response_body);         
      } else {
        email_status_code = "400 Bad Request";
        response_body = "<html><body><h1>Error: Bad Request.</h1></body></html>";
        send_response(client_sockfd, email_status_code, content_type, response_body);   
      }
    } else if (request.method == "GET" && request.path == "/storage") {
        response_body = get_storage_page(request);
        send_response(client_sockfd, status_code, content_type, response_body);
        if (!is_logged_in()) {
            redirect_to_landing_page(client_sockfd);
        } else {
            response_body = get_email_compose(request);
            send_response(client_sockfd, status_code, content_type, response_body);
        }
    } else if (request.method == "GET" && request.path == "/storage") {
         if (!is_logged_in()) {
            redirect_to_landing_page(client_sockfd);
        } else {
            response_body = get_storage_page(request);
            send_response(client_sockfd, status_code, content_type, response_body); 
        }
    } else if (request.method == "GET" && request.path.rfind("/assets", 0) == 0) {
        response_body = handle_static_file(request.path);
        if (response_body.empty()) {
            response_body = get_404_page(request);
            status_code = "404 Not Found";
        } else {
            if (ends_with(request.path, ".jpg") || ends_with(request.path, ".jpeg")) {
                content_type = "image/jpeg";
            } else if (ends_with(request.path, ".png")) {
                content_type = "image/png";
            } else if (ends_with(request.path, ".css")) {
                content_type = "text/css";
            } else if (ends_with(request.path, ".svg")) { // Added SVG support
            content_type = "image/svg+xml";
            }
        }
        send_response(client_sockfd, status_code, content_type, response_body);
    } else if (request.method == "POST" && request.path == "/upload_file") {
        if (!is_logged_in()) {
            redirect_to_landing_page(client_sockfd);
        } else {
            FileMetadata file_metadata;
            string file_data = "";
            handle_file_upload(request, response_body, file_metadata, file_data); 
            const char* user_id = row_key_storage;         // Required
            const char* relative_path = file_metadata.relative_path;   // Required  Include the file itself in the path
            //cerr << "******[upload_file] relative path : " << relative_path << endl;
            const char* filename = file_metadata.filename;        // Required
            const char* file_type = file_metadata.file_type;       // Required
            size_t file_size = file_metadata.file_size;       // Required
            const char* last_modified = "";   // optional
            const char* str = file_data.c_str();
            //cerr << "uploaded file size is " << file_data.size() << endl;
            void* content =  (void*)str;         // Required
            //cerr << "file size " << file_size << endl;
            // std::vector<char> binary_data(file_data.begin(), file_data.end());
            // std::ostringstream filepath;
            // filepath << "uploads/" << filename;
            // std::ofstream output_file(filepath.str(), std::ios::binary);
            // if (!output_file) {
            //     std::cerr << "Error creating file." << std::endl;
            // }
            // output_file.write(binary_data.data(), binary_data.size());
            // output_file.close();
            response_body = handle_upload_request(user_id, relative_path, filename, file_type, file_size, last_modified, content,&storage_status_code);
            status_code = std::string(storage_status_code);
            json my_restart_json = {
                    {"status", "success"},
                    {"message", "Uploaded successfully."}
                    };
            send_response(client_sockfd, status_code, content_type, my_restart_json.dump());
            free(storage_status_code);
        }
    } else if (request.method == "GET" && request.path == "/download_file") {
        const char* user_id = row_key_storage;         // Required
        const char* relative_path = "/";   // Required  Include the folder itself in the path
        auto it = request.query_params.find("path");
        if (it != request.query_params.end()) {
            // Decode the URL-encoded value of "path"
            std::string decoded_path = urlDecode(it->second);
            cerr << "me here" << decoded_path << endl;
            relative_path = strdup(decoded_path.c_str()); // Dynamically allocate memory for safety
        } else {
            std::cerr << "Error: 'relative_path' not found in query_params!" << std::endl;
        }
        //const char* relative_path = "/C.txt";   // Required  Include the file itself in the path
        const char* filename =  strrchr(relative_path, '/');
        filename = (filename != nullptr) ? filename + 1 : relative_path;        // Required
        cerr << "file name is " << filename << endl;
        FileMetadata fileMeta = {0};          // Used to receive file metadata
        fileMeta.relative_path=NULL;
        void *file_content = NULL;      // Used to receive file contents
        // *****You can get the size of binary data by fileMeta->file_size*******
        response_body = handle_download_request(user_id, relative_path, filename, &fileMeta, &file_content,&storage_status_code);
        status_code = std::string(storage_status_code);
        string headers = "HTTP/1.1 " + status_code + "\r\n"
                "Content-Type: application/octet-stream\r\n"
                "Content-Disposition: attachment; filename=\"" + string(filename) + "\"\r\n"
                "Content-Length: " + to_string(fileMeta.file_size) + "\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "\r\n";

        // Send headers first
        send(client_sockfd, headers.c_str(), headers.length(), 0);

        // Send file content directly
        send(client_sockfd, file_content, fileMeta.file_size, 0);
        
        //cerr << "is my string empty? " << my_str.empty() << endl;
        
        free(storage_status_code);
        if (relative_path != "/" && relative_path != nullptr) {
            free(const_cast<char*>(relative_path)); // Free the dynamically allocated memory
        }
        //TODO remember to release memory after use
        // free_file_metadata（fileMeta）
        // }
    } else if (request.method == "GET" && request.path == "/list_entries") {
        if (!is_logged_in()) {
            redirect_to_landing_page(client_sockfd);
        } else {
            const char* user_id = row_key_storage;         // Required
            const char* relative_path = "/";   // Required  Include the folder itself in the path
            auto it = request.query_params.find("relative_path");
            if (it != request.query_params.end()) {
                relative_path = it->second.c_str(); 
            } else {
                std::cerr << "Error: 'relative_path' not found in query_params!" << std::endl;
            }
            const char* foldername = "";        // Required
            EntryNode *entry_list = NULL;
            response_body = handle_list_entries_request(user_id, relative_path, foldername, &entry_list,&storage_status_code);
            json json_response = entry_list_to_json(entry_list);
            string serialized_json = json_response.dump();
            // ***** you can get result by entry_list ****
            status_code = std::string(storage_status_code);
            send_response(client_sockfd, status_code, content_type, serialized_json);
            free(storage_status_code);
        }
    } else if (request.method == "DELETE" && request.path == "/delete_file") {
        if (!is_logged_in()) {
            redirect_to_landing_page(client_sockfd);
        } else {
            json res = nlohmann::json::parse(request.body); 
            const char* user_id = row_key_storage;         
            string relative_path = res["path"];   
            string filename =  relative_path.substr(relative_path.find_last_of('/') + 1); 
            response_body = handle_delete_file_request(user_id, relative_path.c_str(), filename.c_str(), &storage_status_code);
            status_code = std::string(storage_status_code);
            send_response(client_sockfd, status_code, content_type, response_body);
            free(storage_status_code);
        }
    } else if (request.method == "DELETE" && request.path == "/delete_folder") {
        if (!is_logged_in()) {
            redirect_to_landing_page(client_sockfd);
        } else {
            json res = nlohmann::json::parse(request.body); 
            const char* user_id = row_key_storage;         // Required
            string relative_path = res["path"];   // Required  Include the folder itself in the path
            cerr << "deleting my folder " << relative_path << endl;
            cerr << "******[delete_folder] relative path : " << relative_path << endl;
            string foldername =  relative_path.substr(relative_path.find_last_of('/') + 1); // Required
            response_body = handle_delete_folder_request(user_id, relative_path.c_str(), foldername.c_str(),&storage_status_code);
            status_code = std::string(storage_status_code);
            send_response(client_sockfd, status_code, content_type, response_body);
            free(storage_status_code);
        }
    } else if (request.method == "GET" and request.path == "/admin") {
        if (!is_logged_in()) {
            redirect_to_landing_page(client_sockfd);
        } else { 
            response_body = get_admin_page(request);
            send_response(client_sockfd, status_code, content_type, response_body);
        }
    } else if (request.method == "PUT" && request.path == "/move") {
        if (!is_logged_in()) {
            redirect_to_landing_page(client_sockfd);
        } else {
            const char* user_id = row_key_storage;         // Required
            json res = nlohmann::json::parse(request.body); 
            string old_relative_path = res["oldPath"];   // Required  Include the folder/file itself in the path
            //char* new_relative_path = "/";   // Required  Include the folder/file itself in the path
            string new_relative_path_str = res["newLocation"];
            char* new_relative_path = strdup(new_relative_path_str.c_str());
            response_body = handle_move_request( user_id,  old_relative_path.c_str(),  new_relative_path, &storage_status_code);
            status_code = std::string(storage_status_code);
            send_response(client_sockfd, status_code, content_type, response_body);
            free(storage_status_code);
        }
    } else if (request.method == "PUT" && request.path == "/rename") {
        if (!is_logged_in()) {
            redirect_to_landing_page(client_sockfd);
        } else {
            json res = nlohmann::json::parse(request.body); 
            const char* user_id = row_key_storage;         // Required
            string relative_path = res["path"];   // Required  Include the folder/file itself in the path
            string new_name = res["newName"];        // Required
            response_body = handle_rename_request(user_id, relative_path.c_str(), new_name.c_str(),&storage_status_code);
            status_code = std::string(storage_status_code);
            send_response(client_sockfd, status_code, content_type, response_body);
            free(storage_status_code);
        }
    } else if (request.method == "POST" && request.path == "/create_folder") {
        if (!is_logged_in()) {
            redirect_to_landing_page(client_sockfd);
        } else {
            json res = nlohmann::json::parse(request.body); 
            const char* user_id = row_key_storage;         // Required
            string relative_path = res["path"];   // Required  Include the folder itself in the path
            string foldername = res["foldername"];        // Required
            response_body = handle_create_folder_request(user_id, relative_path.c_str(), foldername.c_str(),&storage_status_code);
            status_code = std::string(storage_status_code);
            send_response(client_sockfd, status_code, content_type, response_body);
            free(storage_status_code);
        }
    } else if (request.method == "GET" && request.path == "/game") {
        response_body = get_game_page(request);
        send_response(client_sockfd, status_code, content_type, response_body); 
    } else if (request.method == "GET" && request.path == "/tictactoe") {
        response_body = loadPage("views/games/tictactoe.html", context); 
        send_response(client_sockfd, status_code, content_type, response_body); 
    } else if (request.method == "GET" && request.path == "/snake") {
        response_body = loadPage("views/games/snake.html", context); 
        send_response(client_sockfd, status_code, content_type, response_body); 
    } else if (request.method == "POST" && request.path == "/login_google") {
        json res = nlohmann::json::parse(request.body); 
        string sessionid = res["sessionid"];
        string user = res["user"]; 
        string set_cookie_header;
        bool is_successful;
        post_login_google(sessionid, user, set_cookie_header, is_successful);
        if (is_successful) {
            // Successful sign-up, redirect with Set-Cookie header
            send_response(client_sockfd, "200 OK", "text/html", "success", set_cookie_header);
        } else {
            // Failed sign-up, respond with 400 Bad Request
            send_response(client_sockfd, "400 Bad Request", "text/plain", "Sign-up failed. Username already exists.", "");
        } 
        
    }   else {
        response_body = get_404_page(request);
        status_code = "404 Not Found";
        send_response(client_sockfd, status_code, content_type, response_body);
        free(storage_status_code);
    }
}

string get_email_page(const Request& request) {
    ifstream file("views/email_page.html");
    if (!file.is_open()) {
        return "<html><body><h1>Error: Unable to load landing page.</h1></body></html>";
    }

    // get UUID list from backend
    context.lists["emails"] = std::vector<std::shared_ptr<Model>>{
        std::make_shared<Email>("1", "Meeting Reminder", "alice@example.com", "bob@example.com", "Tue, Nov 14 2023 10:00 AM", "Don't forget about our meeting tomorrow at 10 AM."),
        std::make_shared<Email>("2", "Lunch Invitation", "carol@example.com", "bob@example.com", "Wed, Nov 15 2023 12:30 PM", "Would you like to join me for lunch on Wednesday at 12:30?"),
        std::make_shared<Email>("3", "Project Update", "dave@example.com", "bob@example.com", "Thu, Nov 16 2023 2:00 PM", "Here’s the latest update on the project. Let me know if you have any questions."),
        std::make_shared<Email>("4", "Weekend Plans", "eve@example.com", "bob@example.com", "Fri, Nov 17 2023 5:00 PM", "Are you free this weekend to hang out? Let me know what works for you!"),
        std::make_shared<Email>("5", "Newsletter: November Edition", "newsletter@example.com", "bob@example.com", "Mon, Nov 13 2023 9:00 AM", "Welcome to the November edition of our newsletter. Here are the latest updates and news."),
        std::make_shared<Email>("6", "Flight Itinerary", "travelagency@example.com", "bob@example.com", "Sun, Nov 12 2023 8:30 AM", "Your flight is scheduled for departure on Monday, Nov 20 at 7:45 AM. Please arrive at the airport 2 hours before."),
        std::make_shared<Email>("7", "Invoice for Services", "billing@example.com", "bob@example.com", "Thu, Nov 9 2023 3:00 PM", "Please find attached the invoice for the services provided in October."),
        std::make_shared<Email>("8", "Happy Birthday!", "friends@example.com", "bob@example.com", "Wed, Nov 8 2023 10:00 AM", "Wishing you a very happy birthday, Bob! Hope you have a fantastic year ahead."),
        std::make_shared<Email>("9", "Job Application Update", "hr@examplecompany.com", "bob@example.com", "Tue, Nov 7 2023 9:30 AM", "We would like to invite you for an interview. Please let us know your availability."),
        std::make_shared<Email>("10", "Subscription Confirmation", "noreply@example.com", "bob@example.com", "Mon, Nov 6 2023 11:45 AM", "Thank you for subscribing to our service! You’ll receive updates to this email.")
    };

    stringstream buffer;
    buffer << file.rdbuf();
    file.close();
    string result = renderTemplate(buffer.str(), context);
    return result; 
}

std::string sanitizeJsonString(const std::string& input) {
    std::string sanitized = input;
    size_t pos = 0;
    while (pos < sanitized.length()) {
        if (sanitized[pos] == '\n') {
            sanitized.replace(pos, 1, "\\n");
            pos += 2; // Move past the inserted escape sequence
        } else if (sanitized[pos] == '\r') {
            sanitized.replace(pos, 1, "\\r");
            pos += 2;
        } else {
            pos++;
        }
    }
    return sanitized;
}



string loadPage(const string& filepath, Context& context) {
    ifstream file(filepath);
    if (!file.is_open()) {
        return "<html><body><h1>Error: Unable to load the page.</h1></body></html>";
    }
    stringstream buffer;
    buffer << file.rdbuf();
    file.close();
    return renderTemplate(buffer.str(), context);
}

string get_landing_page(const Request& request) {
    return loadPage("views/landing_page.html", context);
}

string get_game_page(const Request& request) {
    return loadPage("views/game_landing.html", context);
}

string get_email_compose(const Request& request) {
    return loadPage("views/email_compose.html", context);
}

string get_storage_page(const Request& request) {
    return loadPage("views/storage_page.html", context);
}

string get_admin_page(const Request& request) {
    return loadPage("views/admin.html", context);
}

string get_404_page(const Request& request) {
    return loadPage("views/404_page.html", context);
}


std::string get_boundary(const std::string& content_type) {
    std::string boundary_prefix = "boundary=";
    size_t pos = content_type.find(boundary_prefix);
    if (pos != std::string::npos) {
        return "--" + content_type.substr(pos + boundary_prefix.length());
    }
    return "";
}

struct FormPart {
    std::map<std::string, std::string> headers;
    std::string data;
};

std::vector<FormPart> parse_multipart(const std::string& body, const std::string& boundary) {
    std::vector<FormPart> parts;
    size_t pos = 0;
    std::string delimiter = boundary;
    size_t delimiter_length = delimiter.length();

    while (pos < body.length()) {
        // Find the start of the next part
        size_t boundary_start = body.find(delimiter, pos);
        if (boundary_start == std::string::npos) {
            break;
        }
        pos = boundary_start + delimiter_length;

        // Check for the end of the multipart data
        if (body.substr(pos, 2) == "--") {
            break; // Reached the final boundary
        }

        // Skip any CRLF characters
        if (body.substr(pos, 2) == "\r\n") {
            pos += 2;
        }

        // Find the end of headers
        size_t headers_end = body.find("\r\n\r\n", pos);
        if (headers_end == std::string::npos) {
            break; // Malformed data
        }

        // Extract headers
        std::string headers_str = body.substr(pos, headers_end - pos);
        pos = headers_end + 4; // Move past \r\n\r\n

        // Parse headers
        FormPart part;
        std::istringstream headers_stream(headers_str);
        std::string header_line;


        while (std::getline(headers_stream, header_line)) {
            if (!header_line.empty() && header_line.back() == '\r') {
                header_line.pop_back();
            }
            size_t colon_pos = header_line.find(':');
            if (colon_pos != std::string::npos) {
                std::string header_name = header_line.substr(0, colon_pos);
                std::string header_value = header_line.substr(colon_pos + 1);
                // Trim whitespace
                header_value.erase(0, header_value.find_first_not_of(" \t"));
                header_value.erase(header_value.find_last_not_of(" \t\r\n") + 1);
                part.headers[header_name] = header_value;
            }
        }

        // Find the end of the part data
        size_t part_end = body.find(boundary, pos);
        if (part_end == std::string::npos) {
            part_end = body.length();
        } else {
            part_end -= 2; // Move back before \r\n
        }

        // Extract part data
        part.data = body.substr(pos, part_end - pos);

        parts.push_back(part);

        pos = part_end;
    }

    return parts;
}


bool handle_file_upload(const Request& request, std::string& response_body, FileMetadata& metadata, string& content) {
    // Check for Content-Type header
    if (request.headers.find("Content-Type") == request.headers.end()) {
        response_body = "{\"error\":\"Content-Type header missing\"}";
        return false;
    }

    // Verify that the Content-Type is multipart/form-data
    std::string content_type = request.headers.at("Content-Type");
    if (content_type.find("multipart/form-data") == std::string::npos) {
        response_body = "{\"error\":\"Invalid Content-Type\"}";
        return false;
    }

    // Extract the boundary string
    std::string boundary = get_boundary(content_type);
    if (boundary.empty()) {
        response_body = "{\"error\":\"Boundary not found\"}";
        return false;
    }

    // Parse the multipart data
    std::vector<FormPart> parts = parse_multipart(request.body, boundary);
    if (parts.empty()) {
        response_body = "{\"error\":\"No form data found\"}";
        return false;
    }

    string file_data;

    for (const auto& part : parts) {
        if (part.headers.find("Content-Disposition") != part.headers.end()) {
            string disposition = part.headers.at("Content-Disposition");
            // Extract name from Content-Disposition
            string name = get_disposition_value(disposition, "name");

            // Map form fields to metadata
            if (name == "file") {
                file_data = part.data;
            } else if (name == "path") {
                metadata.relative_path = strdup(part.data.c_str());
            } else if (name == "name") {
                strncpy(metadata.filename, part.data.c_str(), sizeof(metadata.filename) - 1);
                metadata.filename[sizeof(metadata.filename) - 1] = '\0';
            } else if (name == "type") {
                strncpy(metadata.file_type, part.data.c_str(), sizeof(metadata.file_type) - 1);
                metadata.file_type[sizeof(metadata.file_type) - 1] = '\0';
            } else if (name == "size") {
                try {
                    metadata.file_size = std::stoul(part.data);
                } catch (const std::exception& e) {
                    response_body = "{\"error\":\"Invalid file size\"}";
                    return false;
                }
            }
        }
    }

    if (file_data.empty() || strlen(metadata.filename) == 0) {
        response_body = "{\"error\":\"File data or filename is missing\"}";
        return false;
    }   


    // Sanitize file name and current folder to prevent directory traversal
    // std::string sanitized_file_name = sanitize_path(file_name);
    // std::string sanitized_folder = sanitize_path(current_folder);

    // Build the full path
    // std::string upload_dir = "uploads"; // Root upload directory
    // std::string full_path = upload_dir + "/" + sanitized_folder;

  

    // Save the file
    // std::string file_path = full_path + "/" + sanitized_file_name;
    // if (!handle_upload(file_path, file_data)) {
    //     response_body = "{\"error\":\"Failed to save file\"}";
    //     return false;
    // }
    response_body = "{\"message\":\"File uploaded successfully\"}";
    content = file_data;
    return true;
}

// bool handle_upload(const std::string& filePath, const std::string& data) {
//     cerr << "path is " << filePath << endl;
//     std::vector<char> binary_data(data.begin(), data.end());
//         // Create the directory if it doesn't exist
//     std::ofstream output_file(filePath, std::ios::binary);
//     if (!output_file) {
//         std::cerr << "Error creating file." << std::endl;
//         return false;
//     }
//     output_file.write(binary_data.data(), binary_data.size());
//     output_file.close();
//     return true;
// }

std::string get_disposition_value(const std::string& disposition, const std::string& key) {
    size_t key_pos = disposition.find(key + "=\"");
    if (key_pos != std::string::npos) {
        key_pos += key.length() + 2; // Move past key="
        size_t end_pos = disposition.find("\"", key_pos);
        if (end_pos != std::string::npos) {
            return disposition.substr(key_pos, end_pos - key_pos);
        }
    }
    return "";
}



std::string sanitize_path(const std::string& path) {
    std::string sanitized = path;
    // Remove any '..' to prevent directory traversal
    size_t pos;
    while ((pos = sanitized.find("..")) != std::string::npos) {
        sanitized.erase(pos, 2);
    }
    // Replace backslashes with slashes
    std::replace(sanitized.begin(), sanitized.end(), '\\', '/');
    // Remove any double slashes
    while ((pos = sanitized.find("//")) != std::string::npos) {
        sanitized.erase(pos, 1);
    }
    // Remove leading slashes
    sanitized.erase(0, sanitized.find_first_not_of("/"));
    return sanitized;
}


/*
your server should check whether the client includes a cookie with the request headers; if not,
it should create a cookie with a random ID and send it back with the response.
*/
void post_login(const Request& request, string& set_cookie_header, bool& is_successful) {
    auto form_data = parse_form_data(request.body);
    string username = form_data["username"];
    string password = form_data["password"];
    ResponseResult result = init_response_result();
    string stored_pwd;
    char* existing_data = NULL;

    // Retrieve stored password
    if (!get_response(SERVER_IP, SERVER_PORT, "GET", (username + "#account").c_str(), "pwd", NULL, 0, NULL, 0, &result)) {
        std::cerr << "[post_login] Error: Get(username#account, pwd) failed" << std::endl;
        context.boolVars["login_error"] = true;
        is_successful = false;
        return;
    }

    // Convert binary data to string
    existing_data = binary_to_string(result.content, result.content_length);
    if (existing_data == NULL) {
        std::cerr << "[post_login] Error: binary_to_string failed. existing_data is NULL" << std::endl;
        context.boolVars["login_error"] = true;
        is_successful = false;
        return;
    }
    stored_pwd = existing_data;
    SAFE_FREE(result.content);

    // Check if stored password matches the provided password
    if (stored_pwd == password) {
        string session_id = generate_session_id();

        // Convert session_id to binary
        const char* c_str = session_id.c_str();
        size_t binary_size = 0;
        void* binary_data = string_to_binary(c_str, &binary_size);
        if (binary_data == NULL) {
            std::cerr << "[post_login] Error: Failed to convert session_id to binary." << std::endl;
            context.boolVars["login_error"] = true;
            is_successful = false;
            return;
        }

        // Store session_id in the database
        if (!get_response(SERVER_IP, SERVER_PORT, "PUT", (username + "#account").c_str(), "sid", binary_data, binary_size, NULL, 0, &result)) {
            std::cerr << "[post_login] Error: Failed to PUT(username#account, sid, session_id)." << std::endl;
            SAFE_FREE(binary_data);
            context.boolVars["login_error"] = true;
            is_successful = false;
            return;
        }

        SAFE_FREE(binary_data);
        std::cout << "[post_login] PUT (username#account, sid, session_id) successfully" << std::endl;

        // Set the Set-Cookie header
        set_cookie_header =
            "Location: /\r\n"
            "Set-Cookie: sessionid=" + session_id + "; Path=/\r\n"
            "Set-Cookie: user=" + username + "; Path=/\r\n";

        context.boolVars["login_error"] = false;
        is_successful = true;
    } else {
        std::cerr << "[post_login] Error: Password mismatch." << std::endl;
        context.boolVars["login_error"] = true;
        is_successful = false;
    }
}


void post_signup(const Request& request, string& set_cookie_header, bool& is_successful) {
    auto form_data = parse_form_data(request.body);
    string username = form_data["username"];
    string password = form_data["password"];

    // Check if the username already exists
    bool username_exists = false; 
    char* existing_data = NULL;
    ResponseResult result = init_response_result();

    if (get_response(SERVER_IP, SERVER_PORT, "GET", (username + "#account").c_str(), "pwd", NULL, 0, NULL, 0, &result)) {
        existing_data = binary_to_string(result.content, result.content_length);
        if (existing_data != NULL) {
            username_exists = true;
            std::cout << "[post_signup] Username already exists: " << username << std::endl;
        }
        SAFE_FREE(result.content);
    }

    if (username_exists) {
        // If username already exists, fail sign-up
        is_successful = false;
        std::cerr << "[post_signup] Error: Username already exists" << std::endl;
        return;
    }

    // Generate session ID
    string session_id = generate_session_id();

    // Store password in the key-value store
    const char* pw = password.c_str();
    size_t binary_size = 0;
    void* binary_data = string_to_binary(pw, &binary_size);
    if (binary_data == NULL) {
        std::cerr << "[post_signup] Error: Failed to convert password to binary data" << std::endl;
        is_successful = false;
        return;
    }

    if (!get_response(SERVER_IP, SERVER_PORT, "PUT", (username + "#account").c_str(), "pwd", binary_data, binary_size, NULL, 0, &result)) {
        std::cerr << "[post_signup] Error: Failed to store password in the database" << std::endl;
        SAFE_FREE(binary_data);
        is_successful = false;
        return;
    }
    SAFE_FREE(binary_data);
    std::cout << "[post_signup] Stored password for username: " << username << std::endl;

    // Store session ID in the key-value store
    const char* sid = session_id.c_str();
    binary_size = 0;
    binary_data = string_to_binary(sid, &binary_size);
    if (binary_data == NULL) {
        std::cerr << "[post_signup] Error: Failed to convert session ID to binary data" << std::endl;
        is_successful = false;
        return;
    }

    if (!get_response(SERVER_IP, SERVER_PORT, "PUT", (username + "#account").c_str(), "sid", binary_data, binary_size, NULL, 0, &result)) {
        std::cerr << "[post_signup] Error: Failed to store session ID in the database" << std::endl;
        SAFE_FREE(binary_data);
        is_successful = false;
        return;
    }
    SAFE_FREE(binary_data);
    std::cout << "[post_signup] Stored session ID for username: " << username << std::endl;
    
    // set default value in column "dir_root" for storage system
    const char* value = "[]";
    binary_size = 0;
    binary_data = string_to_binary(value, &binary_size);
    if (binary_data == NULL) {
        std::cerr << "[post_signup] Error: Failed to convert value(which is []) to binary data" << std::endl;
        is_successful = false;
        return;
    }
    if (!get_response(SERVER_IP, SERVER_PORT, "PUT", (username + "#storage").c_str(), "dir_root", binary_data, binary_size, NULL, 0, &result)) {
        std::cerr << "[post_signup] Error: Failed to store default value (which is [])in the database" << std::endl;
        SAFE_FREE(binary_data);
        is_successful = false;
        return;
    }
    SAFE_FREE(binary_data);
    std::cout << "[post_signup] Stored default value in column dir_root" << std::endl;

    // Set cookies for the session
    set_cookie_header =
        "Location: /\r\n"
        "Set-Cookie: sessionid=" + session_id + "; Path=/\r\n"
        "Set-Cookie: user=" + username + "; Path=/\r\n";

    is_successful = true;
    std::cout << "[post_signup] Signup successful for username: " << username << std::endl;
}


void post_login_google(string session_id, string username, string& set_cookie_header, bool& is_successful) {
    ResponseResult result = init_response_result();
    size_t binary_size = 0;
       // Store session ID in the key-value store
    const char* sid = session_id.c_str();
    void* binary_data = string_to_binary(sid, &binary_size);
    if (binary_data == NULL) {
        std::cerr << "[post_signup] Error: Failed to convert session ID to binary data" << std::endl;
        is_successful = false;
        return;
    }

    if (!get_response(SERVER_IP, SERVER_PORT, "PUT", (username + "#account").c_str(), "sid", binary_data, binary_size, NULL, 0, &result)) {
        std::cerr << "[post_signup] Error: Failed to store session ID in the database" << std::endl;
        SAFE_FREE(binary_data);
        is_successful = false;
        return;
    }
    SAFE_FREE(binary_data);
    std::cout << "[post_signup] Stored session ID for username: " << username << std::endl;
    
    // set default value in column "dir_root" for storage system
    const char* value = "[]";
    binary_size = 0;
    binary_data = string_to_binary(value, &binary_size);
    if (binary_data == NULL) {
        std::cerr << "[post_signup] Error: Failed to convert value(which is []) to binary data" << std::endl;
        is_successful = false;
        return;
    }
    if (!get_response(SERVER_IP, SERVER_PORT, "PUT", (username + "#storage").c_str(), "dir_root", binary_data, binary_size, NULL, 0, &result)) {
        std::cerr << "[post_signup] Error: Failed to store default value (which is [])in the database" << std::endl;
        SAFE_FREE(binary_data);
        is_successful = false;
        return;
    }
    SAFE_FREE(binary_data);
    std::cout << "[post_signup] Stored default value in column dir_root" << std::endl;

    // Set cookies for the session
    set_cookie_header =
        "Location: /\r\n"
        "Set-Cookie: sessionid=" + session_id + "; Path=/\r\n"
        "Set-Cookie: user=" + username + "; Path=/\r\n";

    is_successful = true;
}

bool post_change_pwd(string row_key_acct, string new_pwd) {
    // Step 1: Check if row_key_acct exists in kvs
    ResponseResult result = init_response_result();
    if (!get_response(SERVER_IP, SERVER_PORT, "GET", row_key_acct.c_str(), "pwd", NULL, 0, NULL, 0, &result)) {
        std::cerr << "[post_change_pwd] Error: GET failed for row_key_acct = " << row_key_acct << ", 'pwd' column." << std::endl;
        return false;
    }
    SAFE_FREE(result.content);

    // Step 2: Change the pwd by doing a PUT
    size_t binary_size = 0;
    void* binary_data = string_to_binary(new_pwd.c_str(), &binary_size);
    if (!binary_data) {
        std::cerr << "[post_change_pwd] Error: Failed to convert new password to binary." << std::endl;
        return false;
    }

    if (!get_response(SERVER_IP, SERVER_PORT, "PUT", row_key_acct.c_str(), "pwd", binary_data, binary_size, NULL, 0, &result)) {
        std::cerr << "[post_change_pwd] Error: PUT failed to update password for row_key_acct = " << row_key_acct << std::endl;
        SAFE_FREE(binary_data);
        return false;
    }

    SAFE_FREE(binary_data);
    std::cout << "[post_change_pwd] Successfully updated password for row_key_acct = " << row_key_acct << std::endl;

    return true;
}

string generate_session_id() {
    uuid_t uuid;
    char uuid_str[37];

    uuid_generate(uuid);
    uuid_unparse(uuid, uuid_str);

    return std::string(uuid_str);
}


void post_logout(const Request& request, string& set_cookie_header) {
    set_cookie_header =  
                    "Location: /\r\n"
                    "Set-Cookie: sessionid=; Expires=Thu, 01 Jan 1970 00:00:00 GMT;  Secure\r\n"
                    "Set-Cookie: user=; Expires=Thu, 01 Jan 1970 00:00:00 GMT;  Secure\r\n";
}


string get_current_date() {
    time_t now = time(nullptr);
    char buf[100];
    strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", std::gmtime(&now));
    return std::string(buf);
}

string handle_static_file(const string& path) {
    string file_path = "/home/cis5050/fa24-cis5050-T07/" + path;
    ifstream file(file_path, ios::binary); 

    if (!file.is_open()) {
        cerr << "can't serve static file at " << file_path << endl;
        return ""; 
    }

    stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool ends_with(const std::string& str, const std::string& suffix) {
    return str.size() >= suffix.size() && 
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::map<std::string, std::string> parse_form_data(const std::string& body) {
    std::map<std::string, std::string> form_data;
    std::stringstream ss(body);
    std::string token;

    while (std::getline(ss, token, '&')) {
        size_t pos = token.find('=');
        if (pos != std::string::npos) {
            std::string key = token.substr(0, pos);
            std::string value = token.substr(pos + 1);
            form_data[key] = value;
        }
    }

    return form_data;
}

json entry_list_to_json(EntryNode* entry_list) {
    json result;
    result["data"] = json::array();

    EntryNode* current = entry_list;

    while (current != nullptr) {
        try {
            std::string name(current->name);
            std::string type(current->type);
            json entry = {
                {"name", name},
                {"type", type}
            };

            std::cout << "[entry_list_to_json] Created JSON entry: " << entry.dump() << std::endl;

            result["data"].push_back(entry);

        } catch (const nlohmann::json::exception& e) {
            std::cerr << "[entry_list_to_json] Error: Exception while processing entry: " << e.what() << std::endl;
        }
        current = current->next;
    }

    return result;
}


unordered_map<string, string> parse_cookies(const string& cookie_header) {
    unordered_map<string, string> cookies;
    istringstream stream(cookie_header);
    string token;

    while (getline(stream, token, ';')) {
        size_t start = token.find_first_not_of(' ');
        token = token.substr(start);

        size_t delimiter = token.find('=');
        if (delimiter != string::npos) {
            string key = token.substr(0, delimiter);
            string value = token.substr(delimiter + 1);
            cookies[key] = value;
        }
    }
    return cookies;
}


bool is_logged_in() {
    return !context.variables["sessionid"].empty() && !context.variables["username"].empty();
}

void redirect_to_landing_page(int client_sockfd) {
    string redirect_header = "Location: /\r\n";
    send_response(client_sockfd, "302 Found", "", "", redirect_header);
}


