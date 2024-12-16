#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <string>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <regex>
#include <fstream>
#include <dirent.h>
#include <sys/file.h>
#include "KV.h"
#include <filesystem>
#include "../http/server.h"
#include "../http/cache.h"
#include "../http/storage_api.h"
#include "../http/storage_request.h"
#include <atomic>
#include "../http/nlohmann/json.hpp"
#include <fcntl.h>

#define SEPARATOR "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF"
#define HB_INTERVAL 2
#define CHECKPOINT_INTERVAL 3000
int heartbeat_sockfd;

typedef struct {
	bool HELO_received;
	int curr_state;
	std::string HELO_domain;
	std::string mail_from;
	std::vector<std::string> rcpt_email_address_arr;
	std::vector<std::string> rcpt_mailbox_path_arr;
	std::string email_data;
	bool first_time_writing_to_email;
} state;

std::string config_file;
int sock_coordinator;
volatile bool allow_run = true;
int cp_done_received = 0;
typedef struct {
	std::string method;
    std::string rowKey;
    std::string colKey;
    std::string httpHeader;
    int content_length;
    std::string content_type;
    std::vector<char> binary_data;
    std::vector<char> combined_CPUT;
    bool ready_for_binary_data;
    bool finished_processing_bin_data;
    int tablet_id;
    int comm_fd;
    int sequence_num;
    int curr_seq;
} Method_data;
typedef struct {
int version_num;
std::vector<Method_data> method_vec;
} Checkpoint_entries;

typedef struct  {
	std::string forwarding_address;
	int forwarding_port_num;
	struct sockaddr_in server_addr;
} ServerData;

// Rollback Struct
struct OldValue {
    bool existed;
    std::vector<char> data;
    std::string data_type;
    int size;
};
std::map<int, OldValue> old_values_map_backup;

std::vector<ServerData> allServers;
std::map <std::string, pthread_mutex_t> mutexes_map;
std::vector<pthread_mutex_t> mutex_for_checkpoint(15);
std::map <std::string, std::ofstream> ofstream_mailbox_path_map;
std::map <int, Checkpoint_entries> map_of_checkpoint_entries;
volatile bool shutting_down = false;
volatile int connections_arr[10000] = {0};
std::vector<int> fds;
int num_of_mailboxes = 0;
char first_msg[] = "220 localhost Service Ready\r\n";
std::string  helo_cmd = "250 localhost\r\n";
char command_too_long[] = "-ERR Command is too long\r\n";
char command_unknown_cmd_msg[] = "-ERR Unknown command\r\n";
std::string ok_msg = "250 OK\r\n";
std::string quit_msg = "221 localhost Service closing transmission channel\r\n";
std::string unknown_cmd_msg = "500 Syntax error, command unrecognized\r\n";
char goodbye_msg[] = "+OK Goodbye!\r\n";
char shutting_down_msg[] = "421 localhost Service not available, closing transmission channel\r\n";
char end[] = "\r\n";
std::string bad_seq_msg = "503 Bad sequence of commands \r\n";
std::string syntax_err_msg = "501 Syntax error in parameters or arguments\r\n";
std::string recipient_err_msg = "550 Requested action not taken: mailbox unavailable\r\n";
std::string data_msg = "354 Start mail input; end with <CRLF>.<CRLF>\r\n";
int print_output = 0;
pthread_t pthreads[10000];
int listen_fd;
std::string mbox;
int retries = 5;
std::string ok_http = "200 OK";
std::string bad_http = "500 Internal Server Error";
std::string binding_address_for_main;
int binding_port_num_for_main;
int server_num;
int is_primary = 0;

static int TOTAL_TABLETS = 15;
static int TOTAL_NODES = 3;
static int REPLICAS = 3;
static int TABLETS_PER_GROUP = 5;
int my_node_id; 
std::string checkpointfolder = "checkpoints";
std::string logs = "logs";
std::map <std::string, std::ifstream> ifstream_checkpoints;
std::map <int, std::ofstream> log_writers;
std::map <int, std::ofstream> checkpoint_writers;
//std::map <int, int> tablet_to_checkpoint_version;
std::map <std::string, int> row_to_sequence_num;
//std::map <int, int> tablet_to_checktablet_to_checkpoint_verpoint_ver;
std::map <int, Method_data> requests_remaining;

pthread_mutex_t mutex_for_log;
int MAX_KV_SIZE = 10;
int CURR_KV_SIZE = 0;
int checkpointing_mode = 0;
bool ready_to_checkpoint = false;
int first_run = 0;
int suspend_checkpointing;
std::vector<int> suspend_tablet_checkpoint;
std::vector<int> initialize = getAllTabletsForNode(my_node_id);




bool do_write(int fd, const char *buf, int len) {
	int sent = 0;
	while (sent < len) {
		int n = write(fd, &buf[sent],len-sent);
		if (n<0) {
			return false;
		}
		sent += n;
	}
	return true;
}

bool do_write_vector(int fd, const std::vector<char>& buf, int len) {
	int sent = 0;
	while (sent < len) {
		int n = write(fd, &buf[sent],len-sent);
		if (n<0) {
			return false;
		}
		sent += n;
	}
	return true;
}
bool does_file_exist(const std::string &filepath) {
	FILE* f = fopen(filepath.c_str(), "r");
	if(f != NULL) {
		fclose(f);
		return true;
	} else {
		return false;
	}
}

std::vector<char> formatResponseList(Method_data mdata, const std::string &lst) {
    std::string f_string = mdata.httpHeader + " " + ok_http + "\r\n";
    f_string += "Access-Control-Allow-Origin: *\r\n";
    f_string += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    f_string += "Access-Control-Allow-Headers: Content-Type\r\n";

    std::string to_return;
    if (!lst.empty()) {
        std::string s_string = "Content-Length: " + std::to_string(lst.size()) + "\r\n\r\n";
        to_return = f_string + s_string + lst;
    } else {
        std::string s_string = "Content-Length: 0\r\n\r\n";
        to_return = f_string + s_string;
    }

    return std::vector<char>(to_return.begin(), to_return.end());
}

std::string formatResponseRecovery(int tablet_id, const std::string &lst) {
    std::string f_string =  "RECOVERY /"+ std::to_string(tablet_id)+ +"/"+ std::to_string(my_node_id)+ " HTTP/1.1\r\n";


    std::string to_return;

        std::string s_string = "Content-Length: "  +std::to_string(lst.size()) + "\r\n";
        std::string t_string = "Content-Type: type\r\n\r\n";
        to_return = f_string + s_string +t_string + lst;


    return to_return;
}

std::string formatResponse_checkpoint_ver(int tablet_id, const std::string &lst) {
    std::string f_string =  "CHECKPOINTVERSION /"+ std::to_string(tablet_id)+ +"/"+ std::to_string(my_node_id)+ " HTTP/1.1\r\n";


    std::string to_return;

        std::string s_string = "Content-Length: "  +std::to_string(lst.size()) + "\r\n";
        std::string t_string = "Content-Type: type\r\n\r\n";
        to_return = f_string + s_string +t_string + lst;


    return to_return;
}

std::string formatResponseCheckpoint(int tablet_id, int version_num) {
    std::string f_string =  "CHECKPOINT /"+ std::to_string(tablet_id)+ +"/"+ std::to_string(version_num)+ " HTTP/1.1\r\n";


    std::string to_return;

        std::string s_string = "Content-Length: "  +std::to_string(0) + "\r\n";
        std::string t_string = "Content-Type: type\r\n\r\n";
        to_return = f_string + s_string +t_string ;


    return to_return;
}


std::vector<char> formatResponse(Status status, Method_data mdata) {
	if (mdata.method == "GET") {
		std::string f_string;
		if (status.success) {
			 f_string = mdata.httpHeader+ " " +ok_http + "\r\n";
		} else {
			 f_string = mdata.httpHeader+ " " +bad_http + "\r\n";
		}
		int len = (int)status.binary_data.size();
		std::string d_type = status.data_type;
		std::string s_string = "Content-Length: "  +std::to_string(len) + "\r\n";
		std::string t_string = "Content-Type: "  +d_type + "\r\n" + "\r\n";
		std::string to_return = f_string+ s_string + t_string;
		std::vector<char> vectorToReturn;
		vectorToReturn.insert(vectorToReturn.end(), to_return.begin(), to_return.end());
		vectorToReturn.insert(vectorToReturn.end(), status.binary_data.begin(), status.binary_data.end());
//		fprintf(stderr,"vectorToReturn: %s\n",vectorToReturn.data());
		return vectorToReturn;
	} else {
		std::string f_string;
		if (status.success) {
			 f_string = mdata.httpHeader+ " " +ok_http + "\r\n";
		} else {
			 f_string = mdata.httpHeader+ " " +bad_http + "\r\n";
		}
		int len = (int)status.binary_data.size();
		std::string s_string = "Content-Length: "  +std::to_string(len) + "\r\n" +"\r\n";
		std::string to_return = f_string+ s_string;
		std::vector<char> vectorToReturn;
		vectorToReturn.insert(vectorToReturn.end(), to_return.begin(), to_return.end());
		vectorToReturn.insert(vectorToReturn.end(), status.binary_data.begin(), status.binary_data.end());
//		fprintf(stderr,"vectorToReturn: %s\n", vectorToReturn.data());
		return vectorToReturn;
	}
}

bool do_read(int fd, char *buf, int len) {
	int rcvd = 0;
	while (rcvd < len) {
		int n = read(fd, &buf[rcvd], len-rcvd);
		if (n<0) {
			return false;
		}
		rcvd += n;
	}
	return true;
}

//true means alive
// false means dead
bool ask_coordinator_is_node_alive(int node_id) {
//	fprintf(stderr, "asking coordinator\n");
	std::string add = allServers[node_id].forwarding_address + ":" + std::to_string(allServers[node_id].forwarding_port_num);
//	fprintf(stderr, "ip address sending for: %s\n", add.c_str());
	char buf[2000];
	std::string str_to_send = "ALIVE " + add+ "\r\n";
//	do_write_vector(sock_coordinator,std::vector<char>(str_to_send.begin(),str_to_send.end()) , (int)str_to_send.size());
	send(sock_coordinator,str_to_send.c_str(), (int)str_to_send.size(), 0);
	int n = read(sock_coordinator, buf, sizeof(buf));
		if (n <= 0) {
//			fprintf(stderr, "in this one?\n");
			return false;
		}
		std::string response(buf, n);
		if (response.find("YES") != std::string::npos) {
//			fprintf(stderr, "in this one 1?\n");
			return true;
		}
//		fprintf(stderr, "in this one2?\n");
		return false;
}
void recovered_from_crash() {
	//need to rerun the log files with checkpoint files as the starting point

}
bool send_to_server(int node_id, const std::string &msg) {
//	fprintf(stderr, "node id %d\n", node_id);
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(sockfd, (struct sockaddr*)&allServers[node_id].server_addr, sizeof(allServers[node_id].server_addr)) < 0) {
//		fprintf(stderr, "did we reach here? 1\n");
		if (!ask_coordinator_is_node_alive(node_id)) {
//			fprintf(stderr, "NODE DEAD\n");
			//we don't care about this node for now and can continue without this in our list of replicas anymore
//			removeNodeFromTabletReplicas(node_id);
			close(sockfd);
//			fprintf(stderr,"JUST CLOSED 2\n");
			return true;
		}
	}

	if (!do_write_vector(sockfd, std::vector<char>(msg.begin(), msg.end()), (int)msg.size())) {
//		fprintf(stderr, "did we reach here? 2\n");
		if (!ask_coordinator_is_node_alive(node_id)) {
//					fprintf(stderr, "NODE DEAD\n");
					//we don't care about this node for now and can continue without this in our list of replicas anymore
//					removeNodeFromTabletReplicas(node_id);
					close(sockfd);
//					fprintf(stderr,"JUST CLOSED 3\n");
					return true;
				}
	}
	fd_set r_set;
	while (true) {
		FD_ZERO(&r_set);
			FD_SET(sockfd, &r_set);
			int max_fd = sockfd+1;
			struct timeval tm = {1,0};
			int slct = select(max_fd, &r_set, NULL, NULL, &tm);
			if (slct == 0) {
				//timeout
				//need to ask the coordinator
//				fprintf(stderr, "did we reach here? 3\n");
				bool ask = ask_coordinator_is_node_alive(node_id);
				if (ask) {
//					fprintf(stderr, "NODE ALIVE\n");
					return true;
				} else {
//					fprintf(stderr, "NODE DEAD\n");
					//we don't care about this node for now and can continue without this in our list of replicas anymore
		//			removeNodeFromTabletReplicas(node_id);
					close(sockfd);
//					fprintf(stderr,"JUST CLOSED 4\n");
					return true;
				}
			} else if (FD_ISSET(sockfd, &r_set)){
				//got the info
				char buf[2000];
					int n = read(sockfd, buf, sizeof(buf));
					if (n <= 0) {
//						fprintf(stderr, "are in this?? \n");
						close(sockfd);
						return true;
					}
					std::string response(buf, n);

					if (response.find("ACK") != std::string::npos) {
						close(sockfd);
						return true;
					}
					return false;
			}
	}

//	char buf[2000];
//	int n = read(sockfd, buf, sizeof(buf));
//	if (n <= 0) {
//		fprintf(stderr, "are in this?? \n");
//		close(sockfd);
//		return true;
//	}
//	std::string response(buf, n);
//
//	if (response.find("ACK") != std::string::npos) {
//		return true;
//	}
//	close(sockfd);
//	fprintf(stderr,"JUST CLOSED 5\n");
//	return false;
}

