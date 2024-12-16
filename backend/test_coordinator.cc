#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string>
#include <map>
#include <vector>
#include <sstream>

struct sockaddr_in be_dest;
struct sockaddr_in fe_dest{};
int be_sock;
int fe_sock;
std::map<int, time_t> backend_servers;
typedef struct {
	std::string method;
    std::string rowKey;
    std::string colKey;
    std::string httpHeader;
    int content_length;

} Method_data;

int main(int argc, char *argv[])
{

  if (argc != 2) {
    fprintf(stderr, "*** Author: Eesha Shekhar (eshekhar)\n");
    exit(1);
  }
  //need to get the port number
  char *port_num_fe = argv[1];


  if (port_num_fe == NULL) {
    	  //error
  	  fprintf(stderr, "Port number FE not found \n");
      }

  int port_no_fe = atoi(port_num_fe);
  if (port_no_fe <= 0) {
	  fprintf(stderr, "Incorrect port number \n");
  }
  fprintf(stderr, "port number:%d\n", port_no_fe);

  fe_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (fe_sock < 0) {
  fprintf(stderr, "Cannot open socket (%s)\n", strerror(errno));
  exit(1);
  }



      fe_dest.sin_family = AF_INET;

      fe_dest.sin_port = htons(port_no_fe);
      inet_pton(AF_INET, "127.0.0.1", &fe_dest.sin_addr);
      if (connect(fe_sock, (struct sockaddr *)&fe_dest, sizeof(fe_dest)) < 0) {
    	  fprintf(stderr, "error connecting\n");
      }
      std::string http_request_1 = "GET /row123/col456 HTTP/1.1\r\nContent-Length: 0\r\n\r\n";
      std::string http_request_2 = "PUT /row123/col456 HTTP/1.1\r\nContent-Length: 13\r\nContent-Type: application/octet-stream\r\n\r\nbinary data";
      std::string http_request_ = "PUT /row124/col456 HTTP/1.1\r\nContent-Length: 13\r\nContent-Type: application/octet-stream\r\n\r\nbinary data";
      if (send(fe_sock, http_request_1.c_str(), http_request_1.size(), 0) < 0) {
    	  fprintf(stderr, "error sending\n");
      }
      std::vector<char> buf(1000);
      int rlen = recv(fe_sock, buf.data(), buf.size(), 0);
      fprintf(stderr, "buffer is:%s\n", buf.data());
      int sock_be_server = socket(AF_INET, SOCK_STREAM, 0);
      	struct sockaddr_in c_addr;
      	bzero(&c_addr, sizeof(c_addr));
      	std::string coordinator_ip_address = "127.0.0.1";
      	c_addr.sin_family = AF_INET;
      	c_addr.sin_port = htons(8003);
      	inet_pton(AF_INET, coordinator_ip_address.c_str(), &(c_addr.sin_addr));
      	connect(sock_be_server, (struct sockaddr *)&c_addr, sizeof(c_addr));
      	for (int i = 0; i < 1; i++) {
      		std::string http_request_test = "PUT /row123/col45" + std::to_string(i)+ " HTTP/1.1\r\nContent-Length: 11\r\nContent-Type: application/octet-stream\r\n\r\nbinary data";
      		if (send(sock_be_server, http_request_test.c_str(), http_request_test.size(), 0) < 0) {
      		      	    	  fprintf(stderr, "error sending\n");
      		      	      }
      	}
//      	if (send(sock_be_server, http_request_1.c_str(), http_request_1.size(), 0) < 0) {
//      	    	  fprintf(stderr, "error sending\n");
//      	      }
////      	if (send(sock_be_server, http_request_2.c_str(), http_request_2.size(), 0) < 0) {
////      	      	    	  fprintf(stderr, "error sending\n");
////      	      	      }
//      	int rlen2 = recv(fe_sock, buf.data(), buf.size(), 0);
//      	      fprintf(stderr, "buffer is:%s\n", buf.data());


  return 0;
}

