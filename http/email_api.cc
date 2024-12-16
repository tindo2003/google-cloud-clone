#include "storage_request.h"
#include "storage_api.h"
#include "email_api.h"
#include <string>
#include <iostream>
#include <openssl/md5.h>
#include <sstream>
#include <vector>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <resolv.h>
using namespace std;

// get inbox or outbox. i.e. get list of email UUIDs from the correct column
std::string get_mailbox_or_email(std::string user_id, std::string mailbox_name, std::string status_code) {
  std::string row = std::string(user_id) + "_email";
  std::cout << "Inside get_mailbox(). The row is: " << row << std::endl;
  ResponseResult result = init_response_result();   
    // retrieve the UUID
  bool did_we_succeed = get_response(SERVER_IP, SERVER_PORT, "GET", row.c_str(), mailbox_name.c_str(), NULL, 0, NULL, 0, &result);
  cout << did_we_succeed << endl;
  // Analyze the result object:

  // no emails in the mailbox or no email exists (i think the former case is the only one possible)
  if (!did_we_succeed) {
    return "";
  }
  // result.content field, binary result
  char* existing_data = binary_to_string(result.content, result.content_length);

  return std::string(existing_data);

}

// put mailbox. first append to list of UUIDs, then put in the table
bool put_mailbox(std::string user_id, std::string mailbox_name, std::string metadata, std::string uuid, std::string email, std::string status_code, bool mail_only) {
  size_t binary_size = 0;


  std::string row = std::string(user_id) + "_email";
  if (!mail_only) {
    std::cout << "Inside put_mailbox(). The row is: " << row << std::endl;
    ResponseResult result = init_response_result();   
      // retrieve the UUID
    bool did_we_succeed = get_response(SERVER_IP, SERVER_PORT, "GET", row.c_str(), mailbox_name.c_str(), NULL, 0, NULL, 0, &result);
    cout << did_we_succeed << endl;
    std::string uuids_cpp;

    if (!did_we_succeed) {
      uuids_cpp = "" + metadata + ";" + uuid; // we are initializing the "mailbox" column
    } else {
      uuids_cpp = binary_to_string(result.content, result.content_length);
      uuids_cpp = uuids_cpp + ";;" + metadata + ";" + uuid;
    }

    cout << "Current value of uuids_cpp: " << uuids_cpp << endl;

    void *binary_data = string_to_binary(uuids_cpp.c_str(), &binary_size);

    // call put back to the relevant column
    ResponseResult result_second = init_response_result();
    bool did_we_succeed_3 = get_response(SERVER_IP, SERVER_PORT, "PUT", row.c_str(), mailbox_name.c_str(), binary_data, binary_size, NULL, 0, &result_second);
    if (!did_we_succeed_3) {
      fprintf(stderr, "Unable to put updated metadata back in KVS\n");
    }
  }



  // put email contents in kvs
  size_t binary_size_2 = 0;
  void *binary_data_2 = string_to_binary(email.c_str(), &binary_size_2);
  cout << "HIIIIIII" << endl;
  ResponseResult result2 = init_response_result(); 
  bool did_we_succeed_email = get_response(SERVER_IP, SERVER_PORT, "PUT", row.c_str(), uuid.c_str(), binary_data_2, binary_size_2, NULL, 0, &result2);
  return did_we_succeed_email;
}