bool replicate_to_backups(const std::string &method, const std::string &row, const std::string &col,
                          const std::string &content_type, const std::vector<char> &data) {

//	fprintf(stderr, "replication started\n");
	int tablet_id = getTabletForRowKey(row);
	std::vector<int> replicas = getReplicaNodesForTablet(tablet_id);
//	fprintf(stderr, "getReplicaNodesForTablet(tablet_id) %d\n", tablet_id);
	bool all_success = true;
	std::vector<int> successful_replicas;
	for (int node : replicas) {
		if (node == my_node_id) continue; // skip myself
//		fprintf(stderr, "Replica node is %d\n", node);
		std::string header = "REPLICATE" + std::to_string(row_to_sequence_num[row]) + " " + method + " /" + row + "/" + col + " HTTP/1.1\r\n";
		header += "Content-Length: " + std::to_string((int)data.size()) + "\r\n";
		header += "Content-Type: " + content_type + "\r\n\r\n";
		std::string msg = header;
		msg.insert(msg.end(), data.begin(), data.end());
		if (!send_to_server(node, msg)) {
			all_success = false;
//			fprintf(stderr, "replication failed for node: %d and VERSION NUMBER:", node);
//			fprintf(stderr, "%d\n", row_to_sequence_num[row]);
		} else {
			successful_replicas.push_back(node);
		}
	}
	//Rollback
	if (!all_success) {
		int seq_num = row_to_sequence_num[row];
		std::string rollback_msg = "ROLLBACK " + std::to_string(seq_num) + " " + row + " " + col + "\r\n";
		for (int node : successful_replicas) {
			send_to_server(node, rollback_msg);
		}
//		fprintf(stderr, "replication unsuccessful\n");
		return false;
	}

	//now if all are true the primary needs to inform the particular secondary it was a success
//	fprintf(stderr, "replication successful\n");
	return true;
}
void parseOwnCheckpointFile(int tablet_no, const std::string &filepath, int node, int node_version_no) {
//	fprintf(stderr, "parsing my Checkpoint file\n");
	std::ifstream reader(filepath, std::ios::binary);
	std::string s_line;
	Status status;
	status.success = false;
	//skip the first line
	bool ready_for_binary_data = false;
	bool found_entry = false;
	if (!std::getline(reader, s_line)) {
		return;
	}

	int my_version_no = std::stoi(s_line);
//	fprintf(stderr, "my version in string: %s\n", s_line.c_str());
//	fprintf(stderr, "my version: %d\n", my_version_no);
//	fprintf(stderr, "the node's version: %d\n", node_version_no);
	if (my_version_no <= node_version_no) {
		//don't need to send checkpoint entries

		return;
	}
	suspend_checkpointing = 0;
	//need to tell the node we are going to send CP entries
	std::string header = "CP_SEND /" + std::to_string(tablet_no) + "/" + std::to_string(node_version_no) + " HTTP/1.1\r\n";
						header += "Content-Length: " + std::to_string(0) + "\r\n\r\n";

						std::string msg = header;
//						msg.insert(msg.end(), getBinaryData.begin(), getBinaryData.end());
//						fprintf(stderr, "message we are sending from primary is: %s\n",msg.c_str());
						if (!send_to_server(node, msg)) {
//							fprintf(stderr, "recovery failed for node: %d and VERSION NUMBER:", node);

//							fprintf(stderr, "recovery unsuccessful\n");

						}
	int line_length_to_store=-1;
	std::string linedatatype_to_store;
	Method_data mdata;
	while (true) {
				std::streampos beginning_of_line = reader.tellg();
				if (!std::getline(reader, s_line)) {
					break;
				}

		std::istringstream traverseline(s_line);
		std::string linerowkey;
		std::string linecolkey;
		std::string linedatatype;
		int linedatalength;
		std::string entry_check;

		if (traverseline >> entry_check)  {
//			fprintf(stderr, "reached here\n");
			if (entry_check == "checkpoint_entry") {
				if (traverseline >> linerowkey >> linecolkey >> linedatatype >> linedatalength) {
					ready_for_binary_data = true;
									line_length_to_store = linedatalength;
									linedatatype_to_store = linedatatype;
									mdata.rowKey = linerowkey;
									mdata.colKey = linecolkey;
									mdata.content_type = linedatatype;
									mdata.content_length = linedatalength;
													//found our data!
//													fprintf(stderr, "found data\n");
													found_entry = true;
					//								std::vector<char> getBinaryData;
					//								fprintf(stderr, "reached here1\n");
					//								getBinaryData.resize(linedatalength);
					//								fprintf(stderr, "reached resize\n");
					//								reader.read(getBinaryData.data(), linedatalength);
					//								fprintf(stderr, "reached here2\n");
					//								status.binary_data = getBinaryData;
					//								status.data_type = linedatatype;
					//								status.success = true;

				}


			} else if (ready_for_binary_data){ //not a checkpoint entry and in binary data
				if (found_entry) {
//					fprintf(stderr, "the line found and true: %s\n", s_line.c_str());
				}
				reader.seekg(beginning_of_line);
	//				fprintf(stderr, "curr line : %s\n", s_line.c_str());
					std::vector<char> getBinaryData(line_length_to_store);
					getBinaryData.resize(line_length_to_store);
					reader.read(getBinaryData.data(), line_length_to_store);
//					fprintf(stderr, "binary data read : %s\n", getBinaryData.data());

				ready_for_binary_data = false;
				if (found_entry) {
					//send all the checkpoint entries back to the node
					status.binary_data = getBinaryData;
					status.data_type = linedatatype_to_store;
					status.success = true;
					found_entry = false;
					// now need to send this to the node
					std::string header = "CP_ENTRY" + std::to_string(tablet_no)+ " /" + mdata.rowKey + "/" + mdata.colKey + " HTTP/1.1\r\n";
					header += "Content-Length: " + std::to_string((int)getBinaryData.size()) + "\r\n";
					header += "Content-Type: " + mdata.content_type + "\r\n\r\n";
					std::string msg = header;
					msg.insert(msg.end(), getBinaryData.begin(), getBinaryData.end());
//					fprintf(stderr, "message we are sending from primary is: %s\n",msg.c_str());
					if (!send_to_server(node, msg)) {
//						fprintf(stderr, "recovery failed for node: %d and VERSION NUMBER:", node);

//						fprintf(stderr, "recovery unsuccessful\n");

					}

				}
			}

		} else if (ready_for_binary_data){ //not a checkpoint entry and in binary data

			if (found_entry) {
//								fprintf(stderr, "the line found and true: %s\n", s_line.c_str());
							}
			reader.seekg(beginning_of_line);
//				fprintf(stderr, "curr line : %s\n", s_line.c_str());
				std::vector<char> getBinaryData(line_length_to_store);
				getBinaryData.resize(line_length_to_store);
				reader.read(getBinaryData.data(), line_length_to_store);
//				fprintf(stderr, "binary data read : %s\n", getBinaryData.data());

			ready_for_binary_data = false;
			if (found_entry) {
				//send all the checkpoint entries back to the node
				status.binary_data = getBinaryData;
				status.data_type = linedatatype_to_store;
				status.success = true;
				found_entry = false;
				// now need to send this to the node
				std::string header = "CP_ENTRY" + std::to_string(tablet_no)+ " /" + mdata.rowKey + "/" + mdata.colKey + " HTTP/1.1\r\n";
				header += "Content-Length: " + std::to_string((int)getBinaryData.size()) + "\r\n";
				header += "Content-Type: " + mdata.content_type + "\r\n\r\n";
				std::string msg = header;
				msg.insert(msg.end(), getBinaryData.begin(), getBinaryData.end());
//				fprintf(stderr, "message we are sending from primary is: %s\n",msg.c_str());
				if (!send_to_server(node, msg)) {
//					fprintf(stderr, "recovery failed for node: %d and VERSION NUMBER:", node);

//					fprintf(stderr, "recovery unsuccessful\n");

				}

			}
		}
	}
	//now need to tell the node that we are done sending entries
	std::string header2 = "CPFILEDONE /" + std::to_string(tablet_no) + "/" + std::to_string(node_version_no) + " HTTP/1.1\r\n";
							header2 += "Content-Length: " + std::to_string(0) + "\r\n\r\n";

							std::string msg2 = header2;
	//						msg.insert(msg.end(), getBinaryData.begin(), getBinaryData.end());
//							fprintf(stderr, "message we are sending from primary is: %s\n",msg.c_str());
							if (!send_to_server(node, msg2)) {
//								fprintf(stderr, "recovery failed for node: %d and VERSION NUMBER:", node);

//								fprintf(stderr, "recovery unsuccessful\n");

							}




}
void parseOwnLogFile(std::string filepath, std::vector<int> list_sequence_num, int node) {
	std::ifstream reader(filepath, std::ios::binary);
			std::string s_line;
			Status status;
		Method_data mdata;
		std::vector<int> sequence_num_list;
		bool ready_for_binary_data = false;
		int curr_index = 0;
		int sending_mode = 0;

		while (true) {
			std::streampos beginning_of_line = reader.tellg();
			if (!std::getline(reader, s_line)) {
				break;
			}
			std::istringstream traverseline(s_line);


					int sequence_num;
					std::string method_name;
					std::string linerowkey;
					std::string linecolkey;
					std::string linedatatype;
					int linedatalength;

			if (traverseline >> sequence_num >> method_name >> linerowkey >> linecolkey) {
				if (method_name == "PUT") {
					if (sending_mode == 0 && sequence_num != list_sequence_num[curr_index]) {
//						fprintf(stderr, "NEED TO SEND THIS REQUEST %s\n", s_line.c_str());
						//means that this request onwards needs to be sent
						sending_mode = 1;
					} else {
						curr_index++;
					}
//					fprintf(stderr, "found PUT %s ", linerowkey.c_str());
//					fprintf(stderr, "%s \n", linecolkey.c_str());
					traverseline >> linedatalength >> linedatatype;

//					fprintf(stderr, "reached here1\n");

//					fprintf(stderr, "linedatalength %d\n", linedatalength);
//					fprintf(stderr, "linedatatype %s\n", linedatatype.c_str());
	//				reader.read(getBinaryData.data(), linedatalength);
	//				fprintf(stderr, "getBinaryData %s\n", getBinaryData.data());
//					sequence_num_list.push_back(sequence_num);
					ready_for_binary_data = true;
					mdata.method = method_name;
					mdata.content_length = linedatalength;
					mdata.content_type = linedatatype;
					mdata.rowKey = linerowkey;
					mdata.colKey = linecolkey;
					mdata.sequence_num = sequence_num;

				} else if (method_name == "CPUT") {
					traverseline >> linedatalength >> linedatatype;
					std::vector<char> getBinaryData;
//					fprintf(stderr, "reached here1\n");
					getBinaryData.resize(linedatalength);
//					fprintf(stderr, "reached resize\n");
					reader.read(getBinaryData.data(), linedatalength);
					std::string sep = SEPARATOR;
							auto it = std::search(getBinaryData.begin(), getBinaryData.end(), sep.begin(), sep.end());
							if (it != getBinaryData.end()) {
								std::vector<char> old_value(getBinaryData.begin(), it);
								std::vector<char> new_value(it+sep.size(), getBinaryData.end());


				}
//							sequence_num_list.push_back(sequence_num);
				} else if (method_name == "GET") {
//					fprintf(stderr, "found GET %s ", linerowkey.c_str());
//									fprintf(stderr, "%s \n", linecolkey.c_str());
//					sequence_num_list.push_back(sequence_num);


				} else if (method_name == "DELETE") {
//					sequence_num_list.push_back(sequence_num);
					if (sending_mode == 0 && sequence_num != list_sequence_num[curr_index]) {
											//means that this request needs to be sent
						sending_mode = 1;


										} else {
											curr_index++;
										}
					if (sending_mode == 1) {
						mdata.method = method_name;
						mdata.content_type = linedatatype;
						mdata.rowKey = linerowkey;
						mdata.colKey = linecolkey;
						std::vector<int> data;
						std::string header = "REPLICATE" + std::to_string(sequence_num) + " " + method_name + " /" + linerowkey + "/" + linecolkey + " HTTP/1.1\r\n";
								header += "Content-Length: " + std::to_string((int)data.size()) + "\r\n";

								std::string msg = header;
								msg.insert(msg.end(), data.begin(), data.end());
//								fprintf(stderr, "message we are sending from primary is: %s\n",msg.c_str());
								if (!send_to_server(node, msg)) {
//									fprintf(stderr, "recovery failed for node: %d and VERSION NUMBER:", node);

//									fprintf(stderr, "recovery unsuccessful\n");

								}
					}

				}

				}
			else if (ready_for_binary_data) {
				reader.seekg(beginning_of_line);
//				fprintf(stderr, "curr line : %s\n", s_line.c_str());
					std::vector<char> getBinaryData(linedatalength);
					getBinaryData.resize(linedatalength);
					reader.read(getBinaryData.data(), linedatalength);
//					fprintf(stderr, "getBinaryData %s\n", getBinaryData.data());
					mdata.binary_data = getBinaryData;
					ready_for_binary_data = false;
					std::vector<int> data;
					if (sending_mode == 1) {
						std::string header = "REPLICATE" + std::to_string(mdata.sequence_num) + " " + mdata.method + " /" + mdata.rowKey + "/" + mdata.colKey + " HTTP/1.1\r\n";
													header += "Content-Length: " + std::to_string((int)getBinaryData.size()) + "\r\n";
													header += "Content-Type: " + mdata.content_type + "\r\n\r\n";
													std::string msg = header;
													msg.insert(msg.end(), getBinaryData.begin(), getBinaryData.end());
//													fprintf(stderr, "messa?ge we are sending from primary is: %s\n",msg.c_str());
													if (!send_to_server(node, msg)) {
//														fprintf(stderr, "recovery failed for node: %d and VERSION NUMBER:", node);

//														fprintf(stderr, "recovery unsuccessful\n");

													}
					}

					//need to retrieve the binary data

				}
			}
}
void replayLogFile(std::string filepath) {
	std::ifstream reader(filepath, std::ios::binary);
				std::string s_line;
				Status status;
			Method_data mdata;
			std::vector<int> sequence_num_list;
			bool ready_for_binary_data = false;
			int curr_index = 0;
			int sending_mode = 0;

			while (true) {
				std::streampos beginning_of_line = reader.tellg();
				if (!std::getline(reader, s_line)) {
					break;
				}
				std::istringstream traverseline(s_line);


						int sequence_num;
						std::string method_name;
						std::string linerowkey;
						std::string linecolkey;
						std::string linedatatype;
						int linedatalength;

				if (traverseline >> sequence_num >> method_name >> linerowkey >> linecolkey) {
					if (method_name == "PUT") {

//						fprintf(stderr, "found PUT %s ", linerowkey.c_str());
//						fprintf(stderr, "%s \n", linecolkey.c_str());
						traverseline >> linedatalength >> linedatatype;

//						fprintf(stderr, "reached here1\n");

//						fprintf(stderr, "linedatatypedatalength %d\n", linedatalength);
//						fprintf(stderr, "linedatatype %s\n", linedatatype.c_str());
		//				reader.read(getBinaryData.data(), linedatalength);
		//				fprintf(stderr, "getBinaryData %s\n", getBinaryData.data());
	//					sequence_num_list.push_back(sequence_num);
						ready_for_binary_data = true;
						mdata.method = method_name;
						mdata.content_length = linedatalength;
						mdata.content_type = linedatatype;
						mdata.rowKey = linerowkey;
						mdata.colKey = linecolkey;
						mdata.sequence_num = sequence_num;

					} else if (method_name == "CPUT") {
						traverseline >> linedatalength >> linedatatype;
						std::vector<char> getBinaryData;
//						fprintf(stderr, "reached here1\n");
						getBinaryData.resize(linedatalength);
//						fprintf(stderr, "reached resize\n");
						reader.read(getBinaryData.data(), linedatalength);
						std::string sep = SEPARATOR;
								auto it = std::search(getBinaryData.begin(), getBinaryData.end(), sep.begin(), sep.end());
								if (it != getBinaryData.end()) {
									std::vector<char> old_value(getBinaryData.begin(), it);
									std::vector<char> new_value(it+sep.size(), getBinaryData.end());


					}
	//							sequence_num_list.push_back(sequence_num);
					} else if (method_name == "GET") {
	//					fprintf(stderr, "found GET %s ", linerowkey.c_str());
	//									fprintf(stderr, "%s \n", linecolkey.c_str());
	//					sequence_num_list.push_back(sequence_num);


					} else if (method_name == "DELETE") {
	//					sequence_num_list.push_back(sequence_num);


							mdata.method = method_name;
							mdata.content_type = linedatatype;
							mdata.rowKey = linerowkey;
							mdata.colKey = linecolkey;
							DELETE(kvs, mdata.rowKey, mdata.colKey);


					}

					}
				else if (ready_for_binary_data) {
					reader.seekg(beginning_of_line);
//					fprintf(stderr, "curr line : %s\n", s_line.c_str());
						std::vector<char> getBinaryData(linedatalength);
						getBinaryData.resize(linedatalength);
						reader.read(getBinaryData.data(), linedatalength);
//						fprintf(stderr, "getBinaryData %s\n", getBinaryData.data());
						mdata.binary_data = getBinaryData;
						ready_for_binary_data = false;
						std::vector<int> data;
						//need to run it
						PUT(kvs, mdata.rowKey, mdata.colKey, mdata.binary_data,mdata.content_type,  mdata.binary_data.size());
						//need to retrieve the binary data

					}
				}
}
std::string restart_after_crash(std::string filepath) {
	//it can tell the primary its last request and then it can retrieve the subsequent requests from the primary
	//find the last line in log file
	std::ifstream reader(filepath, std::ios::binary);
		std::string s_line;
		Status status;
	Method_data last_command;
	std::vector<int> sequence_num_list;;
	while (std::getline(reader, s_line)) {
//		fprintf(stderr, "THE LINE %s\n", s_line.c_str());
		std::istringstream traverseline(s_line);


				int sequence_num;
				std::string method_name;
				std::string linerowkey;
				std::string linecolkey;
				std::string linedatatype;
				int linedatalength;

		if (traverseline >> sequence_num >> method_name >> linerowkey >> linecolkey) {
			if (method_name == "PUT") {
//				fprintf(stderr, "found PUT %s ", linerowkey.c_str());
//				fprintf(stderr, "%s \n", linecolkey.c_str());
				traverseline >> linedatalength >> linedatatype;
				std::vector<char> getBinaryData(linedatalength);
//				fprintf(stderr, "reached here1\n");
				getBinaryData.resize(linedatalength);
//				fprintf(stderr, "linedatalength %d\n", linedatalength);
//				fprintf(stderr, "linedatatype %s\n", linedatatype.c_str());
//				reader.read(getBinaryData.data(), linedatalength);
//				fprintf(stderr, "getBinaryData %s\n", getBinaryData.data());
				sequence_num_list.push_back(sequence_num);
				row_to_sequence_num[linerowkey] = sequence_num;

			} else if (method_name == "CPUT") {
				traverseline >> linedatalength >> linedatatype;
				std::vector<char> getBinaryData;
//				fprintf(stderr, "reached here1\n");
				getBinaryData.resize(linedatalength);
//				fprintf(stderr, "reached resize\n");
				reader.read(getBinaryData.data(), linedatalength);
				std::string sep = SEPARATOR;
						auto it = std::search(getBinaryData.begin(), getBinaryData.end(), sep.begin(), sep.end());
						if (it != getBinaryData.end()) {
							std::vector<char> old_value(getBinaryData.begin(), it);
							std::vector<char> new_value(it+sep.size(), getBinaryData.end());


			}
						sequence_num_list.push_back(sequence_num);
			} else if (method_name == "GET") {
//				fprintf(stderr, "found GET %s ", linerowkey.c_str());
//								fprintf(stderr, "%s \n", linecolkey.c_str());
//				sequence_num_list.push_back(sequence_num);


			} else if (method_name == "DELETE") {
				sequence_num_list.push_back(sequence_num);
				row_to_sequence_num[linerowkey] = sequence_num;

			}

			}
		}
	if (sequence_num_list.size() == 0) {
		return "EMPTY";
	}
	//convert this list into a string of numbers
	std::string toRet;
	for (int i = 0; i < sequence_num_list.size(); i++) {
		toRet+=std::to_string(sequence_num_list[i]);
		if (i!=sequence_num_list.size()-1) {
			toRet+=" ";
		}
	}
	fprintf(stderr, "formatted the string\n");
	nlohmann::json resp;
		resp["sequence_numbers"] = sequence_num_list;
		std::string json_to_string = resp.dump();
		reader.close();
		return json_to_string;

}

