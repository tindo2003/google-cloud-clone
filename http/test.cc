#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include <iostream>
#include <signal.h>
#include <sstream>
#include <unordered_map>
#include <ctime>
#include <iomanip>
#include <vector>
#include <fstream>
#include "template_render.h"
#include "email_api.h"
#include "storage_request.h"
#include "storage_api.h"

int main(int argc, char *argv[]) {

  // test code
  std::string status_code = "200";
  int a = 5;
      std::vector<std::string> inputs = {
        "edmund",
        "sent",
        "test;g;9:30",
        "abcde",
        "hi how are you"
    };


      std::vector<std::string> next_inputs = {
        "edmund",
        "sent",
        "lobotomy;jeje;9:57",
        "rdqd",
        "this is the end of the weekend\n i dont wanna wait"
    };
  // put_mailbox(inputs.at(0), inputs.at(1), inputs.at(2), inputs.at(3), inputs.at(4), status_code);
  // put_mailbox(next_inputs.at(0), next_inputs.at(1), next_inputs.at(2), next_inputs.at(3), next_inputs.at(4), status_code);
  size_t test_size = 0;
  ResponseResult result = init_response_result();
  // bool did_we_succeed = get_response(SERVER_IP, SERVER_PORT, "PUT", "edmund_email", "sent", binary_data, test_size, NULL, 0, &result);
  // std::cout << did_we_succeed << std::endl;
  // int c = add(3, 5);
  // cout << get_mailbox_or_email("edmund", "sent", status_code) << endl;

  size_t res_size = 0;
  // cout << get_mailbox_or_email("edmund", "rdqd", status_code) << endl;

  std::string test1 = "mason;addison@gmail.com;2022-12-12T11:00:00Z;58312302bf43fd44;;NYT: resubscribe this weekend for $1;nytimes@gmail.com;2021-10-12T9:00:00Z;158312302b7c92et";
  std::string test2 = "";
  std::string test3 = "Good News!;addison@gmail.com;2022-12-12T11:00:00Z;58312302bf43fd44";
  string test4 = "adadfasf;asdfwef;awefasdf;asf22;;Good News!;addison@gmail.com;2022-12-12T11:00:00Z;58312302bf43fd44;;NYT: resubscribe this weekend for $1;nytimes@gmail.com;2021-10-12T9:00:00Z;158312302b7c92et";
  std::cout << assemble_mailbox(test1) << "\n"
              << assemble_mailbox(test2) << "\n"
              << assemble_mailbox(test3) << "\n"
              << assemble_mailbox(test4) << std::endl;

  std::cout << assemble_single_email("edmund", "freeze", "2022-12-12T11:00:00Z", "why hello there\n i hope your are well") << "\n"
              << assemble_single_email("jacob@javits.com", "wilma-fricken-rudolf", "2022-12-12T11:00:00Z", "good for you i am doing radder well") << "\n"
         << std::endl;
  // worst
  return 0;


}