// delete email- or delete just the email metadata
bool delete_email(std::string user_id, std::string mailbox_name, std::string uuid, std::string status_code, bool meta_only) {
  std::string mailbux = get_mailbox_or_email(user_id, mailbox_name, status_code);

  // return false if the get_mailbox_or_email() failed
  if (!meta_only && mailbux.empty()) {  // Check if the string is empty
    return false;
  }
  // find the current email's metadata - no error handling exists for deleting emails that don't exist
  size_t position = mailbux.find(uuid);
  if (position == std::string::npos) {
    return true;   // do nothing if we find no email: there is nothing to delete: this can only happen with meta_only = true
  }

  cout << "the uuid is " << uuid << endl;
  cout << "the position is " << position << endl;

  // find front of section we are removing
  size_t front = mailbux.rfind(";;", position);
  size_t back = mailbux.find(";;", position);
  // this means we're the only email in the mail column and need to delete from the correct mailbox
  bool are_we_alone = false;
  if (front == std::string::npos && back == std::string::npos) {
    are_we_alone = true;
  }


  cout << "the initial front value is " << front << endl;

  cout << "the string length is " << mailbux.size() << endl;
  if (front == std::string::npos) {
      front = 0; // Start of the string
  } else {
      front += 2; // Move to the position after the semicolon
  }


  cout << "the second front value is " << front << endl;


  // find the back
  
  if (back == std::string::npos) {
      back = mailbux.size();
  } else {
      back += 2; // Move to the position after the semicolon
  }


  cout << "the back is: " << back << endl;

  // erase the semicolons in front of us if we are at the back
  if (back == mailbux.size()) {
    if (front != 0) {
      front -= 2;
    }
  }

  cout << "the front is: " << front << endl;

  // Delete meta-entry and put back in mailbox
  
  size_t binary_size = 0;
  void *binary_data;
  // edit the mailbox metadata only if we actually find the email. We won't find the email if this is first time reply
  if (back - front > 0) {
    mailbux = mailbux.erase(front, back - front);
    binary_data = string_to_binary(mailbux.c_str(), &binary_size);
    // call put back to the relevant column
    std::string row = user_id + "_email";
    ResponseResult result = init_response_result();
    if (are_we_alone) {
      if (!get_response(SERVER_IP, SERVER_PORT, "DELETE", row.c_str(), mailbox_name.c_str(), NULL, 0, NULL, 0, &result)) {
        fprintf(stderr, "Failed to delete email metadata and wipe mailbox\n");
        return false;
      }
    } else {
      if (!get_response(SERVER_IP, SERVER_PORT, "PUT", row.c_str(), mailbox_name.c_str(), binary_data, binary_size, NULL, 0, &result)) {
        fprintf(stderr, "Failed to delete email metadata\n");
        return false;
      }
    }
  }

  std::string the_row = user_id + "_email";
  ResponseResult the_result = init_response_result();

  // now delete the email contents from the kvs
  if (!meta_only) {
    bool did_we_succeed_2 = get_response(SERVER_IP, SERVER_PORT, "DELETE", the_row.c_str(), uuid.c_str(), NULL, 0, NULL, 0, &the_result);
    if (!did_we_succeed_2) {
        fprintf(stderr, "Failed to delete email contents\n");
        return false;      
    }
  }
  return true;
}


// ************** functions for computing UUID ****************


void computeDigest(char *data, int dataLengthBytes, unsigned char *digestBuffer)
{
  /* The digest will be written to digestBuffer, which must be at least MD5_DIGEST_LENGTH bytes long */

  MD5_CTX c;
  MD5_Init(&c);
  MD5_Update(&c, data, dataLengthBytes);
  MD5_Final(digestBuffer, &c);
}

std::string compute_UUID(std::string meta) {
  unsigned char bufo [16];
  computeDigest((char*) meta.c_str(), meta.size(), bufo);
  std::string hash;
  hash.reserve(32);  // C++11 only, otherwise ignore

  for (std::size_t i = 0; i != 16; ++i)
  {
    hash += "0123456789abcdef"[bufo[i] / 16];
    hash += "0123456789abcdef"[bufo[i] % 16];
  }
  return hash;
}


std::string assemble_mailbox(const std::string& metadataString) {
    std::vector<std::string> emails;
    std::string delimiter = ";;";
    size_t start = 0;
    size_t end = metadataString.find(delimiter);

    // Split the input string by ';;'
    while (end != std::string::npos) {
        std::string email = metadataString.substr(start, end - start);
        if (!email.empty()) {
            emails.push_back(email);
        }
        start = end + delimiter.length();
        end = metadataString.find(delimiter, start);
    }

    if (start < metadataString.length()) {
        emails.push_back(metadataString.substr(start));
    }

    // Create a JSON-like string manually
    std::ostringstream result;
    result << "[";

    for (size_t i = 0; i < emails.size(); ++i) {
        std::istringstream emailStream(emails[i]);
        std::string subject, sender, timestamp, uuid;

        // Split each email metadata into its components using ';'
        if (std::getline(emailStream, subject, ';') &&
            std::getline(emailStream, sender, ';') &&
            std::getline(emailStream, timestamp, ';') &&
            std::getline(emailStream, uuid, ';')) {

            result << "{\"target\": \"" << sender << "\", "
                   << "\"subject\": \"" << subject << "\", "
                   << "\"timestamp\": \"" << timestamp << "\", "
                   << "\"uuid\": \"" << uuid << "\"}";

            if (i < emails.size() - 1) {
                result << ", ";
            }
        }
    }

    result << "]";
    return result.str();
}


std::string assemble_single_email(std::string sender, std::string recipient, std::string timestamp, std::string message) {
    std::ostringstream json;
    json << "[{";
    json << "\"sender\": \"" << sender << "\", ";
    json << "\"recipient\": \"" << recipient << "\", ";
    json << "\"timestamp\": \"" << timestamp << "\", ";
    json << "\"message\": \"" << message << "\"";
    json << "}]";
    return json.str();
}