void non_blocking_connection(int sockfd) {
	int opts = fcntl(sockfd, F_GETFL);
	if (opts < 0) {
		fprintf(stderr, "F_GETFL failed (%s)\n", strerror(errno));
		abort();
	}
	opts = (opts | O_NONBLOCK);
	if (fcntl(sockfd, F_SETFL, opts) < 0) {
		fprintf(stderr, "F_SETFL failed (%s)\n", strerror(errno));
		abort();
	}
}

void ctrlc_handler(int arg) {
//	fprintf(stderr, "in this func\n");
	shutting_down = true;
	for (int i = 0; i < 10000 ; i++) {
		if (connections_arr[i] != 0) {
			non_blocking_connection(connections_arr[i]);
//			fprintf(stderr, "in this loop\n");
			if (do_write(connections_arr[i], shutting_down_msg, (int)strlen(shutting_down_msg)) == false) {
				fprintf(stderr, "Error in writing server shut down message\n");
			}
			if (close(connections_arr[i]) < 0) {
				fprintf(stderr, "Error in closing connection\n");
				exit(EXIT_FAILURE);
			}
			fprintf(stderr, "[%d] Connection closed \r\n ", connections_arr[i]);
			connections_arr[i] = 0;
		}
	}
	if (sock_coordinator != 0) {
		non_blocking_connection(sock_coordinator);
//						fprintf(stderr, "in this loop\n");
						std::string str_to_send = "SHUTDOWN "+ binding_address_for_main + ":" + std::to_string(binding_port_num_for_main) + "\r\n";
								send(sock_coordinator,str_to_send.c_str(), (int)str_to_send.size(), 0);

						if (close(sock_coordinator) < 0) {
							fprintf(stderr, "Error in closing connection\n");
							exit(EXIT_FAILURE);
						}
	}

	if (close(listen_fd) < 0) {
		fprintf(stderr, "ran into issue with closing listener socket");
		exit(EXIT_FAILURE);
	}
	exit(0);
}

void traverse_through_checkpointfile(std::string filepath) {
	std::ifstream reader(filepath, std::ios::in);
		if (!reader.is_open()) {
//			fprintf(stderr, "Error opening file\n");
			exit(EXIT_FAILURE);
		}
}
/*
 * Log file format
 * seq_num PUT <Row: _, Col: _> Value: _\r\n\r\n
 * */
void write_to_log_file(int tablet_no,std::string filepath, int seq_num, Method_data mdata) {
	//first need to check if the log file exists
//	fprintf(stderr,"writing to log file function\n");
	std::ofstream writer(filepath, std::ios::binary | std::ios::app);
	if (!writer.is_open()) {
		fprintf(stderr, "couldn't open file");
	}
//	if (!does_file_exist(filepath)) {
//		fprintf(stderr,"writing to log file function\n");
//		//need to create it
//		log_writers[tablet_no].open(filepath, std::ios::out | std::ios::app);
//
//	}
//	if (!log_writers[tablet_no].is_open()) {
//		log_writers[tablet_no].open(filepath, std::ios::out | std::ios::app);
//	}
//	fprintf(stderr, "this is what we are writing\n");
//	fprintf(stderr, "sequence num %d\n", seq_num);
//		fprintf(stderr, "mdata.method %s\n", mdata.method.c_str());
//		fprintf(stderr, "mdata.rowKey %s\n", mdata.rowKey.c_str());
//		fprintf(stderr, "mdata.colKey %s\n", mdata.colKey.c_str());
//		fprintf(stderr, "mdata.binary_data %s\n", mdata.binary_data.data());
//		fprintf(stderr, "mdata.binary_data.size %ld\n", mdata.binary_data.size());
//		fprintf(stderr, "content.size %d\n", mdata.content_length);
	writer << seq_num << " " << mdata.method << " " << mdata.rowKey << " " << mdata.colKey;
	if (mdata.method == "PUT") {
		//has a value
//		fprintf(stderr,"WRITING PUT\n");
		writer << " " << mdata.content_length << " ";
		writer << mdata.content_type;
		writer << "\r\n";
		writer.write(mdata.binary_data.data(), mdata.content_length);
		writer << "\r\n";

	} else if (mdata.method == "CPUT"){
		writer << " " << mdata.content_length << " ";
		writer << mdata.content_type << " ";
		writer.write(mdata.combined_CPUT.data(), mdata.combined_CPUT.size());
		writer << "\r\n";

	} else if (mdata.method == "GET"){
		writer << "\r\n";

	} else if (mdata.method == "DELETE"){
		writer << "\r\n";
	}
//	fprintf(stderr, "flushing \n");
	writer.flush();

	writer.close();

}

Status get_from_checkpoint_file(int tablet_no, const std::string &filepath, const std::string &row, const std::string &col) {
//	fprintf(stderr, "get from checkpoint called\n");
	std::ifstream reader(filepath, std::ios::binary);
	std::string s_line;
	Status status;
	status.success = false;
	//skip the first line
	bool ready_for_binary_data = false;
	bool found_entry = false;
	std::getline(reader, s_line);
	int line_length_to_store=-1;
	std::string linedatatype_to_store;
	while (true) {
				std::streampos beginning_of_line = reader.tellg();
				if (!std::getline(reader, s_line)) {
					break;
				}

		std::istringstream traverseline(s_line);
		std::string linerowkey;
		std::string linecolkey;
		std::string linedatatype;
		int linedatalength;
		std::string entry_check;

		if (traverseline >> entry_check)  {
//			fprintf(stderr, "reached here\n");
			if (entry_check == "checkpoint_entry") {
				if (traverseline >> linerowkey >> linecolkey >> linedatatype >> linedatalength) {
					ready_for_binary_data = true;
									line_length_to_store = linedatalength;
									linedatatype_to_store = linedatatype;
									if (linerowkey == row && linecolkey == col) {
													//found our data!
//													fprintf(stderr, "found data\n");
													found_entry = true;
					//								std::vector<char> getBinaryData;
					//								fprintf(stderr, "reached here1\n");
					//								getBinaryData.resize(linedatalength);
					//								fprintf(stderr, "reached resize\n");
					//								reader.read(getBinaryData.data(), linedatalength);
					//								fprintf(stderr, "reached here2\n");
					//								status.binary_data = getBinaryData;
					//								status.data_type = linedatatype;
					//								status.success = true;
												}
				}


			} else if (ready_for_binary_data){ //not a checkpoint entry and in binary data
				if (found_entry) {
//					fprintf(stderr, "the line found and true: %s\n", s_line.c_str());
				}
				reader.seekg(beginning_of_line);
	//				fprintf(stderr, "curr line : %s\n", s_line.c_str());
					std::vector<char> getBinaryData(line_length_to_store);
					getBinaryData.resize(line_length_to_store);
					reader.read(getBinaryData.data(), line_length_to_store);
//					fprintf(stderr, "binary data read : %s\n", getBinaryData.data());

				ready_for_binary_data = false;
				if (found_entry) {
					status.binary_data = getBinaryData;
					status.data_type = linedatatype_to_store;
					status.success = true;
					found_entry = false;

				}
			}

		} else if (ready_for_binary_data){ //not a checkpoint entry and in binary data
//			fprintf(stderr, "reached here huh\n");
			if (found_entry) {
//								fprintf(stderr, "the line found and true: %s\n", s_line.c_str());
							}
			reader.seekg(beginning_of_line);
//				fprintf(stderr, "curr line : %s\n", s_line.c_str());
				std::vector<char> getBinaryData(line_length_to_store);
				getBinaryData.resize(line_length_to_store);
				reader.read(getBinaryData.data(), line_length_to_store);
//				fprintf(stderr, "binary data read : %s\n", getBinaryData.data());

			ready_for_binary_data = false;
			if (found_entry) {
				status.binary_data = getBinaryData;
				status.data_type = linedatatype;
				status.success = true;
				found_entry = false;

			}
		}
	}
	if (status.success) {
//		fprintf(stderr, "found ENTRY\n");
//		fprintf(stderr, "rowkey:%s ", row.c_str());
//		fprintf(stderr, "colkey:%s ", col.c_str());
//		fprintf(stderr, "binary data: %s\n", status.binary_data.data());
	} else {
//		fprintf(stderr, "didn't find ENTRY\n");
	}
	return status;
}
std::string getJsonForKVstore() {
	json kvsJson;
	for (const auto& row_packed : kvs.KVmap) {
				const std::string& rowkey = row_packed.first;
				const auto& columns = row_packed.second;
				json kvsRowJson;
				for (const auto& col_packed : columns.cols) {
					const std::string& colKey = col_packed.first;
					const auto& celldata = col_packed.second;
					kvsJson[rowkey][colKey]["value"] = celldata.binary_data;
					kvsJson[rowkey][colKey]["content_type"] = celldata.data_type;
				}
		 	}

	//now need to read and write from the disk as well

	std::vector<int> alltablets = getAllTabletsForNode(my_node_id);
					for (int i = 0; i < alltablets.size(); i++) {
						int tablet_id = alltablets[i];
						std::string filepath = "checkpoints/checkpoints_server"+std::to_string(my_node_id)+"tablet"+std::to_string(tablet_id);
						std::ifstream reader(filepath);
								std::string s_line;
								std::getline(reader,s_line);
								bool write_curr_entry = false;
								bool ready_for_binary_data=false;
								int line_length_to_store = -1;
								Method_data mdata;
								Status status;
								while (true) {
													std::streampos beginning_of_line = reader.tellg();
													if (!std::getline(reader, s_line)) {
														break;
													}

											std::istringstream traverseline(s_line);
											std::string linerowkey;
											std::string linecolkey;
											std::string linedatatype;
											int linedatalength;
											std::string entry_check;
											if (traverseline >> entry_check) {
												if (entry_check == "checkpoint_entry") {
													if (traverseline >> linerowkey >> linecolkey >> linedatatype >> linedatalength) {
																	line_length_to_store = linedatalength;
																	mdata.rowKey = linerowkey;
																	mdata.colKey = linecolkey;
																	mdata.content_length = linedatalength;
																	mdata.content_type = linedatatype;
//																	fprintf(stderr, "reached here\n");

																		ready_for_binary_data = true;
																		//now need to check if this row and key exists in our current kv
																		Status newstatus = GET(kvs, linerowkey, linecolkey);
																		if (newstatus.success) {
																			//don't have to add and can skip
																			//i.e already accounted for
																		} else {
																			write_curr_entry = true;
																		}




																}
												} else if (ready_for_binary_data){ //not a checkpoint entry and in binary data
//													fprintf(stderr, "reached here huh\n");
													reader.seekg(beginning_of_line);
										//				fprintf(stderr, "curr line : %s\n", s_line.c_str());
														std::vector<char> getBinaryData(line_length_to_store);
														getBinaryData.resize(line_length_to_store);
														reader.read(getBinaryData.data(), line_length_to_store);


													ready_for_binary_data = false;
													if (write_curr_entry) {
														//need to write this entry to the checkpoint file
														status.binary_data = getBinaryData;
														status.data_type = linedatatype;
														status.success = true;
//														fprintf(stderr, "writing this entry %s ", mdata.rowKey.c_str());
//														fprintf(stderr, "%s ", linecolkey.c_str());
//														fprintf(stderr, "binary data is: %s\n", getBinaryData.data());
														kvsJson[mdata.rowKey][mdata.colKey]["value"] = getBinaryData;
														kvsJson[mdata.rowKey][mdata.colKey]["content_type"] = mdata.content_type;
														write_curr_entry = false;

													}
												}

											} else if (ready_for_binary_data){ //not a checkpoint entry and in binary data
//												fprintf(stderr, "reached here huh\n");
												reader.seekg(beginning_of_line);
									//				fprintf(stderr, "curr line : %s\n", s_line.c_str());
													std::vector<char> getBinaryData(line_length_to_store);
													getBinaryData.resize(line_length_to_store);
													reader.read(getBinaryData.data(), line_length_to_store);


												ready_for_binary_data = false;
												if (write_curr_entry) {
													//need to write this entry to the checkpoint file
													status.binary_data = getBinaryData;
													status.data_type = linedatatype;
													status.success = true;
//													fprintf(stderr, "writing this entry %s ", mdata.rowKey.c_str());
//													fprintf(stderr, "%s ", linecolkey.c_str());
//													fprintf(stderr, "binary data is: %s\n", getBinaryData.data());
													kvsJson[mdata.rowKey][mdata.colKey]["value"] = getBinaryData;
													kvsJson[mdata.rowKey][mdata.colKey]["content_type"] = mdata.content_type;
													write_curr_entry = false;

												}
											}
										}
								reader.close();
					}

					return kvsJson.dump();
}

