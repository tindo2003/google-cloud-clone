#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <vector>
#include <iostream>
#include <algorithm>
#include <string>
#include <unordered_set>
#include <signal.h>
#include <fcntl.h>
#include <bits/stdc++.h>
#include <errno.h>
#include <ctime>
#include <regex>
#include <dirent.h>
#include <sys/file.h>
#include <resolv.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <netdb.h>
#include <cstring>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/md5.h>
#include "email_api.h"
using namespace std;


volatile std::unordered_set<int> fds;
// volatile std::unordered_map<std::string, pthread_mutex_t> mutexes;
volatile bool shut_it_down = false;
bool logging_it = false;
std::string path;
std::unordered_set<std::string> valid{"helo ", "mail ", "rcpt ", "data\r", "quit\r", "noop\r", "rset\r"};
std::regex send_match("<[a-zA-Z0-9.]+@[a-zA-Z0-9.]+>");
std::regex recp_match("<([a-zA-Z0-9.]+)@[a-zA-Z0-9.]+>");

/*
bool compareMX(const MXRecord &a, const MXRecord &b) {
    return a.priority < b.priority;
}
*/

bool hasEnding (std::string const &fullString, std::string const &ending) {
    if (fullString.length() >= ending.length()) {
        return (0 == fullString.compare (fullString.length() - ending.length(), ending.length(), ending));
    } else {
        return false;
    }
}