std::string assemble_email_display(std::string uuid, std::string target, std::string subject, std::string message) {
    std::ostringstream json;
    json << "{";
    json << "\"uuid\": \"" << uuid << "\", ";
    json << "\"target\": \"" << target << "\", ";
    json << "\"subject\": \"" << subject << "\", ";
    json << "\"message\": " << message; // No escaping or quotes, as 'message' is a JSON array
    json << "}";
    return json.str();
}

bool send_external(std::string address, std::string uuid, std::string message, std::string sender) {
  size_t atPos = address.find('@');
  std::string domain = address.substr(atPos + 1);
  if (sender.find('@') != std::string::npos) {
    sender = sender.substr(0, sender.find('@'));  // remove the domain part since we will replace with seas.upenn.edu
  }
  
  sender = "<" + sender + "@seas.upenn.edu" + ">";

  cout << "the sender is: " << sender << endl;
  cout << "the address is: " << address << endl;
  cout << "the message is: " << message << endl;

  unsigned char response[NS_PACKETSZ];
  ns_msg handle;

  // Query the MX records for the domain
  int len = res_query(domain.c_str(), C_IN, ns_t_mx, response, sizeof(response));
  if (len < 0) {
      std::cerr << "Failed to query MX records for " << domain << std::endl;
      return false;
  }


  // Initialize a ns_msg handle to parse the response
  if (ns_initparse(response, len, &handle) < 0) {
      std::cerr << "Failed to parse DNS response" << std::endl;
      return false;
  }

  // Get the number of answer records
  int answerCount = ns_msg_count(handle, ns_s_an);
  if (answerCount <= 0) {
      std::cerr << "No MX records found for " << domain << std::endl;
      return false;
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
              return false;
          }

          // Resolve mail server address
          server = gethostbyname(mailServer);
          if (!server) {
              std::cerr << "Error resolving mail server" << std::endl;
              close(sockfd);
              return false;
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
            return false;
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
            return false;
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

          cout << "hello my firends" << endl;
          sendCommand("EHLO seas.upenn.edu\r\n", buffer);
          if (buffer[0] != '2') {
            close(sockfd);
            return false;
          }

          sendCommand("STARTTLS\r\n", buffer);
          // if (buffer[0] != '2') {
          //   close(sockfd);
          //   break;
          // }
          if (buffer[0] != '2') {
            close(sockfd);
            return false;
          }

          // setup and inintialize SSL
          SSL_library_init();
          SSL_load_error_strings();
          const SSL_METHOD* method = TLS_client_method();
          SSL_CTX* ctx = SSL_CTX_new(method);
          if (!ctx) {
              ERR_print_errors_fp(stderr);
              close(sockfd);
              return false; // fail
          }
          SSL* ssl = SSL_new(ctx);
          SSL_set_fd(ssl, sockfd);
          if (SSL_connect(ssl) <= 0) {
              ERR_print_errors_fp(stderr);
              close(sockfd);
              return false; // fail
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
            return false;
          }
            
          std::string next2 = "RCPT TO:<" + address + ">\r\n";
          SSL_write(ssl, next2.c_str(), next2.size());
          int received2 = SSL_read(ssl, buffer, sizeof(buffer) - 1);
          buffer[received2] = '\0';
          std::cout << buffer << std::endl;

          // sendCommand("RCPT TO:<" + recipients.front() + ">\r\n", buffer);
          if (buffer[0] != '2') {
            close(sockfd);
            return false;
          }
          SSL_write(ssl, "DATA\r\n", strlen("DATA\r\n"));
          int received3 = SSL_read(ssl, buffer, sizeof(buffer) - 1);
          buffer[received3] = '\0';
          std::cout << buffer << std::endl;                        
          // sendCommand("DATA\r\n", buffer);
          if (buffer[0] != '3') {
            close(sockfd);
            return false;
          }


          time_t timestamp;
          time(&timestamp);
          char* curr_time = ctime(&timestamp);
          std::string curr_time_cpp = curr_time;
          std::string header = "From: " + sender + " " + curr_time;
          // SSL_write(ssl, header.c_str(), header.size());
          size_t atPos = sender.find('@');
          size_t endPos = sender.find('>');
          std::string our_domain = sender.substr(atPos + 1, endPos - atPos - 1);


          std::string second_header = "Message-ID: <" + uuid + ">\n";

          std::string final_mess = header + second_header + message + "\r\n.\r\n";
          cout << "the final_mess is: " << final_mess << endl;
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
            return false;
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
  





  return true;
}