void write_versioned_checkpoint_file(int tablet_no, std::string filepath, int versionnum) {
//	fprintf(stderr, "write_to_checkpoint_file\n");
//	tablet_to_checkpoint_ver[tablet_no] = versionnum;
	//first need to check if the checkpoint file exists
//	if (!does_file_exist(filepath)) {
//		//need to create it
//		checkpoint_writers[tablet_no].open(filepath, std::ios::out | std::ios::app);
//
//	}
//	if (!checkpoint_writers[tablet_no].is_open()) {
//		checkpoint_writers[tablet_no].open(filepath, std::ios::out | std::ios::app);
//	}
//	fprintf(stderr, "WRITING TO CHECKPOINT FILE");
	std::string tmp_filepath = filepath + "tmp";
	std::ofstream writer(tmp_filepath, std::ios::out | std::ios::app);
	//need to update the version number

	writer<< versionnum << "\n";
	for (const Method_data mdata: map_of_checkpoint_entries[tablet_no].method_vec) {
		//need to write these to the checkpoint file
		writer << "checkpoint_entry " << mdata.rowKey << " " << mdata.colKey << " " << mdata.content_type << " " << mdata.content_length << "\r\n";
						writer.write(mdata.binary_data.data(), mdata.binary_data.size());
						writer << "\r\n";
	}

	//first need to write the data from the original checkpoint file
//	for (const auto& row_packed : kvs.KVmap) {
//			const std::string& rowkey = row_packed.first;
//			const auto& columns = row_packed.second;
//			for (const auto& col_packed : columns.cols) {
//				const std::string& colKey = col_packed.first;
//				const auto& celldata = col_packed.second;
//				writer << "checkpoint_entry " << rowkey << " " << colKey << " " << celldata.data_type << " " << celldata.size << "\r\n";
//				writer.write(celldata.binary_data.data(), celldata.size);
//				writer << "\r\n";
//				fprintf(stderr, "rowkey : %s\n", rowkey.c_str());
//				fprintf(stderr, "colkey : %s\n", colKey.c_str());
//				fprintf(stderr, "celldata.data_type : %s\n", celldata.data_type.c_str());
//				fprintf(stderr, "actual data : %s\n", celldata.binary_data.data());
//			}
//	 	}

//	while (std::getline(reader_old_file,l)) {
//
//		//before writing, should check if the entry exists in our current KV store
//		//if it does, then don't write it
//
//				std::istringstream traverseline(s_line);
//				std::string linerowkey;
//				std::string linecolkey;
//				std::string linedatatype;
//				int linedatalength;
//				std::string entry_check;
//				fprintf(stderr, "reached before\n");
//				if (traverseline >> entry_check >> linerowkey >> linecolkey >> linedatatype >> linedatalength) {
//					fprintf(stderr, "reached here\n");
//					if (entry_check == "checkpoint_entry") {}}
//
//		writer << l << "\n";
//	}

	//now write the new information

	writer.flush();
	rename(tmp_filepath.c_str(), filepath.c_str());
	    	//need to add a 3 ms of delay
	    	usleep(3000);
}
void write_to_checkpoint_file(int tablet_no, std::string filepath, int versionnum) {
//	fprintf(stderr, "write_to_checkpoint_file\n");
	//first need to check if the checkpoint file exists
//	if (!does_file_exist(filepath)) {
//		//need to create it
//		checkpoint_writers[tablet_no].open(filepath, std::ios::out | std::ios::app);
//
//	}
//	if (!checkpoint_writers[tablet_no].is_open()) {
//		checkpoint_writers[tablet_no].open(filepath, std::ios::out | std::ios::app);
//	}
//	fprintf(stderr, "WRITING TO CHECKPOINT FILE");
	std::string tmp_filepath = filepath + "tmp";
	std::ofstream writer(tmp_filepath, std::ios::out | std::ios::app);
	//need to update the version number

	writer<< versionnum << "\n";
	//first need to write the data from the original checkpoint file
	for (const auto& row_packed : kvs.KVmap) {
			const std::string& rowkey = row_packed.first;
			const auto& columns = row_packed.second;
			for (const auto& col_packed : columns.cols) {
				const std::string& colKey = col_packed.first;
				const auto& celldata = col_packed.second;
				writer << "checkpoint_entry " << rowkey << " " << colKey << " " << celldata.data_type << " " << celldata.size << "\r\n";
				writer.write(celldata.binary_data.data(), celldata.size);
				writer << "\r\n";
//				fprintf(stderr, "rowkey : %s\n", rowkey.c_str());
//				fprintf(stderr, "colkey : %s\n", colKey.c_str());
//				fprintf(stderr, "celldata.data_type : %s\n", celldata.data_type.c_str());
//				fprintf(stderr, "actual data : %s\n", celldata.binary_data.data());
			}
	 	}
	std::ifstream reader(filepath);
	std::string s_line;
	std::getline(reader,s_line);
//	while (std::getline(reader_old_file,l)) {
//
//		//before writing, should check if the entry exists in our current KV store
//		//if it does, then don't write it
//
//				std::istringstream traverseline(s_line);
//				std::string linerowkey;
//				std::string linecolkey;
//				std::string linedatatype;
//				int linedatalength;
//				std::string entry_check;
//				fprintf(stderr, "reached before\n");
//				if (traverseline >> entry_check >> linerowkey >> linecolkey >> linedatatype >> linedatalength) {
//					fprintf(stderr, "reached here\n");
//					if (entry_check == "checkpoint_entry") {}}
//
//		writer << l << "\n";
//	}
	bool write_curr_entry = false;
	bool ready_for_binary_data=false;
	int line_length_to_store = -1;
	Method_data mdata;
	Status status;
	while (true) {
					std::streampos beginning_of_line = reader.tellg();
					if (!std::getline(reader, s_line)) {
						break;
					}

			std::istringstream traverseline(s_line);
			std::string linerowkey;
			std::string linecolkey;
			std::string linedatatype;
			int linedatalength;
			std::string entry_check;

			if (traverseline >> entry_check) {
				if (entry_check == "checkpoint_entry") {
					if (traverseline >> linerowkey >> linecolkey >> linedatatype >> linedatalength) {
									line_length_to_store = linedatalength;
									mdata.rowKey = linerowkey;
									mdata.colKey = linecolkey;
									mdata.content_length = linedatalength;
									mdata.content_type = linedatatype;
//									fprintf(stderr, "reached here\n");

										ready_for_binary_data = true;
										//now need to check if this row and key exists in our current kv
										Status newstatus = GET(kvs, linerowkey, linecolkey);
										if (newstatus.success) {
											//don't have to add and can skip
											//i.e already accounted for
										} else {
											//status is false need to write
//											fprintf(stderr, "COULDN'T FIND for row : %s ", linerowkey.c_str());
//											fprintf(stderr, "col : %s\n", linecolkey.c_str());
											write_curr_entry = true;
										}




								}
				} else if (ready_for_binary_data){ //not a checkpoint entry and in binary data
//					fprintf(stderr, "reached here huh\n");
					reader.seekg(beginning_of_line);
		//				fprintf(stderr, "curr line : %s\n", s_line.c_str());
						std::vector<char> getBinaryData(line_length_to_store);
						getBinaryData.resize(line_length_to_store);
						reader.read(getBinaryData.data(), line_length_to_store);


					ready_for_binary_data = false;
					if (write_curr_entry) {
						//need to write this entry to the checkpoint file
						status.binary_data = getBinaryData;
						status.data_type = linedatatype;
						status.success = true;
//						fprintf(stderr, "writing this entry %s ", mdata.rowKey.c_str());
//						fprintf(stderr, "%s ", linecolkey.c_str());
//						fprintf(stderr, "binary data is: %s\n", getBinaryData.data());
						writer << "checkpoint_entry " << mdata.rowKey << " " << mdata.colKey << " " << mdata.content_type << " " << mdata.content_length << "\r\n";
										writer.write(getBinaryData.data(), getBinaryData.size());
										writer << "\r\n";
						write_curr_entry = false;

					}
				}

			} else if (ready_for_binary_data){ //not a checkpoint entry and in binary data
//				fprintf(stderr, "reached here huh\n");
				reader.seekg(beginning_of_line);
	//				fprintf(stderr, "curr line : %s\n", s_line.c_str());
					std::vector<char> getBinaryData(line_length_to_store);
					getBinaryData.resize(line_length_to_store);
					reader.read(getBinaryData.data(), line_length_to_store);


				ready_for_binary_data = false;
				if (write_curr_entry) {
					//need to write this entry to the checkpoint file
					status.binary_data = getBinaryData;
					status.data_type = linedatatype;
					status.success = true;
//					fprintf(stderr, "writing this entry %s ", mdata.rowKey.c_str());
//					fprintf(stderr, "%s ", linecolkey.c_str());
//					fprintf(stderr, "binary data is: %s\n", getBinaryData.data());
					writer << "checkpoint_entry " << mdata.rowKey << " " << mdata.colKey << " " << mdata.content_type << " " << mdata.content_length << "\r\n";
									writer.write(getBinaryData.data(), getBinaryData.size());
									writer << "\r\n";
					write_curr_entry = false;

				}
			}
		}
	reader.close();
	//now write the new information

	writer.flush();
	rename(tmp_filepath.c_str(), filepath.c_str());
	    	//need to add a 3 ms of delay
	    	usleep(3000);
}

void write_to_checkpoint_file_recovery(int tablet_no, std::string filepath, int versionnum) {
//	fprintf(stderr, "write_to_checkpoint_file\n");
	//first need to check if the checkpoint file exists
//	if (!does_file_exist(filepath)) {
//		//need to create it
//		checkpoint_writers[tablet_no].open(filepath, std::ios::out | std::ios::app);
//
//	}
//	if (!checkpoint_writers[tablet_no].is_open()) {
//		checkpoint_writers[tablet_no].open(filepath, std::ios::out | std::ios::app);
//	}
//	fprintf(stderr, "WRITING TO CHECKPOINT FILE");
	std::string tmp_filepath = filepath + "tmp";
	std::ofstream writer(tmp_filepath, std::ios::out | std::ios::app);
	//need to update the version number

	writer<< versionnum << "\n";
	//first need to write the data from the original checkpoint file
	for (const auto& row_packed : kvs.KVmap) {
			const std::string& rowkey = row_packed.first;
			const auto& columns = row_packed.second;
			for (const auto& col_packed : columns.cols) {
				const std::string& colKey = col_packed.first;
				const auto& celldata = col_packed.second;
				writer << "checkpoint_entry " << rowkey << " " << colKey << " " << celldata.data_type << " " << celldata.size << "\r\n";
				writer.write(celldata.binary_data.data(), celldata.size);
				writer << "\r\n";
//				fprintf(stderr, "rowkey : %s\n", rowkey.c_str());
//				fprintf(stderr, "colkey : %s\n", colKey.c_str());
//				fprintf(stderr, "celldata.data_type : %s\n", celldata.data_type.c_str());
//				fprintf(stderr, "actual data : %s\n", celldata.binary_data.data());
			}
	 	}
	std::ifstream reader(filepath);
	std::string s_line;
	std::getline(reader,s_line);
//	while (std::getline(reader_old_file,l)) {
//
//		//before writing, should check if the entry exists in our current KV store
//		//if it does, then don't write it
//
//				std::istringstream traverseline(s_line);
//				std::string linerowkey;
//				std::string linecolkey;
//				std::string linedatatype;
//				int linedatalength;
//				std::string entry_check;
//				fprintf(stderr, "reached before\n");
//				if (traverseline >> entry_check >> linerowkey >> linecolkey >> linedatatype >> linedatalength) {
//					fprintf(stderr, "reached here\n");
//					if (entry_check == "checkpoint_entry") {}}
//
//		writer << l << "\n";
//	}
	bool write_curr_entry = false;
	bool ready_for_binary_data=false;
	int line_length_to_store = -1;
	Method_data mdata;
	Status status;
	while (true) {
					std::streampos beginning_of_line = reader.tellg();
					if (!std::getline(reader, s_line)) {
						break;
					}

			std::istringstream traverseline(s_line);
			std::string linerowkey;
			std::string linecolkey;
			std::string linedatatype;
			int linedatalength;
			std::string entry_check;

			if (traverseline >> entry_check) {
				if (entry_check == "checkpoint_entry") {
					if (traverseline >> linerowkey >> linecolkey >> linedatatype >> linedatalength) {
									line_length_to_store = linedatalength;
									mdata.rowKey = linerowkey;
									mdata.colKey = linecolkey;
									mdata.content_length = linedatalength;
									mdata.content_type = linedatatype;
//									fprintf(stderr, "reached here\n");

										ready_for_binary_data = true;
										//now need to check if this row and key exists in our current kv
										Status newstatus = GET(kvs, linerowkey, linecolkey);
										if (newstatus.success) {
											//don't have to add and can skip
											//i.e already accounted for
										} else {
											//status is false need to write
//											fprintf(stderr, "COULDN'T FIND for row : %s ", linerowkey.c_str());
//											fprintf(stderr, "col : %s\n", linecolkey.c_str());
											write_curr_entry = true;
										}




								}
				} else if (ready_for_binary_data){ //not a checkpoint entry and in binary data
//					fprintf(stderr, "reached here huh\n");
					reader.seekg(beginning_of_line);
		//				fprintf(stderr, "curr line : %s\n", s_line.c_str());
						std::vector<char> getBinaryData(line_length_to_store);
						getBinaryData.resize(line_length_to_store);
						reader.read(getBinaryData.data(), line_length_to_store);


					ready_for_binary_data = false;
					if (write_curr_entry) {
						//need to write this entry to the checkpoint file
						status.binary_data = getBinaryData;
						status.data_type = linedatatype;
						status.success = true;
//						fprintf(stderr, "writing this entry %s ", mdata.rowKey.c_str());
//						fprintf(stderr, "%s ", linecolkey.c_str());
//						fprintf(stderr, "binary data is: %s\n", getBinaryData.data());
						writer << "checkpoint_entry " << mdata.rowKey << " " << mdata.colKey << " " << mdata.content_type << " " << mdata.content_length << "\r\n";
										writer.write(getBinaryData.data(), getBinaryData.size());
										writer << "\r\n";
						write_curr_entry = false;

					}
				}

			} else if (ready_for_binary_data){ //not a checkpoint entry and in binary data
//				fprintf(stderr, "reached here huh\n");
				reader.seekg(beginning_of_line);
	//				fprintf(stderr, "curr line : %s\n", s_line.c_str());
					std::vector<char> getBinaryData(line_length_to_store);
					getBinaryData.resize(line_length_to_store);
					reader.read(getBinaryData.data(), line_length_to_store);


				ready_for_binary_data = false;
				if (write_curr_entry) {
					//need to write this entry to the checkpoint file
					status.binary_data = getBinaryData;
					status.data_type = linedatatype;
					status.success = true;
//					fprintf(stderr, "writing this entry %s ", mdata.rowKey.c_str());
//					fprintf(stderr, "%s ", linecolkey.c_str());
//					fprintf(stderr, "binary data is: %s\n", getBinaryData.data());
					writer << "checkpoint_entry " << mdata.rowKey << " " << mdata.colKey << " " << mdata.content_type << " " << mdata.content_length << "\r\n";
									writer.write(getBinaryData.data(), getBinaryData.size());
									writer << "\r\n";
					write_curr_entry = false;

				}
			}
		}
	reader.close();
	//now write the new information

	writer.flush();
	rename(tmp_filepath.c_str(), filepath.c_str());
	    	//need to add a 3 ms of delay
	    	usleep(3000);
}

bool check_checkpoint_requirements(int tablet_id) {
	//based on some condition figure its time to checkpoint
	int version_num = 0;
	std::string checkpointfile_path = "checkpoints/checkpoints_server"+std::to_string(my_node_id)+"tablet"+std::to_string(tablet_id);
	int get_ver_no;
	std::ifstream reader(checkpointfile_path);
								reader >> get_ver_no;
								if (!reader.fail()) {
									//no version number
									version_num = get_ver_no;
								}
								version_num++;
								reader.close();
//	if (tablet_to_checkpoint_ver.find(tablet_id) == tablet_to_checkpoint_ver.end()) {
//		tablet_to_checkpoint_ver[tablet_id] = 1;
//	} else {
//		tablet_to_checkpoint_ver[tablet_id]++;
//	}
	std::string str_to_send = formatResponseCheckpoint(tablet_id, version_num);

	std::vector<int> replicas = getReplicaNodesForTablet(tablet_id);
		for (int node : replicas) {
			if (node == my_node_id) continue; // skip myself

			if (!send_to_server(node, str_to_send)) {
//				return false;
			}
		}
//		std::string checkpointfile_path = "checkpoints/checkpoints_server"+std::to_string(my_node_id)+"tablet"+std::to_string(tablet_id);
		write_to_checkpoint_file(tablet_id, checkpointfile_path, version_num);
		std::string logfile_path = logs+"/logs_server"+std::to_string(my_node_id)+"tablet"+std::to_string(tablet_id);
		std::ofstream refresh_logger(logfile_path);
//		fprintf(stderr, "CHECKPOINTED my own file\n");
		return true;
}
void replay_log_file(int tablet_id, const std::string &filepath) {
	//need to check if this is the most up to date, otherwise ask the primary for its version
	std::ifstream reader(filepath, std::ios::binary);
	std::string checkpointfile_path = "checkpoints/checkpoints_server"+std::to_string(my_node_id)+"tablet"+std::to_string(tablet_id);
	std::string s_line;
	Status status;
	//skip the first line
	std::getline(reader, s_line);
	while (std::getline(reader, s_line)) {
//		fprintf(stderr, "in the next line\n");
		std::istringstream traverseline(s_line);
		int sequence_num;
		std::string method_name;
		std::string linerowkey;
		std::string linecolkey;
		std::string linedatatype;
		int linedatalength;

		if (traverseline >> sequence_num >> method_name >> linerowkey >> linecolkey) {
			if (method_name == "PUT") {
				traverseline >> linedatalength >> linedatatype;
				std::vector<char> getBinaryData;
//				fprintf(stderr, "reached here1\n");
				getBinaryData.resize(linedatalength);
//				fprintf(stderr, "reached resize\n");
				reader.read(getBinaryData.data(), linedatalength);
				//Now need to call the function
				status = GET(kvs, linerowkey, linecolkey);
																if (!status.success) {
																	//need to check in the actual checkpoint file then
//																	fprintf(stderr,"trying to retrieve from checkpoint file\n");
																	status = get_from_checkpoint_file(tablet_id, checkpointfile_path, linerowkey, linecolkey);
																	if (status.success) {
//																		fprintf(stderr,"retrieve from checkpoint file SUCCEEDED\n");
																		//need to add it to our map
																		PUT(kvs,  linerowkey, linecolkey, status.binary_data, status.data_type, status.binary_data.size());}

			}
																PUT(kvs, linerowkey, linecolkey, getBinaryData,linedatatype, linedatalength);
			} else if (method_name == "CPUT") {
				traverseline >> linedatalength >> linedatatype;
				std::vector<char> getBinaryData;
//				fprintf(stderr, "reached here1\n");
				getBinaryData.resize(linedatalength);
//				fprintf(stderr, "reached resize\n");
				reader.read(getBinaryData.data(), linedatalength);
				std::string sep = SEPARATOR;
						auto it = std::search(getBinaryData.begin(), getBinaryData.end(), sep.begin(), sep.end());
						if (it != getBinaryData.end()) {
							std::vector<char> old_value(getBinaryData.begin(), it);
							std::vector<char> new_value(it+sep.size(), getBinaryData.end());
							status = GET(kvs, linerowkey, linecolkey);
												if (!status.success) {
													//need to check in the actual checkpoint file then
//													fprintf(stderr,"trying to retrieve from checkpoint file\n");
													status = get_from_checkpoint_file(tablet_id, checkpointfile_path, linerowkey, linecolkey);
													if (status.success) {
//														fprintf(stderr,"retrieve from checkpoint file SUCCEEDED\n");
														//need to add it to our map


													PUT(kvs,  linerowkey, linecolkey, status.binary_data, status.data_type, status.binary_data.size());}}
																				status = CPUT(kvs, linerowkey, linecolkey, old_value, new_value, linedatatype, linedatalength);
			}} else if (method_name == "GET") {
				status = GET(kvs, linerowkey, linecolkey);
																if (!status.success) {
																	//need to check in the actual checkpoint file then
//																	fprintf(stderr,"trying to retrieve from checkpoint file\n");
																	status = get_from_checkpoint_file(tablet_id, checkpointfile_path, linerowkey, linecolkey);
																	if (status.success) {
//																		fprintf(stderr,"retrieve from checkpoint file SUCCEEDED\n");
																		//need to add it to our map
																		PUT(kvs,  linerowkey, linecolkey, status.binary_data, status.data_type, status.binary_data.size());}
			}

			} else if (method_name == "DELETE") {
				status = GET(kvs, linerowkey, linecolkey);
																if (!status.success) {

																	//need to check in the actual checkpoint file then
//																	fprintf(stderr,"trying to retrieve from checkpoint file\n");
																	status = get_from_checkpoint_file(tablet_id, checkpointfile_path, linerowkey, linecolkey);
																	if (status.success) {
//																		fprintf(stderr,"retrieve from checkpoint file SUCCEEDED\n");
																		//need to add it to our map
																		PUT(kvs,  linerowkey, linecolkey, status.binary_data, status.data_type, status.binary_data.size());}
			}
			status = DELETE(kvs, linerowkey, linecolkey);

			}

			} else {
//				fprintf(stderr, "reached here huh\n");
				reader.seekg(linedatalength, std::ios::cur);
			}
		}
	}