void* worker(void* arg) {
	// read client request
	int comm_fd = *(int*) arg;
	unsigned short rlen;
	// char buf [2000];

  std::vector<char> real_buf(2000);    //  -- this is new (previously no number in the declaration)
  // real_buf.reserve(10000000); -- do not use this
  // real_buf.resize(10000000);     -- was using this

	int low = 0;
  int state = -1;
  int retry_ctr = 0;
  std::string sender;
  std::queue<std::string> recipients;

    while (true) {
    	if (shut_it_down) {
    		close(comm_fd);
    		pthread_exit(NULL);
    	}
        if (state != 5 || strstr(real_buf.data(), "\r\n.\r\n") == nullptr) {
          // int num_read = read(comm_fd, &buf[low], sizeof(buf) - low); // Read data
          if ((int)real_buf.size() - low < 1000) {
            real_buf.resize(real_buf.size() * 2);       // another thing new
          }
          int num_read = read(comm_fd, &real_buf[low], 1000);
          // read whole command from server side;
          if (num_read <= 0) {
              fprintf(stderr, "server failed to read from socket\n");
              if (logging_it) {
                fprintf(stderr, "[%d] Connection closed\n", comm_fd);
              }
            std::unordered_set<int>& non_volatile_fds = const_cast<std::unordered_set<int>&>(fds);
            non_volatile_fds.erase(comm_fd);
            close(comm_fd);
            pthread_exit(NULL);
            // exit(1);
          }
          
          low += num_read;
          real_buf[low] = '\0'; // Null-terminate the buffer for easier searching
          // real_buf.resize(num_read + 1);

          if (logging_it) {
            fprintf(stderr, "[%d] C: %s", comm_fd, real_buf.data());
          }
        }


        char* search_pos = real_buf.data();
        // expecting DATA
        if (state == 5) {
          char* pos = strstr(search_pos, "\r\n.\r\n");
          if (pos != nullptr) {
            while (!recipients.empty()) {
              if (hasEnding(recipients.front(), "localhost")) {
                std::string trunc = recipients.front().substr(0, recipients.front().find("@"));
                // std::string full_path = path + "/" + trunc + ".mbox";
                
                
                // to assemble a single email, we need sender, recipient, timestamp, message
                

                recipients.pop();
                // int fd = open(full_path.c_str(), O_WRONLY | O_APPEND);

                // // Open file, acquire lock 
                
                // int returned = flock(fd, LOCK_EX | LOCK_NB);
                // if (returned < 0) {
                //   close(fd);
                //   retry_ctr = retry_ctr + 1;
                //   if (retry_ctr == 5) {
                //     search_pos = pos + 5;
                //     retry_ctr = 0;
                //     // Shift the remaining data in the buffer to the beginning (if any)
                //     int remaining = &real_buf[low] - search_pos;
                //     if (remaining > 0) {
                //         memmove(real_buf.data(), search_pos, remaining);
                //     }
                //     // Adjust the 'low' index for the remaining data
                //     low = remaining;
                //     continue;
                //   } else {
                //     continue;   //  try again
                //   }
                // }



                time_t timestamp;
                time(&timestamp);
                char* curr_time = ctime(&timestamp);
                size_t len = std::strlen(curr_time);
                if (len > 0 && curr_time[len - 1] == '\n') {
                    curr_time[len - 1] = '\0';
                }
                std::string header = "From " + sender + " " + curr_time;
                std::string our_sender;
                if (sender.length() >= 2) {
                  our_sender = sender.substr(1, sender.length() - 2);
                }
                // write(fd, header.c_str(), header.length()); // write header


                std::string email_content;
                email_content.append(real_buf.data(), &real_buf[low] - 3 - search_pos);

                bool is_telnet = true;
                std::vector<std::string> lines;
                std::istringstream stream(email_content);
                std::string line;
                while (std::getline(stream, line)) {
                  if (line.size() >= 2 && line.substr(line.size() - 1) == "\r") {
                      // Return the string without the last two characters
                   line = line.substr(0, line.size() - 1);
                  }
                  lines.push_back(line);
                }
                // Variables to hold the subject and the message
                std::string subject;
                std::string the_mess;

                // Parse the lines
                bool start_concatenating = false;
                size_t ignore_lines_after_encoding = -1;
                if (lines[0].find("Message-ID: ") == 0) {
                  is_telnet = false;
                  for (size_t i = 0; i < lines.size(); ++i) {
                      // Check if the line starts with "Subject: "

                      if (lines[i].find("Subject: ") == 0) {
                        subject = lines[i].substr(9); // Extract text after "Subject: "
                      }
                      // Check if the line starts with "Content-Transfer-Encoding:"
                      if (lines[i].find("Content-Transfer-Encoding:") == 0) {
                          ignore_lines_after_encoding = i + 2; // Set the index to skip 2 lines after this
                      }

                      // Start concatenating two lines after "Content-Transfer-Encoding:"
                      if (i == ignore_lines_after_encoding) {
                          start_concatenating = true;
                      }

                      if (start_concatenating && i < lines.size() - 1) { // Avoid the last line
                          the_mess += lines[i] + "\n";
                      }
                  }
                }



                // Remove the trailing '\n' or '\r' from the_mess and subject
                if (!the_mess.empty() && the_mess.back() == '\n') {
                    the_mess.pop_back();
                }

                if (is_telnet) {
                  subject = "Telnet Message";
                  the_mess = email_content;
                }

                // Print results
                std::cout << "Subject: " << subject << "\n";
                std::cout << "The Message:\n" << the_mess << "\n";

                
                std::string metadata = subject + ";" + trunc + ";" + string(curr_time);

                cout << "the following is the email content:\n" << the_mess << endl;
                cout << "the recipient is: --------- : " << trunc << endl;
                cout << "the current time is: " << string(curr_time) << endl;

                // write(fd, real_buf.data(), &real_buf[low] - 3 - search_pos);   // write email body without .<CRLF>
                string assembled_mess = assemble_single_email(our_sender, trunc, string(curr_time), the_mess);


                std::string uuid = compute_UUID(metadata);


                bool win = put_mailbox(trunc, "inbox", metadata, uuid, assembled_mess, "dummy", false);
                // write to mail


                // Release lock
                // flock(fd, LOCK_UN);


                // close(fd);
              } else {
                // send to external

                string curr_recp = recipients.front();
                recipients.pop();
                size_t atPos = curr_recp.find('@');
                std::string domain = curr_recp.substr(atPos + 1);


                unsigned char response[NS_PACKETSZ];
                ns_msg handle;

                // Query the MX records for the domain
                int len = res_query(domain.c_str(), C_IN, ns_t_mx, response, sizeof(response));
                if (len < 0) {
                    std::cerr << "Failed to query MX records for " << domain << std::endl;
                    continue;
                }

                // Initialize a ns_msg handle to parse the response
                if (ns_initparse(response, len, &handle) < 0) {
                    std::cerr << "Failed to parse DNS response" << std::endl;
                    continue;
                }

                // Get the number of answer records
                int answerCount = ns_msg_count(handle, ns_s_an);
                if (answerCount <= 0) {
                    std::cerr << "No MX records found for " << domain << std::endl;
                    continue;
                }

                std::cout << "MX Records for " << domain << ":" << std::endl;

                // Iterate over the answer records
                int count = 0;
                for (int i = 0; i < answerCount; ++i) {
                    ns_rr rr;
                    if (ns_parserr(&handle, ns_s_an, i, &rr) < 0) {
                        std::cerr << "Failed to parse answer record" << std::endl;
                        continue;
                    }

                    if (ns_rr_type(rr) == ns_t_mx) {
                        const unsigned char *rdata = ns_rr_rdata(rr);

                         
                        // Extract preference value (2 bytes)
                        uint16_t preference = ntohs(*(const uint16_t *)rdata);

                        // Extract the mail server domain name
                        char mailServer[NS_MAXDNAME];
                        if (dn_expand(response, response + len, rdata + 2, mailServer, sizeof(mailServer)) < 0) {
                            std::cerr << "Failed to extract mail server name" << std::endl;
                            continue;
                        }

                        int sockfd;
                        struct sockaddr_in serverAddr;
                        struct hostent *server;

                        std::cout << "Preference: " << preference << ", Mail Server: " << mailServer << std::endl;

                        /*
                        if (count < 2) {
                          count = count + 1;
                          continue;
                        }
                        */
                        // Create a socket
                        sockfd = socket(PF_INET, SOCK_STREAM, 0);
                        struct timeval timeout;
                        timeout.tv_sec = 10;  // 10 seconds timeout
                        timeout.tv_usec = 0;

                        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
                        if (sockfd < 0) {
                            perror("Error creating socket");
                            break;
                        }

                        // Resolve mail server address
                        server = gethostbyname(mailServer);
                        if (!server) {
                            std::cerr << "Error resolving mail server" << std::endl;
                            close(sockfd);
                            break;
                        }

                        // Set up the server address structure
                        bzero(&serverAddr, sizeof(serverAddr));
                        serverAddr.sin_family = AF_INET;
                        serverAddr.sin_port = htons(25); // SMTP port- use 25
                        // inet_pton(AF_INET, inet_ntoa(server->h_addr_list[0]), &(serverAddr.sin_addr));
                        // serverAddr.sin_addr.s_addr = htonl(*(u_long *) server->h_addr_list[0]);

                        struct in_addr **addr_list;


                        // memcpy(&serverAddr.sin_addr.s_addr, server->h_addr_list[0], server->h_length);
                        // bind(listen_fd, (sockaddr*)&servaddr, sizeof(servaddr));

                        addr_list = (struct in_addr **)server->h_addr_list;
                        for(int i = 0; addr_list[i] != NULL; i++) {
                            std::cout << inet_ntoa(*addr_list[i]) << std::endl;
                        }
                        serverAddr.sin_addr.s_addr = inet_addr(inet_ntoa(*addr_list[0]));
                        if (connect(sockfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
                          perror("Error connecting to mail server");
                          close(sockfd);
                          break;
                        }

                        // initial transmission
                        char buffer[1024];
                        int cnt = recv(sockfd, buffer, sizeof(buffer) - 1, 0); // Read initial server response
                        std::cout << buffer;
                        if (len > 0) {
                            buffer[len] = '\0';
                            std::cout << buffer; // Print server response
                            
                        } else {
                          close(sockfd);
                          break;
                        }
                        auto sendCommand = [&](const std::string &cmd, char* buffer) {
                          send(sockfd, cmd.c_str(), cmd.size(), 0);
                          int x = sizeof(buffer) - 1;
                          int len = recv(sockfd, buffer, 1024, 0);
                          if (len > 0) {
                              buffer[len] = '\0';
                              std::cout << buffer; // Print server response
                          }
                        };

                        
                        sendCommand("EHLO seas.upenn.edu\r\n", buffer);
                        if (buffer[0] != '2') {
                          close(sockfd);
                          break;
                        }

                        sendCommand("STARTTLS\r\n", buffer);
                        // if (buffer[0] != '2') {
                        //   close(sockfd);
                        //   break;
                        // }
                        if (buffer[0] != '2') {
                          close(sockfd);
                          break;
                        }

                        // setup and inintialize SSL
                        SSL_library_init();
                        SSL_load_error_strings();
                        const SSL_METHOD* method = TLS_client_method();
                        SSL_CTX* ctx = SSL_CTX_new(method);
                        if (!ctx) {
                            ERR_print_errors_fp(stderr);
                            close(sockfd);
                            break; // fail
                        }
                        SSL* ssl = SSL_new(ctx);
                        SSL_set_fd(ssl, sockfd);
                        if (SSL_connect(ssl) <= 0) {
                            ERR_print_errors_fp(stderr);
                            close(sockfd);
                            break; // fail
                        }


                        SSL_write(ssl, "EHLO seas.upenn.edu\r\n", strlen("EHLO seas.upenn.edu\r\n"));
                        int received = SSL_read(ssl, buffer, sizeof(buffer) - 1);
                        buffer[received] = '\0';
                        std::cout << buffer << std::endl;
                        std::string next = "MAIL FROM:" + sender + "\r\n";
                        SSL_write(ssl, next.c_str(), next.size());
                        int received7 = SSL_read(ssl, buffer, sizeof(buffer) - 1);
                        buffer[received7] = '\0';
                        std::cout << buffer << std::endl;
                        
                        // sendCommand("MAIL FROM:" + sender + "\r\n", buffer);
                        if (buffer[0] != '2') {
                          break;
                        }
                         
                        std::string next2 = "RCPT TO:<" + curr_recp + ">\r\n";
                        SSL_write(ssl, next2.c_str(), next2.size());
                        int received2 = SSL_read(ssl, buffer, sizeof(buffer) - 1);
                        buffer[received2] = '\0';
                        std::cout << buffer << std::endl;

                        // sendCommand("RCPT TO:<" + recipients.front() + ">\r\n", buffer);
                        if (buffer[0] != '2') {
                          close(sockfd);
                          break;
                        }
                        SSL_write(ssl, "DATA\r\n", strlen("DATA\r\n"));
                        int received3 = SSL_read(ssl, buffer, sizeof(buffer) - 1);
                        buffer[received3] = '\0';
                        std::cout << buffer << std::endl;                        
                        // sendCommand("DATA\r\n", buffer);
                        if (buffer[0] != '3') {
                          close(sockfd);
                          break;
                        }


                        time_t timestamp;
                        time(&timestamp);
                        char* curr_time = ctime(&timestamp);
                        std::string curr_time_cpp = curr_time;
                        std::string header = "From: " + sender + " " + curr_time;
                        std::string message = std::string(real_buf.data(), &real_buf[low] - 3 - search_pos) + ".\r\n";
                        // SSL_write(ssl, header.c_str(), header.size());
                        std::string to_hash = curr_time_cpp + " " + message;
                        unsigned char bufo [16];
                        computeDigest((char*) to_hash.c_str(), to_hash.size(), bufo);
                        std::string hash;
                        hash.reserve(32);  // C++11 only, otherwise ignore

                        for (std::size_t i = 0; i != 16; ++i)
                        {
                          hash += "0123456789ABCDEF"[bufo[i] / 16];
                          hash += "0123456789ABCDEF"[bufo[i] % 16];
                        }
                        size_t atPos = sender.find('@');
                        size_t endPos = sender.find('>');
                        std::string our_domain = sender.substr(atPos + 1, endPos - atPos - 1);


                        std::string second_header = "Message-ID: <" + hash + "@" + our_domain + ">\n";

                        std::string final_mess = header + second_header + message;
                        SSL_write(ssl, final_mess.c_str(), final_mess.size());
                        // int received4 = SSL_read(ssl, buffer, sizeof(buffer) - 1);
                        // buffer[received4] = '\0';
                        // std::cout << buffer << std::endl; 

                        // sendCommand(header + "\r\n", buffer);
                        std::cout << final_mess << std::endl;
                        
                        // SSL_write(ssl, message.c_str(), message.size());
                        int received5 = SSL_read(ssl, buffer, sizeof(buffer) - 1);
                        buffer[received5] = '\0';                       
                        // sendCommand(message + "\r\n.\r\n", buffer); // End the message with a single dot
                        std::cout << buffer << std::endl;
                        if (buffer[0] != '2') {
                          
                          close(sockfd);
                          break;
                        }
                        SSL_write(ssl, "QUIT\r\n", strlen("QUIT\r\n"));
                        int received6 = SSL_read(ssl, buffer, sizeof(buffer) - 1);
                        // sendCommand("QUIT\r\n", buffer);


                        SSL_free(ssl);
                        SSL_CTX_free(ctx);
                        close(sockfd);
      


                        break;



                    }
                }
                





                // recipients.pop();
                // write(fd, header.c_str(), header.length()); // write header
                // write(fd, real_buf.data(), &real_buf[low] - 3 - search_pos);   // write email body without .<CRLF>                

              }
            }


            std::string response = "250 OK\r\n";
            if (logging_it) {
                fprintf(stderr, "[%d] S: %s", comm_fd, response.c_str());
            }
            write(comm_fd, response.c_str(), response.length());

            search_pos = pos + 5;
            state = 0;
          }

        } else {
            while (state != 5 && true) {
                // Search for "\r\n" in the current buffer
                char* pos = strstr(search_pos, "\r\n");
                if (pos != nullptr) {
                    // Process data up to the position of "\r\n"
                    int chunk_len = pos - search_pos;

                    if (chunk_len >= 4) {
                        // check command
                        char command[6];
                        strncpy(command, search_pos, 5);
                        command[5] = '\0';

                        std::transform(command, command + 5, command, ::tolower);

                        // Command handling
                        if ((state == -1 || state == 0) && strncmp(command, "helo ", 5) == 0) {
                          if (*(search_pos + 5) == '\r') {
                            // We enter this logic if no argument was provided for helo
                            std::string response = "501 Syntax error in parameters or arguments\r\n";
                            if (logging_it) {
                                fprintf(stderr, "[%d] S: %s", comm_fd, response.c_str());
                            }
                            write(comm_fd, response.c_str(), response.length());
                          } else {
                            std::string response = "250 localhost\r\n";
                              if (logging_it) {
                                  fprintf(stderr, "[%d] S: %s", comm_fd, response.c_str());
                              }
                              write(comm_fd, response.c_str(), response.length());

                              state = 0;
                          }                         

                        } else if ((state == 0 || state == 1) && strncmp(command, "mail ", 5) == 0) {
                          std::string comp = std::string(search_pos + 5, search_pos + 10);
                          std::transform(comp.begin(), comp.end(), comp.begin(),
                   [](unsigned char c){ return std::tolower(c); });
                          bool param_correct = comp == "from:";
                          sender = std::string(search_pos + 10, chunk_len - 10);
                          if (param_correct && std::regex_match(sender, send_match)) {
                            std::string response = "250 OK\r\n";
                              if (logging_it) {
                                  fprintf(stderr, "[%d] S: %s", comm_fd, response.c_str());
                              }
                              write(comm_fd, response.c_str(), response.length());
                            state = 1;
                          } else {
                            std::string response = "501 Syntax error in parameters or arguments\r\n";
                            if (logging_it) {
                                fprintf(stderr, "[%d] S: %s", comm_fd, response.c_str());
                            }
                            write(comm_fd, response.c_str(), response.length());                            
                          }


                        } else if ((state == 1 || state == 2) && strncmp(command, "rcpt ", 5) == 0) {
                          std::string comp = std::string(search_pos + 5, search_pos + 8);
                          std::transform(comp.begin(), comp.end(), comp.begin(),
                   [](unsigned char c){ return std::tolower(c); });
                          bool param_correct = comp == "to:";
                          std::string recipient = std::string(search_pos + 8, chunk_len - 8);
                          if (param_correct && std::regex_match(recipient, recp_match)) {
                            // std::smatch Matches;
                            // std::regex_search(recipient, Matches, recp_match);
                            // // std::unordered_map<std::string, pthread_mutex_t>& non_volatile_mutexes = const_cast<std::unordered_map<std::string, pthread_mutex_t>&>(mutexes);
                            // std::string search_for = std::string(Matches[1]) + ".mbox";
                            // if (hasEnding(recipient, "localhost>") && non_volatile_mutexes.count(search_for) == 0) {
                            //   std::string response = "550 File doesn't exist\r\n";
                            //   if (logging_it) {
                            //       fprintf(stderr, "[%d] S: %s", comm_fd, response.c_str());
                            //   }
                            //   write(comm_fd, response.c_str(), response.length());  
                            // } else {
                              // .mbox file exists in target folder or we are sending external
                              recipients.push(recipient.substr(1, recipient.size() - 2));
                              
                              std::string response = "250 OK\r\n";
                                if (logging_it) {
                                    fprintf(stderr, "[%d] S: %s", comm_fd, response.c_str());
                                }
                                write(comm_fd, response.c_str(), response.length());

                                state = 2;
                            
                            
                          } else {
                            std::string response = "501 Syntax error in parameters or arguments\r\n";
                            if (logging_it) {
                                fprintf(stderr, "[%d] S: %s", comm_fd, response.c_str());
                            }
                            write(comm_fd, response.c_str(), response.length());                                
                          }

                        } else if (state == 2 && strncmp(command, "data\r", 5) == 0) {
                          // std::cout << recipient << std::endl;
                          std::string response = "354 Start mail input; end with <CRLF>.<CRLF>\r\n";
                          
                            if (logging_it) {
                                fprintf(stderr, "[%d] S: %s", comm_fd, response.c_str());
                            }
                            write(comm_fd, response.c_str(), response.length());
                            state = 5;
                        }                    
                        else if (state != -1 && strncmp(command, "quit\r", 5) == 0) {
                            std::string response = "221 *\r\n";

                            if (logging_it) {
                                fprintf(stderr, "[%d] S: %s", comm_fd, response.c_str());
                            }

                            write(comm_fd, response.c_str(), response.length());

                          close(comm_fd);
                          std::unordered_set<int>& non_volatile_fds = const_cast<std::unordered_set<int>&>(fds);
                          non_volatile_fds.erase(comm_fd);
                          if (logging_it) {
                              fprintf(stderr, "[%d] Connection closed\n", comm_fd);
                          }

                          pthread_exit(NULL);
                        } else if (state != -1 && strncmp(command, "noop\r", 5) == 0) {
                          std::string response = "250 Requested mail action okay, completed\r\n";
                            if (logging_it) {
                                fprintf(stderr, "[%d] S: %s", comm_fd, response.c_str());
                            }
                            write(comm_fd, response.c_str(), response.length());
                        } else if (state != -1 && strncmp(command, "rset\r", 5) == 0) {
                          std::string response = "250 Requested mail action okay, completed\r\n";
                            if (logging_it) {
                                fprintf(stderr, "[%d] S: %s", comm_fd, response.c_str());
                            }
                            write(comm_fd, response.c_str(), response.length());
                            state = 0;                          
                        }
                        else {
                          if (valid.find(command) != valid.end()) {
                            std::string response = "503 ERR Out of order\r\n";
                            write(comm_fd, response.c_str(), response.length());                            
                          } else {
                            std::string response = "500 Syntax error: command unrecognized\r\n";
                            write(comm_fd, response.c_str(), response.length());
                          }

                        }
                    } else {
                        std::string response = "500 syntax error: command unrecognized\r\n";
                        write(comm_fd, response.c_str(), response.length());
                    }

                    // Skip past the "\r\n"
                    search_pos = pos + 2;
                } else {
                    // No more "\r\n" found in the current buffer
                    break;
                }
            }
        }



        // Shift the remaining data in the buffer to the beginning (if any)
        int remaining = &real_buf[low] - search_pos;
        if (remaining > 0) {
            memmove(real_buf.data(), search_pos, remaining);
        }

        // Adjust the 'low' index for the remaining data
        low = remaining;
    }
}

void stopServer(int arg) {
	shut_it_down = true;
	// set every connection to non-blocking, write goodbye message, and close
	std::unordered_set<int>& non_volatile_fds = const_cast<std::unordered_set<int>&>(fds);
    for (auto itr = non_volatile_fds.begin(); itr != non_volatile_fds.end(); ++itr) {
    	// set to non-blocking
    	int opts = fcntl(*itr, F_GETFL);
    	if (opts < 0) {
    		fprintf(stderr, "F_GETFL failed (%s)\n", strerror(errno));
		}
		opts = (opts | O_NONBLOCK);
		if (fcntl(*itr, F_SETFL, opts) < 0) {
			fprintf(stderr, "F_SETFL failed (%s)\n", strerror(errno));
		}

		// shutdown
		std::string shutdown_message = "-ERR Server shutting down\n";
        write(*itr, shutdown_message.c_str(), shutdown_message.length());
    	close(*itr);
    	if (logging_it) {
        	fprintf(stderr, "[%d] Connection closed\n", *itr);
    	}
    }

    exit(1); // stop program
}


int main(int argc, char *argv[])
{
	signal(SIGINT, stopServer);

	// Parse arguments
	int c;
	bool exit_imed = false;
	int port = 2500;
	while ((c = getopt (argc, argv, "avp:")) != -1)
		switch (c)
			{
			case 'a':
				exit_imed = true;
				break;
		    case 'v':
		    	logging_it = true;
		    	break;
		    case 'p':
		    	port = atoi(optarg);
		    	break;
		    default:
		    	abort();
		  }

	// -a given
	if (exit_imed) {
		fprintf(stderr, "Edmund Doerksen - esd25\n");
		exit(1);
	}

	// socket()
	int listen_fd = socket(PF_INET, SOCK_STREAM, 0);
	if (listen_fd < 0) {
		fprintf(stderr, "Cannot open socket");
		exit(1);
	}

	// bind()
	struct sockaddr_in servaddr;
	bzero(&servaddr, sizeof(servaddr));	// clears servaddr fields
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htons(INADDR_ANY);
	servaddr.sin_port = htons(port);
	bind(listen_fd, (sockaddr*)&servaddr, sizeof(servaddr));

	listen(listen_fd, 100);

  // no host folder arg provided
  // if (optind == argc) {
  //   fprintf(stderr, "Please provide an email host folder");
  //   exit(1);
  // }

  // path = argv[optind];

    // commented out: all emails should send successfully


    
    // DIR *dir;
    // struct dirent *ent;

    // // Open the directory
    // if ((dir = opendir(path.c_str())) != nullptr) {
    //     // Loop through all the entries
    //     while ((ent = readdir(dir)) != nullptr) {
    //         if (ent->d_type == DT_REG) {  // Check if it's a regular file
    //             std::unordered_map<std::string, pthread_mutex_t>& non_volatile_mutexes = const_cast<std::unordered_map<std::string, pthread_mutex_t>&>(mutexes);
    //             pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
    //             non_volatile_mutexes[std::string(ent->d_name)] = file_mutex;
    //         }
    //     }
    //     closedir(dir);
    // } else {
    //     std::cerr << "Could not open directory!" << std::endl;
    // }

	while (true) {
		struct sockaddr_in clientaddr;
		socklen_t clientaddrlen = sizeof(clientaddr);
		int* comm_fd = new int;
		*comm_fd = accept(listen_fd, (struct sockaddr*)&clientaddr, &clientaddrlen);

		if (*comm_fd < 0) {
		    fprintf(stderr, "accept failed");
		    delete comm_fd;
		    continue;
		}

	    std::unordered_set<int>& non_volatile_fds = const_cast<std::unordered_set<int>&>(fds);
		// close connection if we are shutting down
		if (shut_it_down) {
			close(*comm_fd);
			delete comm_fd;
			continue;
		}

	    non_volatile_fds.insert(*comm_fd);

	    if (logging_it) {
	    	fprintf(stderr, "[%d] New connection\n", *comm_fd);
	    }

		std::string greeting_mess = "220 localhost *\r\n";
		write(*comm_fd, greeting_mess.c_str(), greeting_mess.length());

		// spin off thread
		pthread_t thread;
		pthread_create(&thread, NULL, worker, comm_fd);
		pthread_detach(thread);
	}

  return 0;
}