void initialize_mutex_map(int size) {
	for (int i = 0; i < size; i++) {
		pthread_mutex_init(&mutex_for_checkpoint[i], NULL);
		}

}
void periodic_checkpointing() {
	sleep(3000);
	ready_to_checkpoint = true;

	while (!shutting_down) {
			if (!allow_run || !ready_to_checkpoint ) {
//			            fprintf(stderr, "Heartbeat paused. Waiting for RESTART command.\n");
			            sleep(1); // Polling interval while paused
			            continue;
			        }
	//		std::string str_to_send = "heartbeat string\r\n";
	//		fprintf(stderr, "sending heartbeat\n");
			std::vector<int> alltablets = getAllTabletsForNode(my_node_id);
				for (int i = 0; i < alltablets.size(); i++) {

					int primary_node = getPrimaryNodeForTablet(alltablets[i]);
					if (primary_node == my_node_id) {
						pthread_mutex_lock(&mutex_for_checkpoint[i]);
//						fprintf(stderr, "locked checkpoint periodic\n");
						//I need to conduct the checkpoint
//						fprintf(stderr, "I AM CONDUCTING CHECKPOINTING\n");
						check_checkpoint_requirements(alltablets[i]);
//						fprintf(stderr, "unlocked checkpoint periodic\n");
						pthread_mutex_unlock(&mutex_for_checkpoint[i]);

					} else {
//						fprintf(stderr, "I AM NOT A PRIMARY\n");
					}
				}

			sleep(CHECKPOINT_INTERVAL);
	//		if (my_node_id  == 1) {
	//			if (i == 2) {
	//				allow_run = false;
	//				break;
	//			}
	//		}

		}


}
void periodic_heartbeats() {
	int i = 0;
	while (!shutting_down) {
		if (!allow_run) {
//		            fprintf(stderr, "Heartbeat paused. Waiting for RESTART command.\n");
		            sleep(1); // Polling interval while paused
		            continue;
		        }
//		std::string str_to_send = "heartbeat string\r\n";
//		fprintf(stderr, "sending heartbeat\n");
		std::string str_to_send = binding_address_for_main + ":" + std::to_string(binding_port_num_for_main) + "\r\n";
		send(sock_coordinator,str_to_send.c_str(), (int)str_to_send.size(), 0);
//		do_write_vector(sock_coordinator, std::vector<char>(str_to_send.begin(), str_to_send.end()), (int)str_to_send.size());


		sleep(HB_INTERVAL);
//		if (my_node_id  == 1) {
//			if (i == 2) {
//				allow_run = false;
//				break;
//			}
//		}
		i++;
	}
}

bool is_valid_email(const std::string& email_address) {
	std::regex to_match("^<[a-zA-Z0-9.]+@[a-zA-Z0-9.]+>$");
	return std::regex_match(email_address, to_match);
}

void populateAllServers(const std::string& filepath) {
	std::ifstream reader(filepath, std::ios::in);
	if (!reader.is_open()) {
//		fprintf(stderr, "Error opening file\n");
		exit(EXIT_FAILURE);
	}
	int curr_index = 1;
	std::string s_line;
	std::vector<std::string> all_lines;
	while (std::getline(reader, s_line)) {
		all_lines.push_back(s_line);
	}
	reader.close();

	for (int i = 0; i < (int)all_lines.size(); i++) {
		std::string line = all_lines[i];
		size_t colon_index = line.find(':');
		std::string IP_address = line.substr(0, colon_index);
		int port_num = std::stoi(line.substr(colon_index+1));
		struct sockaddr_in server_dest;
		bzero(&server_dest, sizeof(server_dest));
		server_dest.sin_family = AF_INET;
		server_dest.sin_port = htons(port_num);
		inet_pton(AF_INET, IP_address.c_str(), &(server_dest.sin_addr));
		ServerData newServer;
		newServer.forwarding_address = IP_address;
		newServer.forwarding_port_num = port_num;
		newServer.server_addr = server_dest;
		allServers.push_back(newServer);
	}

	my_node_id = server_num - 1;

	binding_address_for_main = allServers[my_node_id].forwarding_address;
	binding_port_num_for_main = allServers[my_node_id].forwarding_port_num;
}

int connect_to_primary_and_forward(const std::string &original_request, int primary_node, std::vector<char> &resp) {
	// Connect to primary node and forward the request
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in server_dest;
				bzero(&server_dest, sizeof(server_dest));
				server_dest.sin_family = AF_INET;
				server_dest.sin_port = htons(allServers[primary_node].forwarding_port_num);
				inet_pton(AF_INET, allServers[primary_node].forwarding_address.c_str(), &(server_dest.sin_addr));
	if (connect(sockfd, (struct sockaddr*)&server_dest, sizeof(server_dest)) < 0) {
		close(sockfd);
//		fprintf(stderr,"JUST CLOSED 6\n");
		return -1;
	}

	if (!do_write_vector(sockfd, std::vector<char>(original_request.begin(), original_request.end()), (int)original_request.size())) {
		close(sockfd);
//		fprintf(stderr,"JUST CLOSED 7\n");
		return -1;
	}

	// read response from primary
	std::vector<char> resp_buf(2000);
	int r = read(sockfd, resp_buf.data(), (int)resp_buf.size());
	if (r <= 0) {
		close(sockfd);
//		fprintf(stderr,"JUST CLOSED 8\n");
		return -1;
	}
	resp_buf.resize(r);
	close(sockfd);
//	fprintf(stderr,"JUST CLOSED 9\n");


	std::vector<char> response(resp_buf.begin(), resp_buf.end());
	resp = response;
//	fprintf(stderr, "THIS IS WHAT IS RETURNED BY PRIMARY initially\n");
//		fprintf(stderr, "%s\n", response.data());
	static std::vector<char> primary_response;
	primary_response = response;
	return r;
}

void connect_to_primary_and_forward_recovery(const std::string &original_request, int primary_node, std::vector<char> &resp) {
	// Connect to primary node and forward the request

	struct sockaddr_in server_dest;
			bzero(&server_dest, sizeof(server_dest));
			server_dest.sin_family = AF_INET;
			server_dest.sin_port = htons(allServers[primary_node].forwarding_port_num);
			inet_pton(AF_INET, allServers[primary_node].forwarding_address.c_str(), &(server_dest.sin_addr));
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(sockfd, (struct sockaddr*)&server_dest, sizeof(server_dest)) < 0) {
		close(sockfd);
//		fprintf(stderr,"JUST CLOSED 10\n");
		return;
//		return -1;
	}

	if (!do_write_vector(sockfd, std::vector<char>(original_request.begin(), original_request.end()), (int)original_request.size())) {
		close(sockfd);
//		fprintf(stderr,"JUST CLOSED 11\n");
		return;
//		return -1;
	}
//	fprintf(stderr, "SUCCESSFULLY FORWARDED %s to primary\n", original_request.c_str());

//	// read response from primary
//	std::vector<char> resp_buf(2000);
//	int r = read(sockfd, resp_buf.data(), (int)resp_buf.size());
//	if (r <= 0) {
//		close(sockfd);
//		return -1;
//	}
//	resp_buf.resize(r);
//	close(sockfd);
//
//
//	std::vector<char> response(resp_buf.begin(), resp_buf.end());
//	resp = response;
////	fprintf(stderr, "THIS IS WHAT IS RETURNED BY PRIMARY initially\n");
////		fprintf(stderr, "%s\n", response.data());
//	static std::vector<char> primary_response;
//	primary_response = response;
//	return r;
}

bool forward_request_to_primary_checkpoint_version(int comm_fd, const std::string &original_request, int primary_node) {
	std::vector<char> resp;
	int r = connect_to_primary_and_forward(original_request, primary_node, resp);
	if (r < 0) {
		// failed to forward
//		fprintf(stderr, "failed\n");
		std::string err_response = "HTTP/1.1 500 Internal Server Error\r\nContent-Length:0\r\n\r\n";
		do_write_vector(comm_fd, std::vector<char>(err_response.begin(), err_response.end()), (int)err_response.size());
		return false;
	}
	// Retrieve static primary_response
	extern std::vector<char> primary_response;
//	fprintf(stderr, "THIS IS WHAT IS RETURNED BY PRIMARY\n");
//	fprintf(stderr, "%s\n", resp.data());
	do_write_vector(comm_fd, resp, (int)resp.size());
	return true;
}

bool forward_request_to_primary(int comm_fd, const std::string &original_request, int primary_node) {
	std::vector<char> resp;
	int r = connect_to_primary_and_forward(original_request, primary_node, resp);
	if (r < 0) {
		// failed to forward
//		fprintf(stderr, "failed\n");
		std::string err_response = "HTTP/1.1 500 Internal Server Error\r\nContent-Length:0\r\n\r\n";
		do_write_vector(comm_fd, std::vector<char>(err_response.begin(), err_response.end()), (int)err_response.size());
		return false;
	}
	// Retrieve static primary_response
	extern std::vector<char> primary_response;
//	fprintf(stderr, "THIS IS WHAT IS RETURNED BY PRIMARY\n");
//	fprintf(stderr, "%s\n", resp.data());
	do_write_vector(comm_fd, resp, (int)resp.size());
	return true;
}

bool forward_request_to_primary_for_recovery(int comm_fd, const std::string &original_request, int primary_node) {
	std::vector<char> resp;
	connect_to_primary_and_forward_recovery(original_request, primary_node, resp);
//	if (r < 0) {
//		// failed to forward
//		fprintf(stderr, "failed\n");
//		std::string err_response = "HTTP/1.1 500 Internal Server Error\r\nContent-Length:0\r\n\r\n";
//		do_write_vector(comm_fd, std::vector<char>(err_response.begin(), err_response.end()), (int)err_response.size());
//		return false;
//	}
//	// Retrieve static primary_response
//	extern std::vector<char> primary_response;
//	fprintf(stderr, "THIS IS WHAT IS RETURNED BY PRIMARY\n");
//	fprintf(stderr, "%s\n", resp.data());
//	do_write_vector(comm_fd, resp, (int)resp.size());
	return true;
}

std::vector<char> primary_response;

void handle_replication_request(int fd) {

	char buf[2000];
//	fprintf(stderr, "are we in the function?\n");
	int n = read(fd, buf, sizeof(buf));
	if (n <= 0) {
//		fprintf(stderr, "are we here 1\n");
		close(fd);
		return;
	}
//	fprintf(stderr, "are we waiting?\n");
	std::string req(buf, n);
	size_t line_end = req.find("\r\n");
	if (line_end == std::string::npos) {
//		fprintf(stderr, "are we here 2\n");
		close(fd);
		return;
	}
	std::string first_line = req.substr(0, line_end);
	std::istringstream iss(first_line);
	std::string replicate_word, method, uri, httpver;
	iss >> replicate_word >> method >> uri >> httpver;

	size_t second_slash = uri.find('/',1);
	std::string rowKey = uri.substr(1, second_slash-1);
	std::string colKey = uri.substr(second_slash+1);

	size_t cl_start = req.find("Content-Length:");
	size_t ct_start = req.find("Content-Type:");

	if (cl_start == std::string::npos || ct_start == std::string::npos) {
//		fprintf(stderr, "are we here 3\n");
		close(fd);
		return;
	}
	size_t cl_end = req.find("\r\n", cl_start);
	size_t ct_end = req.find("\r\n", ct_start);
	std::string cl_line = req.substr(cl_start, cl_end-cl_start);
	std::string ct_line = req.substr(ct_start, ct_end-ct_start);

	int content_length = 0;
	{
		std::istringstream iss_cl(cl_line);
		std::string dummy;
		iss_cl >> dummy >> content_length;
	}

	std::string content_type;
	{
		std::istringstream iss_ct(ct_line);
		std::string dummy;
		iss_ct >> dummy >> content_type;
	}

	size_t body_start = req.find("\r\n\r\n");
	if (body_start == std::string::npos) {
//		fprintf(stderr, "are we here 4\n");
		close(fd);
		return;
	}
//	fprintf(stderr, "are we here\n");
	body_start += 4;
	int body_len = n - (int)body_start;
	std::vector<char> data;
	if (body_len < content_length) {
		data.insert(data.end(), req.begin()+body_start, req.end());
		int remain = content_length - body_len;
		std::vector<char> temp(remain);
		int r = read(fd, temp.data(), remain);
		if (r > 0) {
			data.insert(data.end(), temp.begin(), temp.begin()+r);
		}
	} else {
		data.insert(data.end(), req.begin()+body_start, req.begin()+body_start+content_length);
	}

	Status status;
	status.success = false;
//	fprintf(stderr, "method: %s\n", method.c_str());
	if (method == "PUT") {
//		fprintf(stderr, "reaching PUT 2\n");
		status = PUT(kvs, rowKey, colKey, data, content_type, content_length);
		if (status.success) {
//			CURR_KV_SIZE++;
		}
//		if (CURR_KV_SIZE > MAX_KV_SIZE) {
//			int tablet_id = getTabletForRowKey(rowKey);
//			std::string checkpointfile_path = "checkpoints/checkpoints_server"+std::to_string(my_node_id)+"tablet"+std::to_string(tablet_id);
//										write_to_checkpoint_file(tablet_id, checkpointfile_path);
//										kvs.KVmap.clear();
//									}
	} else if (method == "DELETE") {
		status = DELETE(kvs, rowKey, colKey);
	} else if (method == "CPUT") {
		std::string sep = SEPARATOR;
		auto it = std::search(data.begin(), data.end(), sep.begin(), sep.end());
		if (it != data.end()) {
			std::vector<char> old_value(data.begin(), it);
			std::vector<char> new_value(it+sep.size(), data.end());
			status = CPUT(kvs, rowKey, colKey, old_value, new_value, content_type, content_length);
		}
	}

	std::string resp = "ACK\r\n";
	write(fd, resp.c_str(), (int)resp.size());
	close(fd);
}

void running_the_list_of_methods(Method_data mdata){
//	fprintf(stderr, "entered the loop\n");
	size_t lensep = std::strlen(SEPARATOR);
	std::vector<char> separator_vector(SEPARATOR, SEPARATOR + lensep);
	Status status;
	int tablet_id = mdata.tablet_id;
//	fprintf(stderr, "curr sequence num %d\n", row_to_sequence_num[mdata.rowKey]);
//	fprintf(stderr, "curr tablet_id %d\n", tablet_id);
//	fprintf(stderr, "mdata.method %s\n", mdata.method.c_str());
//	fprintf(stderr, "mdata.rowKey %s\n", mdata.rowKey.c_str());
//	fprintf(stderr, "mdata.colKey %s\n", mdata.colKey.c_str());
//	fprintf(stderr, "mdata.binary_data %s\n", mdata.binary_data.data());

	row_to_sequence_num[mdata.rowKey]++;
	std::string checkpointfile_path = "checkpoints/checkpoints_server"+std::to_string(my_node_id)+"tablet"+std::to_string(tablet_id);
						std::string logfile_path = logs+"/logs_server"+std::to_string(my_node_id)+"tablet"+std::to_string(tablet_id);
//						fprintf(stderr, "log file path is %s\n", logfile_path.c_str());
						write_to_log_file(tablet_id,logfile_path, row_to_sequence_num[mdata.rowKey], mdata);

							if (mdata.method == "PUT") {

//								write_to_log_file(tablet_id,logfile_path, row_to_sequence_num[mdata.rowKey], mdata);
								status = PUT(kvs, mdata.rowKey, mdata.colKey, mdata.binary_data, mdata.content_type, mdata.content_length);
								if (status.success) {

	//								CURR_KV_SIZE++;
	//								if (CURR_KV_SIZE > MAX_KV_SIZE) {
	//									write_to_checkpoint_file(tablet_id, checkpointfile_path);
	//									kvs.KVmap.clear();
	//								}
									fprintf(stderr, "successful PUT\n");
									std::string resp = "ACK\r\n";
									write(mdata.comm_fd, resp.c_str(), (int)resp.size());

								} else {
//									fprintf(stderr, "COULD NOT SEND ACK\n");
									fprintf(stderr, "not successful PUT\n");
								}

							} else if (mdata.method == "CPUT") {
								auto getIndex = std::search(mdata.binary_data.begin(), mdata.binary_data.end(), separator_vector.begin(), separator_vector.end());
								if (getIndex != mdata.binary_data.end()) {
									std::vector<char> old_value(mdata.binary_data.begin(), getIndex);
									size_t sepsize = separator_vector.size();
									std::vector<char> new_value(getIndex + sepsize, mdata.binary_data.end());

									status = CPUT(kvs, mdata.rowKey, mdata.colKey, old_value, new_value, mdata.content_type, mdata.content_length);
									if (status.success) {
										std::vector<char> combined = old_value;
										combined.insert(combined.end(), separator_vector.begin(), separator_vector.end());
										combined.insert(combined.end(), new_value.begin(), new_value.end());
										std::string resp = "ACK\r\n";
										write(mdata.comm_fd, resp.c_str(), (int)resp.size());



									}

								}
							} else if (mdata.method == "DELETE") {

								//								write_to_log_file(tablet_id,logfile_path, row_to_sequence_num[mdata.rowKey], mdata);
																status = DELETE(kvs, mdata.rowKey, mdata.colKey);
																if (status.success) {
																	fprintf(stderr, "DELETE succeeded\n");
									//								CURR_KV_SIZE++;
									//								if (CURR_KV_SIZE > MAX_KV_SIZE) {
									//									write_to_checkpoint_file(tablet_id, checkpointfile_path);
									//									kvs.KVmap.clear();
									//								}
																	std::string resp = "ACK\r\n";
																	write(mdata.comm_fd, resp.c_str(), (int)resp.size());

																}

															}
							requests_remaining.erase(row_to_sequence_num[mdata.rowKey]);
}

void revert_local_change(const std::string &row, const std::string &col, const OldValue &old_val) {
	if (old_val.existed) {
		PUT(kvs, row, col, old_val.data, old_val.data_type, old_val.size);
	} else {
		DELETE(kvs, row, col);
	}
}

void *clientFunc(void *arg) {
	int index = *(int*) arg;
	Status status;
	size_t lensep = std::strlen(SEPARATOR);
	std::vector<char> separator_vector(SEPARATOR, SEPARATOR + lensep);
	std::vector<char> buffer(2000);
	int rcvd = 0;
	int comm_fd = connections_arr[index];
	std::string original_request; // store the entire request
	int replication_mode = 0;

	if (print_output == 1) {
		fprintf(stderr, "[%d] New connection\r\n", comm_fd);
		fprintf(stderr, "[%d] S: %s\r\n", comm_fd, first_msg);
	}

	Method_data mdata;
	mdata.ready_for_binary_data = false;
	mdata.comm_fd = comm_fd;
	Request request;
	int restart_mode = 0;
	    // Use the read_request function


	    // Dispatch the request to the appropriate handler

	int version_no = -1;
	main_loop:
	while (!shutting_down) {
		first_run++;

//		dispatch_request(comm_fd, request);
		if ((int)buffer.size() - rcvd < 1000) {
			buffer.resize(buffer.size()*2);
		}
		int n = read(comm_fd, buffer.data() + rcvd, 1000);
		if (n <= 0) {
			if (close(comm_fd) < 0) {
				fprintf(stderr, "[%d] Error in closing connection \r\n", comm_fd);
			}
//			fprintf(stderr,"JUST CLOSED 12\n");
			connections_arr[index] = 0;
			if (print_output == 1) {
				fprintf(stderr, "[%d] Connection closed \r\n ", comm_fd);
			}
			pthread_detach(pthreads[index]);
			pthread_exit(NULL);
		}

		rcvd += n;
		fprintf(stderr, "%s\n", buffer.data());
		original_request.append(buffer.data()+rcvd-n, n); // append read data to original_request
		std::string temp_req(buffer.data(), rcvd);
		size_t line_end = temp_req.find("\r\n");
//		fprintf(stderr, "buffer in the start : %s\n", buffer.data());

		if (line_end != std::string::npos) {
			std::string first_line = temp_req.substr(0, line_end);
//			fprintf(stderr, "first line : %s\n", first_line.c_str());
			std::istringstream iss(first_line);
			std::string replicate_word;
			iss >> replicate_word;
			if (replicate_word.substr(0, 9) == "REPLICATE") {
//				fprintf(stderr, "replication\n");
				replication_mode = 1;
				std::string num_string = replicate_word.substr(9);
				version_no = std::stoi(num_string);
				mdata.curr_seq = version_no;
//				fprintf(stderr, "version num: %d\n", version_no);
				//need to shift the buffer by REPLICATE
				buffer.erase(buffer.begin(), buffer.begin() + 10 + num_string.size());
//				handle_replication_request(comm_fd);
//				fprintf(stderr, "buffer now %s\n", buffer.data());
//
//				connections_arr[index] = 0;
//				if (print_output == 1) {
//					fprintf(stderr, "[%d] Connection closed after replication\r\n ", comm_fd);
//				}
//				pthread_detach(pthreads[index]);
//				pthread_exit(NULL);
			}
		}
		//Rollback
		{
			std::string tmp_str(buffer.data(), rcvd);
			size_t pos_rollback = tmp_str.find("ROLLBACK ");

			if (pos_rollback != std::string::npos && tmp_str.find("\r\n") != std::string::npos) {
				std::istringstream iss(tmp_str);
				std::string cmd, rowKey, colKey;
				int seq;
				iss >> cmd >> seq >> rowKey >> colKey;
				if (cmd == "ROLLBACK") {
					if (old_values_map_backup.find(seq) != old_values_map_backup.end()) {
						OldValue &ov = old_values_map_backup[seq];
						revert_local_change(rowKey, colKey, ov);
						old_values_map_backup.erase(seq);
					}
					std::string resp = "ACK\r\n";
					do_write(comm_fd, resp.c_str(), (int)resp.size());
					rcvd = 0;
					memset(buffer.data(),0,buffer.size());

					continue;
				}
			}
		}

		if (mdata.ready_for_binary_data) {
			if (rcvd >= mdata.content_length) {
				if (replication_mode == 0) {
					mdata.binary_data = std::vector<char>(buffer.begin(), buffer.begin() + mdata.content_length);
//					fprintf(stderr,"binary_data: %s\n", mdata.binary_data.data());
					buffer.erase(buffer.begin(), buffer.begin() + mdata.content_length);
					rcvd -= mdata.content_length;
					mdata.ready_for_binary_data = false;

					int tablet_id = getTabletForRowKey(mdata.rowKey);
					int primary_node = getPrimaryNodeForTablet(tablet_id);
//					fprintf(stderr, "THE TABLET IS: %d\n", tablet_id);
//					fprintf(stderr, "THE PRIMARY NODE IS: %d\n", primary_node);
					int test_seq_num = 0;
					std::string checkpointfile_path = "checkpoints/checkpoints_server"+std::to_string(my_node_id)+"tablet"+std::to_string(tablet_id);
					std::string logfile_path = logs+"/logs_server"+std::to_string(my_node_id)+"tablet"+std::to_string(tablet_id);


					//Rollback
					OldValue old_val;
					{
						Status original_status = GET(kvs, mdata.rowKey, mdata.colKey);
						if (original_status.success) {
							old_val.existed = true;
							old_val.data = original_status.binary_data;
							old_val.data_type = original_status.data_type;
							old_val.size = (int)original_status.binary_data.size();
						} else {
							old_val.existed = false;
						}
					}
					if (mdata.method.find("CP_ENTRY") == 0){
												//need to extract the tablet_no
												std::string cp_entry = "CP_ENTRY";
												std::string get_number = mdata.method.substr(cp_entry.length());
												int tablet_no = std::stoi(get_number);

												map_of_checkpoint_entries[tablet_no].method_vec.push_back(mdata);
												std::string resp = "ACK\r\n";
												write(comm_fd, resp.c_str(), (int)resp.size());
												goto done;

												}
					else if (primary_node != my_node_id) {
						// Forward entire original_request to primary

						forward_request_to_primary(comm_fd, original_request, primary_node);

						break; // after forwarding, we are done
					} else {
//						fprintf(stderr, "THIS IS PRIMARY\n");
						//primary node


						if (mdata.method == "PUT") {
							if (row_to_sequence_num.find(mdata.rowKey) != row_to_sequence_num.end()) {
															row_to_sequence_num[mdata.rowKey]++;
														} else {
															row_to_sequence_num[mdata.rowKey] =1;
														}
							write_to_log_file(tablet_id,logfile_path, row_to_sequence_num[mdata.rowKey], mdata);



							status = PUT(kvs, mdata.rowKey, mdata.colKey, mdata.binary_data, mdata.content_type, mdata.content_length);
							if (status.success) {
//								CURR_KV_SIZE++;
//								if (CURR_KV_SIZE > MAX_KV_SIZE) {
//									write_to_checkpoint_file(tablet_id, checkpointfile_path);
//									kvs.KVmap.clear();
//								}
								fprintf(stderr, "status.success for PUT true\n");
								if (!replicate_to_backups("PUT", mdata.rowKey, mdata.colKey, mdata.content_type, mdata.binary_data)) {
									//Rollback
									fprintf(stderr, "replication.success for PUT false\n");
									revert_local_change(mdata.rowKey, mdata.colKey, old_val);
									status.success = false;
								}
							}

							std::vector<char> resp = formatResponse(status, mdata);
							do_write_vector(comm_fd, resp, (int)resp.size());

						} else if (mdata.method == "CPUT") {
							if (row_to_sequence_num.find(mdata.rowKey) != row_to_sequence_num.end()) {
																						row_to_sequence_num[mdata.rowKey]++;
																					} else {
																						row_to_sequence_num[mdata.rowKey] =1;
																					}
														write_to_log_file(tablet_id,logfile_path, row_to_sequence_num[mdata.rowKey], mdata);

							auto getIndex = std::search(mdata.binary_data.begin(), mdata.binary_data.end(), separator_vector.begin(), separator_vector.end());
							if (getIndex != mdata.binary_data.end()) {
								std::vector<char> old_value(mdata.binary_data.begin(), getIndex);
								size_t sepsize = separator_vector.size();
								std::vector<char> new_value(getIndex + sepsize, mdata.binary_data.end());
//								write_to_log_file(tablet_id,logfile_path, test_seq_num, mdata);
								status = CPUT(kvs, mdata.rowKey, mdata.colKey, old_value, new_value, mdata.content_type, mdata.content_length);
								if (status.success) {
									std::vector<char> combined = old_value;
									combined.insert(combined.end(), separator_vector.begin(), separator_vector.end());
									combined.insert(combined.end(), new_value.begin(), new_value.end());
									if (!replicate_to_backups("CPUT", mdata.rowKey, mdata.colKey, mdata.content_type, combined)) {
										//Rollback
										revert_local_change(mdata.rowKey, mdata.colKey, old_val);
										status.success = false;
									}
								}
								std::vector<char> resp = formatResponse(status, mdata);
								do_write_vector(comm_fd, resp, (int)resp.size());
							}
						} else if (mdata.method == "RECOVERY") {
							std::string jsonString(mdata.binary_data.begin(), mdata.binary_data.end());
							nlohmann::json afterparse = nlohmann::json::parse(jsonString);
//							fprintf(stderr, "in recovery\n");
//							std::string number_lst(mdata.binary_data.begin(), mdata.binary_data.end());
//							std::istringstream traversenums;
//							std::vector<int> list_of_sequence_nums;
//							int getnum;
//							while (traversenums >> getnum) {
//								list_of_sequence_nums.push_back(getnum);
//							}
//							for (int i = 0; i < list_of_sequence_nums.size(); i++) {
//								fprintf(stderr, "%d ", list_of_sequence_nums[i]);
//							}
							fprintf(stderr, "\n");
							std::vector<int> list_of_sequence_nums = afterparse["sequence_numbers"];
							std::string logfile_path_recovery = logs+"/logs_server"+std::to_string(my_node_id)+"tablet"+mdata.rowKey;
//							fprintf(stderr, "tablet number %s\n", mdata.rowKey.c_str());
							parseOwnLogFile(logfile_path_recovery, list_of_sequence_nums, std::stoi(mdata.colKey));

						} else if (mdata.method == "CHECKPOINTVERSION") {
							//requesting to check if there is a need to send the checkpoint
//								fprintf(stderr, "RECEIVED CHECKPOINTVERSION\n");
							std::string the_num(mdata.binary_data.begin(), mdata.binary_data.end());
							int get_version = std::stoi(the_num);
							int tablet_no = std::stoi(mdata.rowKey);
							int get_node_id = std::stoi(mdata.colKey);
							std::string checkpointfile_path = "checkpoints/checkpoints_server"+std::to_string(my_node_id)+"tablet"+std::to_string(tablet_no);
							pthread_mutex_lock(&mutex_for_checkpoint[tablet_no]);
//							fprintf(stderr, "locke/?d checkpoint version");
							parseOwnCheckpointFile(tablet_no, checkpointfile_path, get_node_id, get_version);
							pthread_mutex_unlock(&mutex_for_checkpoint[tablet_no]);
//							fprintf(stderr, "unlocked checkpoint version");

						}
					}
				} else {
					//replication mode
					int tablet_id = getTabletForRowKey(mdata.rowKey);
					mdata.binary_data = std::vector<char>(buffer.begin(), buffer.begin() + mdata.content_length);
					buffer.erase(buffer.begin(), buffer.begin() + mdata.content_length);
					rcvd -= mdata.content_length;
					mdata.ready_for_binary_data = false;
					if (row_to_sequence_num.find(mdata.rowKey) == row_to_sequence_num.end()) {
						row_to_sequence_num[mdata.rowKey] = 0;
					}
					requests_remaining[mdata.curr_seq] = mdata;
//					fprintf(stderr, "version number here is :%d\n", version_no);
					mdata.comm_fd = comm_fd;
					mdata.tablet_id = tablet_id;

					//Rollback
					{
						Status original_status = GET(kvs, mdata.rowKey, mdata.colKey);
						OldValue ov;
						ov.existed = original_status.success;
						if (ov.existed) {
							ov.data = original_status.binary_data;
							ov.data_type = original_status.data_type;
							ov.size = (int)original_status.binary_data.size();
						}
						// Store the old value keyed by the sequence number
						old_values_map_backup[version_no] = ov;
					}

//					fprintf(stderr, "[%d] ", comm_fd);
//					fprintf(stderr, "current most recent sequence number seen for ROW: %s", mdata.rowKey.c_str());
//					fprintf(stderr, "[%d] ", comm_fd);
//					fprintf(stderr, "is %d\n",row_to_sequence_num[mdata.rowKey]);
						while (requests_remaining.find(row_to_sequence_num[mdata.rowKey] + 1) != requests_remaining.end()) {
							//keep executing
							running_the_list_of_methods(mdata);
//							fprintf(stderr, "[%d] ", comm_fd);
//							fprintf(stderr, "are we looping?\n");
						}
						replication_mode = 0;
						goto done;





				}

			}
			continue;
		}

		char *crlf_found = strstr(buffer.data(), "\r\n\r\n");
		while (crlf_found != NULL && !mdata.ready_for_binary_data) {
			*(crlf_found+2) = '\0';
			int length_msg = (int)(crlf_found - buffer.data())+2;
			std::string getbuffer(buffer.data(), length_msg);

			size_t find_eol = getbuffer.find("\r\n");
			std::string remaining;
			if (find_eol != std::string::npos) {
				std::string f_line = getbuffer.substr(0, find_eol);
				std::string func_name;
				std::string rowpluscolkey;
				std::string httpheader;
				{
					std::istringstream iss(f_line);
					iss >> func_name >> rowpluscolkey >> httpheader;
				}
				size_t getslash = rowpluscolkey.find('/');
				size_t getslash_2 = rowpluscolkey.find('/', getslash + 1);
				size_t second_index = getslash_2 - getslash - 1;
				std::string rowKey = rowpluscolkey.substr(getslash + 1, second_index);
				std::string colKey = rowpluscolkey.substr(getslash_2 + 1);
				mdata.method = func_name;
				mdata.rowKey = rowKey;
				mdata.colKey = colKey;
				mdata.httpHeader = httpheader;
//				fprintf(stderr, "[%d]IMPORTANT\n", comm_fd);
//				fprintf(stderr, "[%d] METHOD NAME ", comm_fd);
//				fprintf(stderr, "%s\n", mdata.method.c_str());
				remaining = getbuffer.substr(find_eol+2);
				if (mdata.method == "POST" || mdata.method == "OPTIONS") {
					rcvd -= (int)((crlf_found - buffer.data()) + 4);
					memmove(buffer.data(), crlf_found+4, rcvd);
					memset(buffer.data()+rcvd,0,buffer.size()-rcvd);
					if (mdata.rowKey == "stop") {
						allow_run = false;
						suspend_checkpointing = 0;
						//this means we need to crash this node
                        json my_stop_json = {
                        {"status", "success"},
                        {"message", "Node stopped successfully."}
                        };
                        std::vector<char> resp = formatResponseList(mdata, my_stop_json.dump());
                        do_write_vector(comm_fd, resp, (int)resp.size());
                        goto done;
					} else if (mdata.rowKey == "restart") {
						//need to restart this node
						allow_run = true;
//						fprintf(stderr, "restarted\n");
						restart_mode = 1;
                        json my_restart_json = {
                        {"status", "success"},
                        {"message", "Node restarted successfully."}
                        };
                        fprintf(stderr, "restarted the node\n");
                        std::vector<char> resp = formatResponseList(mdata, my_restart_json.dump());
                        do_write_vector(comm_fd, resp, (int)resp.size()); 
//                        std::vector<int> alltablets = getAllTabletsForNode(my_node_id);
//                        						        for (int i = 0; i < alltablets.size(); i++) {
//                        						        	std::string logfile_path = logs+"/logs_server"+std::to_string(my_node_id)+"tablet"+std::to_string(alltablets[i]);
//                        						        	fprintf(stderr, "for tablet: %d\n", alltablets[i]);
//                        						        	std::string log_file_numbers = restart_after_crash(logfile_path);
//                        						        	if (log_file_numbers != "EMPTY") {
//                        						        		std::string responseForRecovery = formatResponseRecovery(alltablets[i], log_file_numbers);
//                        										//need to ask the primary for the remaining functions
//                        										int primary_node = getPrimaryNodeForTablet(alltablets[i]);
//                        										fprintf(stderr, "THE TABLET IS: %d\n", alltablets[i]);
//                        										fprintf(stderr, "THE PRIMARY NODE IS: %d\n", primary_node);
//                        										if (primary_node != my_node_id) {
//                        																// Forward entire original_request to primary
//
//                        																fprintf(stderr, "FORWARDED TO PRIMARY RECOVERY \n");
//                        																fprintf(stderr, "forwarding this %s\n", responseForRecovery.c_str());
//                        																forward_request_to_primary_for_recovery(comm_fd, responseForRecovery, primary_node);
//                        																fprintf(stderr, "[%d] ", comm_fd);
//                        																fprintf(stderr, "AFTER FORWARDED TO PRIMARY Recovery DONE\n");
//                        																 // after forwarding, we are done
//                        															}
//                        						        	}
//
//
//                        						        }
						goto done;
					}

				} else if (mdata.method == "GET" && mdata.rowKey == "get_kvs_store") {
					//this is for GET the current Key value store

						std::string json_str_kvs = getJsonForKVstore();

						std::vector<char> resp = formatResponseList(mdata, json_str_kvs);
						do_write_vector(comm_fd, resp, (int)resp.size());
//						close(comm_fd);


					goto done;
				}
				if (!allow_run) {
//									fprintf(stderr, "Heartbeat paused. Waiting for RESTART command.\n");
														sleep(1); // Polling interval while paused
//														fprintf(stderr, "SENT TO DONE\n");
														goto done;
								}
			}

			size_t find_eol_2 = remaining.find("\r\n");
			std::string remaining_2;
			if (find_eol_2 != std::string::npos) {
				std::string s_line = remaining.substr(0, find_eol_2);
				size_t get_space = s_line.find(' ');
				get_space+=1;
				std::string get_num_str = s_line.substr(get_space);
//				fprintf(stderr, "the method is %s\n", mdata.method.c_str());
//				fprintf(stderr, "the rowkey is %s\n",mdata.rowKey.c_str());
//				fprintf(stderr, "the num string %s\n", get_num_str.c_str());
				mdata.content_length = std::stoul(get_num_str);
				remaining_2 = remaining.substr(find_eol_2+2);
			}

			if (mdata.method == "PUT" || mdata.method == "CPUT" || mdata.method == "RECOVERY" || mdata.method.find("CP_ENTRY") == 0 || mdata.method == "CHECKPOINTVERSION") {
				size_t find_eol_3 = remaining_2.find("\r\n");
				if (find_eol_3 != std::string::npos) {
					std::string t_line = remaining_2.substr(0, find_eol_3);
					size_t get_space = t_line.find(' ');
					get_space+=1;
					std::string get_content_type = t_line.substr(get_space);
					mdata.content_type = get_content_type;
				}
				mdata.ready_for_binary_data = true;
				rcvd -= (int)((crlf_found - buffer.data()) + 4);
				memmove(buffer.data(), crlf_found+4, rcvd);
				memset(buffer.data()+rcvd, 0, buffer.size()-rcvd);
				if (rcvd >= mdata.content_length) {
					mdata.binary_data = std::vector<char>(buffer.begin(), buffer.begin()+mdata.content_length);
					buffer.erase(buffer.begin(), buffer.begin()+mdata.content_length);
					rcvd -= mdata.content_length;
					mdata.ready_for_binary_data = false;
					if (replication_mode == 0) {
						int tablet_id = getTabletForRowKey(mdata.rowKey);
											int primary_node = getPrimaryNodeForTablet(tablet_id);
//											fprintf(stderr, "THE TABLET IS: %d\n", tablet_id);
//											fprintf(stderr, "THE PRIMARY NODE IS: %d\n", primary_node);

											// Rollback
											OldValue old_val;
											{
												Status original_status = GET(kvs, mdata.rowKey, mdata.colKey);
												if (original_status.success) {
													old_val.existed = true;
													old_val.data = original_status.binary_data;
													old_val.data_type = original_status.data_type;
													old_val.size = (int)original_status.binary_data.size();
												} else {
													old_val.existed = false;
												}
											}
											if (mdata.method.find("CP_ENTRY") == 0) {
													//need to extract the tablet_no
													std::string cp_entry = "CP_ENTRY";
													std::string get_number = mdata.method.substr(cp_entry.length());
													int tablet_no = std::stoi(get_number);

													map_of_checkpoint_entries[tablet_no].method_vec.push_back(mdata);
													std::string resp = "ACK\r\n";
													write(comm_fd, resp.c_str(), (int)resp.size());
													goto done;


												}
											else if (primary_node != my_node_id) {
												// Forward entire original_request to primary
//												fprintf(stderr, "FORWARDING request to primary from here\n");
												forward_request_to_primary(comm_fd, original_request, primary_node);
												//it needs to listen for a success and then send that to the FE server
												// after forwarding, close and break
												goto done;
											} else {
//												fprintf(stderr, "this is the primary node\n");
												std::string logfile_path = logs+"/logs_server"+std::to_string(my_node_id)+"tablet"+std::to_string(tablet_id);
												if (mdata.method == "PUT") {
													if (row_to_sequence_num.find(mdata.rowKey) != row_to_sequence_num.end()) {
																						row_to_sequence_num[mdata.rowKey]++;
																					} else {
																						row_to_sequence_num[mdata.rowKey] =1;
																					}
														write_to_log_file(tablet_id,logfile_path, row_to_sequence_num[mdata.rowKey], mdata);

													status = PUT(kvs, mdata.rowKey, mdata.colKey, mdata.binary_data, mdata.content_type, mdata.content_length);
//													fprintf(stderr, "row key %s\n", mdata.rowKey.c_str());
//													fprintf(stderr, "col key %s\n", mdata.colKey.c_str());
													if (status.success && !replicate_to_backups("PUT", mdata.rowKey, mdata.colKey, mdata.content_type, mdata.binary_data)) {
//														CURR_KV_SIZE++;
														//Rollback
														fprintf(stderr, "we were succesfful with PUT but unsuccesful with replicate\n ");
														revert_local_change(mdata.rowKey, mdata.colKey, old_val);
//														fprintf(stderr, "PUT didn't work\n");
//														fprintf(stderr, "Are we here?\n");
														status.success = false;
													}
//													if (CURR_KV_SIZE > MAX_KV_SIZE) {
//														std::string checkpointfile_path = "checkpoints/checkpoints_server"+std::to_string(my_node_id)+"tablet"+std::to_string(tablet_id);
//														write_to_checkpoint_file(tablet_id, checkpointfile_path);
//														kvs.KVmap.clear();
//													}
													fprintf(stderr, "we were succesfful with PUT\n ");
													std::vector<char> resp = formatResponse(status, mdata);
													do_write_vector(comm_fd, resp, (int)resp.size());
//													fprintf(stderr, "PUT OVER\n");
												} else if (mdata.method == "CPUT") {
													if (row_to_sequence_num.find(mdata.rowKey) != row_to_sequence_num.end()) {
																												row_to_sequence_num[mdata.rowKey]++;
																											} else {
																												row_to_sequence_num[mdata.rowKey] =1;
																											}
																				write_to_log_file(tablet_id,logfile_path, row_to_sequence_num[mdata.rowKey], mdata);

													auto getIndex = std::search(mdata.binary_data.begin(), mdata.binary_data.end(), separator_vector.begin(), separator_vector.end());
													if (getIndex != mdata.binary_data.end()) {
														std::vector<char> old_value(mdata.binary_data.begin(), getIndex);
														size_t sepsize = separator_vector.size();
														std::vector<char> new_value(getIndex + sepsize, mdata.binary_data.end());
														status = CPUT(kvs, mdata.rowKey, mdata.colKey, old_value, new_value, mdata.content_type, mdata.content_length);
														if (status.success) {
															std::vector<char> combined = old_value;
															combined.insert(combined.end(), separator_vector.begin(), separator_vector.end());
															combined.insert(combined.end(), new_value.begin(), new_value.end());
															if (!replicate_to_backups("CPUT", mdata.rowKey, mdata.colKey, mdata.content_type, combined)) {
																//Rollback
																revert_local_change(mdata.rowKey, mdata.colKey, old_val);
																status.success = false;
															}
														}
														std::vector<char> resp = formatResponse(status, mdata);
														do_write_vector(comm_fd, resp, (int)resp.size());
													}
												} else if (mdata.method == "RECOVERY") {
													fprintf(stderr, "within RECOVERY part\n");
													std::string jsonString(mdata.binary_data.begin(), mdata.binary_data.end());
													nlohmann::json afterparse = nlohmann::json::parse(jsonString);
													std::vector<int> list_of_sequence_nums = afterparse["sequence_numbers"];
//													std::string number_lst(mdata.binary_data.begin(), mdata.binary_data.end());
//													std::istringstream traversenums;
//													std::vector<int> list_of_sequence_nums;
//													int getnum;
//													while (traversenums >> getnum) {
//														list_of_sequence_nums.push_back(getnum);
//													}
//													for (int i = 0; i < list_of_sequence_nums.size(); i++) {
//														fprintf(stderr, "%d ", list_of_sequence_nums[i]);
//													}
//													fprintf(stderr, "\n");
													std::string logfile_path_recovery = logs+"/logs_server"+std::to_string(my_node_id)+"tablet"+mdata.rowKey;
//													fprintf(stderr, "tablet number %s\n", mdata.rowKey.c_str());
													parseOwnLogFile(logfile_path_recovery, list_of_sequence_nums, std::stoi(mdata.colKey));

												}
												else if (mdata.method == "CHECKPOINTVERSION") {
																			//requesting to check if there is a need to send the checkpoint
																			std::string the_num(mdata.binary_data.begin(), mdata.binary_data.end());
																			int get_version = std::stoi(the_num);
																			int tablet_no = std::stoi(mdata.rowKey);
																			int get_node_id = std::stoi(mdata.colKey);
																			std::string checkpointfile_path = "checkpoints/checkpoints_server"+std::to_string(my_node_id)+"tablet"+std::to_string(tablet_no);
																			pthread_mutex_lock(&mutex_for_checkpoint[tablet_no]);
//																			fprintf(stderr, "locked checkpoint version");
																			parseOwnCheckpointFile(tablet_no, checkpointfile_path, get_node_id, get_version);

																			pthread_mutex_unlock(&mutex_for_checkpoint[tablet_no]);
//																			fprintf(stderr, "unlocked checkpoint version");
																			goto done;
																		}
												}

					} else {
						//node is replicating right now
						int tablet_id = getTabletForRowKey(mdata.rowKey);
						std::string logfile_path = logs+"/logs_server"+std::to_string(my_node_id)+"tablet"+std::to_string(tablet_id);
						if (row_to_sequence_num.find(mdata.rowKey) == row_to_sequence_num.end()) {
												row_to_sequence_num[mdata.rowKey] = 0;
											}
											requests_remaining[version_no] = mdata;
											mdata.comm_fd = comm_fd;
											mdata.tablet_id = tablet_id;

											while (requests_remaining.find(row_to_sequence_num[mdata.rowKey] + 1) != requests_remaining.end()) {
													//keep executing
													running_the_list_of_methods(mdata);
//													fprintf(stderr, "are we looping?\n");
												}
												replication_mode = 0;
												goto done;
					}

				}
			} else {
				// GET/DELETE no binary data
				rcvd -= (int)((crlf_found - buffer.data()) + 4);
				memmove(buffer.data(), crlf_found+4, rcvd);
				memset(buffer.data()+rcvd,0,buffer.size()-rcvd);

				int tablet_id = getTabletForRowKey(mdata.rowKey);
				int primary_node = getPrimaryNodeForTablet(tablet_id);
//				fprintf(stderr, "THE TABLET IS: %d\n", tablet_id);
//				fprintf(stderr, "THE PRIMARY NODE IS: %d\n", primary_node);
				if (mdata.method == "GET") {
					// Allow GET to be handled by any server (primary or backup)
					int test_seq_num = 0;
					std::string logfile_path = logs+"/logs_server"+std::to_string(my_node_id)+"tablet"+std::to_string(tablet_id);
					std::string checkpointfile_path = "checkpoints/checkpoints_server"+std::to_string(my_node_id)+"tablet"+std::to_string(tablet_id);
					write_to_log_file(tablet_id,logfile_path, row_to_sequence_num[mdata.rowKey], mdata);
					//for testing
					std::vector<char> binary_value_2 = {'a', '\r', '\n', 'b', 'c'};
					Method_data mdata_test;
					mdata_test.rowKey = mdata.rowKey;
					mdata_test.colKey = mdata.colKey;
					mdata_test.content_length = binary_value_2.size();
					mdata_test.content_type = mdata.content_type;
					mdata_test.binary_data = binary_value_2;
//					fprintf(stderr, "rowkey being sent %s\n", mdata_test.rowKey.c_str());
//					PUT(kvs,  mdata.rowKey, mdata.colKey, mdata_test.binary_data , "application/octet-stream", mdata_test.binary_data.size());
//					write_to_checkpoint_file(tablet_id, checkpointfile_path, mdata_test);

					status = GET(kvs, mdata.rowKey, mdata.colKey);
					if (!status.success) {
						//need to check in the actual checkpoint file then
//						fprintf(stderr,"trying to retrieve from checkpoint file\n");
						status = get_from_checkpoint_file(tablet_id, checkpointfile_path, mdata.rowKey, mdata.colKey);
						if (status.success) {
//							fprintf(stderr,"retrieve from checkpoint file SUCCEEDED\n");
							//need to add it to our map
							PUT(kvs,  mdata.rowKey, mdata.colKey, status.binary_data, status.data_type, status.binary_data.size());
//							CURR_KV_SIZE++;
//							if (CURR_KV_SIZE > MAX_KV_SIZE) {
//															write_to_checkpoint_file(tablet_id, checkpointfile_path);
//															kvs.KVmap.clear();
//														}
						}
					}
					std::vector<char> resp = formatResponse(status, mdata);
					do_write_vector(comm_fd, resp, (int)resp.size());
				} else if (mdata.method == "DELETE"){
					//DELETE
					std::string logfile_path = logs+"/logs_server"+std::to_string(my_node_id)+"tablet"+std::to_string(tablet_id);
					//Rollback
					OldValue old_val;
					{
						Status original_status = GET(kvs, mdata.rowKey, mdata.colKey);
						if (original_status.success) {
							old_val.existed = true;
							old_val.data = original_status.binary_data;
							old_val.data_type = original_status.data_type;
							old_val.size = (int)original_status.binary_data.size();
						} else {
							old_val.existed = false;
						}
					}

					if (replication_mode == 0) {
						//not replication
						int tablet_id = getTabletForRowKey(mdata.rowKey);
						int primary_node = getPrimaryNodeForTablet(tablet_id);
//						fprintf(stderr, "THE TABLET IS: %d\n", tablet_id);
//						fprintf(stderr, "THE PRIMARY NODE IS: %d\n", primary_node);
						if (primary_node != my_node_id) {
												forward_request_to_primary(comm_fd, original_request, primary_node);
												goto done;
											} else {
												if (row_to_sequence_num.find(mdata.rowKey) != row_to_sequence_num.end()) {
													row_to_sequence_num[mdata.rowKey]++;
												} else {
													row_to_sequence_num[mdata.rowKey] =1;
												}
												write_to_log_file(tablet_id,logfile_path, row_to_sequence_num[mdata.rowKey], mdata);
												status = DELETE(kvs, mdata.rowKey, mdata.colKey);
												if (status.success && !replicate_to_backups("DELETE", mdata.rowKey, mdata.colKey, "application/octet-stream", {})) {
													//Rollback
													fprintf(stderr, "DELETE passed but replicate failed\n");
													revert_local_change(mdata.rowKey, mdata.colKey, old_val);
													status.success = false;
												}
												std::vector<char> resp = formatResponse(status, mdata);
												do_write_vector(comm_fd, resp, (int)resp.size());
											}
					} else {
						//replication mode
						int tablet_id = getTabletForRowKey(mdata.rowKey);
						std::string logfile_path = logs+"/logs_server"+std::to_string(my_node_id)+"tablet"+std::to_string(tablet_id);
						if (row_to_sequence_num.find(mdata.rowKey) == row_to_sequence_num.end()) {
												row_to_sequence_num[mdata.rowKey] = 0;
											}
											requests_remaining[version_no] = mdata;
											mdata.comm_fd = comm_fd;
											mdata.tablet_id = tablet_id;

											while (requests_remaining.find(row_to_sequence_num[mdata.rowKey] + 1) != requests_remaining.end()) {
													//keep executing
													running_the_list_of_methods(mdata);
//													fprintf(stderr, "are we looping?\n");
												}
												replication_mode = 0;
												goto done;

					}


					} else if (mdata.method == "NEWPRIMARY") {
//						fprintf(stderr, "did we reach here?");
						int the_tablet = std::stoi(mdata.rowKey);
						int new_primary = std::stoi(mdata.colKey);
//						fprintf(stderr, "setting %d to be the new primary ",new_primary);
//						fprintf(stderr, "for tablet: %d\n",the_tablet);
						setPrimaryNodeForTablet(the_tablet, new_primary);
						std::string resp = "ACK\r\n";
							write(comm_fd, resp.c_str(), (int)resp.size());
//							std::string logfile_path = logs+"/logs_server"+std::to_string(my_node_id)+"tablet"+std::to_string(the_tablet);
//							fprintf(stderr, "for tablet: %d\n", the_tablet);
//							std::string log_file_numbers = restart_after_crash(logfile_path);
////							if (log_file_numbers != "EMPTY") {
//								std::string responseForRecovery = formatResponseRecovery(the_tablet, log_file_numbers);
//								//need to ask the primary for the remaining functions
//								int primary_node = getPrimaryNodeForTablet(the_tablet);
//								fprintf(stderr, "THE TABLET ISrep: %d\n", the_tablet);
//								fprintf(stderr, "THE PRIMARY NODE IS: %d\n", primary_node);
//								if (primary_node != my_node_id) {
//														// Forward entire original_request to primary
//
//														fprintf(stderr, "FORWARDED TO PRIMARY RECOVERY \n");
//														fprintf(stderr, "forwarding this %s\n", responseForRecovery.c_str());
//														forward_request_to_primary_for_recovery(comm_fd, responseForRecovery, primary_node);
//														fprintf(stderr, "[%d] ", comm_fd);
//														fprintf(stderr, "AFTER FORWARDED TO PRIMARY Recovery DONE\n");
//														 // after forwarding, we are done
//													}
////							}


					} else if (mdata.method == "DONE") {
//						fprintf(stderr, "RECEIVED DONE\n");
						std::vector<int> alltablets = getAllTabletsForNode(my_node_id);
						std::vector<std::string> logfilerecoveryrequests(alltablets.size());
						for (int i = 0; i < alltablets.size(); i++) {
													std::string logfile_path = logs+"/logs_server"+std::to_string(my_node_id)+"tablet"+std::to_string(alltablets[i]);
//													fprintf(stderr, "for tablet: %d\n", alltablets[i]);
													std::string log_file_numbers = restart_after_crash(logfile_path);
													logfilerecoveryrequests[i] = log_file_numbers;
						}
						fprintf(stderr, "created all the logfilerecovery requests\n");
						for (int i = 0; i < alltablets.size(); i++) {
							std::string logfile_path = logs+"/logs_server"+std::to_string(my_node_id)+"tablet"+std::to_string(alltablets[i]);
//							fprintf(stderr, "for tablet: %d\n", alltablets[i]);
							std::string log_file_numbers = logfilerecoveryrequests[i];
														if (log_file_numbers != "EMPTY") {
															std::string responseForRecovery = formatResponseRecovery(alltablets[i], log_file_numbers);
															//need to ask the primary for the remaining functions
															int primary_node = getPrimaryNodeForTablet(alltablets[i]);
//															fprintf(stderr, "THE TABLET IS: %d\n", alltablets[i]);
//															fprintf(stderr, "THE PRIMARY NODE IS: %d\n", primary_node);
															if (primary_node != my_node_id) {
																					// Forward entire original_request to primary

//																					fprintf(stderr, "FORWARDED TO PRIMARY RECOVERY \n");
//																					fprintf(stderr, "forwarding this %s\n", responseForRecovery.c_str());
																					forward_request_to_primary_for_recovery(comm_fd, responseForRecovery, primary_node);
//																					fprintf(stderr, "[%d] ", comm_fd);
//																					fprintf(stderr, "AFTER FORWARDED TO PRIMARY Recovery DONE\n");
																					 // after forwarding, we are done
																				}
														}
//														fprintf(stderr, "Logging recovery requests sent for %d\n", alltablets[i]);
//														fprintf(stderr, "replay log file\n");
														replayLogFile(logfile_path);
							//first need to check if it has the most updated checkpoint before getting the most updated logfile
							std::string checkpointfile_path = "checkpoints/checkpoints_server"+std::to_string(my_node_id)+"tablet"+std::to_string(tablet_id);
							std::ifstream reader(checkpointfile_path, std::ios::binary);
							int get_ver_no;
							reader >> get_ver_no;
							if (reader.fail()) {
								//no version number
								get_ver_no = 0;
							} else {
								//need to send checkpoint version to primary
								int primary_node = getPrimaryNodeForTablet(alltablets[i]);
//								fprintf(stderr, "THE TABLET IS: %d\n", alltablets[i]);
//								fprintf(stderr, "THE PRIMARY NODE IS: %d\n", primary_node);
								if (primary_node != my_node_id) {
														// Forward entire original_request to primary
									std::string responseForCheckpointVersion = formatResponse_checkpoint_ver(alltablets[i], std::to_string(get_ver_no));

//														fprintf(stderr, "FORWARDED TO PRIMARY RECOVERY \n");
//														fprintf(stderr, "forwarding this %s\n", responseForCheckpointVersion.c_str());
														forward_request_to_primary_for_recovery(comm_fd, responseForCheckpointVersion, primary_node);
//														fprintf(stderr, "[%d] ", comm_fd);
//														fprintf(stderr, "AFTER FORWARDED TO PRIMARY Checkpoint Version DONE\n");
														 // after forwarding, we are done
													}


//							fprintf(stderr, "Checkpoint recovery requests sent for %d\n", alltablets[i]);

							}

						}
						suspend_checkpointing = 1;
						goto done;

					} else if (mdata.method == "CHECKPOINT") {
						//need to checkpoint
						int tablet_id = std::stoi(mdata.rowKey);
						int version_num = std::stoi(mdata.colKey);
						std::string checkpointfile_path = "checkpoints/checkpoints_server"+std::to_string(my_node_id)+"tablet"+std::to_string(tablet_id);
						write_to_checkpoint_file(tablet_id, checkpointfile_path, version_num);
						std::string logfile_path = logs+"/logs_server"+std::to_string(my_node_id)+"tablet"+std::to_string(tablet_id);
						std::ofstream refresh_logger(logfile_path);
						std::string resp = "ACK\r\n";
							write(comm_fd, resp.c_str(), (int)resp.size());
							goto done;

					} else if (mdata.method == "CP_SEND") {
						//means we are about to receive entries for a new checkpoint file
						int tablet_no = std::stoi(mdata.rowKey);
						int ver = std::stoi(mdata.colKey);
						map_of_checkpoint_entries[tablet_no].method_vec = std::vector<Method_data>();
						map_of_checkpoint_entries[tablet_no].version_num = ver;
						std::string resp = "ACK\r\n";
						write(mdata.comm_fd, resp.c_str(), (int)resp.size());
						goto done;

					}
					else if (mdata.method == "CPFILEDONE") {
									//done receiving entries and now need write them

									int tablet_no = std::stoi(mdata.rowKey);
									int versionnum = std::stoi(mdata.colKey);
									std::string checkpointfile_path = "checkpoints/checkpoints_server"+std::to_string(my_node_id)+"tablet"+std::to_string(tablet_no);
									write_versioned_checkpoint_file(tablet_no, checkpointfile_path, versionnum);
									std::string resp = "ACK\r\n";
								    write(mdata.comm_fd, resp.c_str(), (int)resp.size());
								    goto done;
								}

			}
//			fprintf(stderr, "WHY ARE WE NOT HERE\n");
//			fprintf(stderr, "METHOD %s\n", mdata.method.c_str());
//			fprintf(stderr, "Ready for BINARY DATA %d\n", mdata.ready_for_binary_data);
			fflush(stderr);
			if (mdata.method == "PUT" || mdata.method == "CPUT") {
				if (!mdata.ready_for_binary_data) {
					crlf_found = strstr(buffer.data(), "\r\n\r\n");
					bool check = crlf_found == NULL;
//					fprintf(stderr, "check %d\n", check);
				} else {
					crlf_found = NULL;
				}
			} else {
				crlf_found = strstr(buffer.data(), "\r\n\r\n");
			}
			bool check2 = crlf_found == NULL;
//			fprintf(stderr, "check %d\n", check2);
		}
	}
done:;

	if (close(comm_fd) < 0) {
		if (print_output == 1) {
			fprintf(stderr, "[%d] Error in closing connection \r\n", comm_fd);
		}
	}
//	fprintf(stderr,"JUST CLOSED 1\n");
	connections_arr[index] = 0;
	if (print_output == 1) {
		fprintf(stderr, "[%d] Connection closed \r\n ", comm_fd);
	}
	pthread_detach(pthreads[index]);
	pthread_exit(NULL);
}

void init_server(int port_no) {
	for (int i = 0; i < initialize.size(); i++) {
		suspend_tablet_checkpoint[i] = 1;
	}
	initialize_mutex_map(initialize.size());
	populateAllServers(config_file);
	//log mutex
	pthread_mutex_init(&mutex_for_log, NULL);

	initReplicationConfig(TOTAL_TABLETS, TOTAL_NODES, REPLICAS, TABLETS_PER_GROUP);

	signal(SIGINT, ctrlc_handler);
	listen_fd = socket(PF_INET, SOCK_STREAM, 0);
	int opt = 1;
	int ret = setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR|SO_REUSEPORT, &opt, sizeof(opt));
	if (ret < 0) {
		fprintf(stderr, "error with setting sockopt");
		exit(EXIT_FAILURE);
	}

	struct sockaddr_in servaddr;
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(binding_port_num_for_main);
//	fprintf(stderr, "portnum :%d\n", servaddr.sin_port);
//	fprintf(stderr, "binding_port_num_for_main :%d\n", binding_port_num_for_main);
	inet_pton(AF_INET, binding_address_for_main.c_str(), &(servaddr.sin_addr));
	if (bind(listen_fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
		fprintf(stderr, "error with binding server");
		exit(EXIT_FAILURE);
	}
	if (listen(listen_fd, SOMAXCONN) < 0) {
		fprintf(stderr, "error with listening");
		exit(EXIT_FAILURE);
	}
	sock_coordinator = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in c_addr;
	bzero(&c_addr, sizeof(c_addr));
	std::string coordinator_ip_address = "127.0.0.1";
	c_addr.sin_family = AF_INET;
	c_addr.sin_port = htons(port_no);
	inet_pton(AF_INET, coordinator_ip_address.c_str(), &(c_addr.sin_addr));
	connect(sock_coordinator, (struct sockaddr *)&c_addr, sizeof(c_addr));
	pthread_t heartbeat_thread;
	pthread_create(&heartbeat_thread, NULL, [](void*)->void* { periodic_heartbeats();return NULL;}, NULL);
	pthread_t checkpoint_thread;
	pthread_create(&checkpoint_thread, NULL, [](void*)->void* {periodic_checkpointing();return NULL;}, NULL);


	std::vector<char> binary_value = {'[', ']'};

	PUT(kvs, "1", "dir_root", binary_value, "application/octet-stream", binary_value.size());
	while (!shutting_down) {
		struct sockaddr_in clientaddr;
		socklen_t clientaddrlen = sizeof(clientaddr);
		int comm_fd = accept(listen_fd, (struct sockaddr*)&clientaddr, &clientaddrlen);
		if (comm_fd < 0) {
			if (shutting_down) {
				break;
			} else {
				break;
				continue;
			}
		}
		int spot_found = 0;
		int spot_in_arr = -1;
		for (int i = 0; i < 10000 && spot_found == 0; i++) {
			if (connections_arr[i] == 0) {
				spot_found = 1;
				if (shutting_down) {
					if (close(comm_fd) < 0) {
//						fprintf(stderr, "[%d] Error in closing connection \r\n", comm_fd);
					}
					spot_found = 0;
					break;
				}
				connections_arr[i] = comm_fd;
				spot_in_arr = i;
				break;
			}
		}
		if (!shutting_down) {
			if (spot_found == 0) {
				fprintf(stderr, "Currently at capacity with connections");
				fprintf(stderr, "[%d] Connection closed\r\n", comm_fd);
				if (close(comm_fd) < 0) {
					fprintf(stderr, "[%d] Error in closing connection \r\n", comm_fd);
				}
				continue;
			}
			pthread_create(&pthreads[spot_in_arr], NULL, clientFunc, &spot_in_arr);
		}
	}
}

int main(int argc, char *argv[])
{
	signal(SIGINT, ctrlc_handler);
	int port_no = 8000;
	int input_arg;
	while ((input_arg = getopt(argc, argv, "p:av")) != -1) {
		switch(input_arg) {
			case 'p': {
				port_no = atoi(optarg);
				break;
			}
			case 'a': {
				fprintf(stderr,"Author: Eesha Shekhar / eshekhar\n");
				exit(EXIT_SUCCESS);
				break;
			}
			case 'v': {
				print_output = 1;
				break;
			}
			default: {
				fprintf(stderr,"Incorrect command\n");
				return -1;
			}
		}
	}
	if (optind < argc) {
		config_file = argv[optind];
		optind++;
	} else {
		fprintf(stderr, "Not provided config file\n");
		return -1;
	}
	if (optind < argc) {
		server_num = atoi(argv[optind]);
	} else {
		fprintf(stderr, "Not provided index number\n");
		return -1;
	}

	init_server(port_no);
	return 0;
}