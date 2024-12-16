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
#include <algorithm>
#include <functional>
#include <fstream>
#include <ctime>
#include "KV.h" 
#include "../http/nlohmann/json.hpp"

#define HB_INTERVAL 2

struct sockaddr_in be_dest;
struct sockaddr_in fe_dest;
int be_sock;
int fe_sock;
typedef struct {
	time_t curr_time;
	bool success = true;
} statusandtime;
std::map<int, statusandtime> backend_servers;
std::vector<int> frontend_servers;
int num_of_servers;
//std::vector<std::string> allServers;
typedef struct {
	bool success = true;
	std::string server_string;
} serverStatus;
std::map<int, serverStatus> successful_list;
std::map<std::string, int> server_address_to_accept_fd;

std::string config_file;
std::string ok_http = "200 OK";
std::string bad_http = "500 Internal Server Error";


typedef struct {
    std::string method;
    std::string rowKey;
    std::string colKey;
    std::string httpHeader;
    int content_length;
} Method_data;

typedef struct {
    std::string server_address;
    bool failed = false;
} ServerInfo;

std::vector<ServerInfo> allServers;
std::map<std::string, int> allServersMap;

bool do_write_vector(int fd, const std::vector<char>& buf, int len) {
    int sent = 0;
    while (sent < len) {
        int n = write(fd, &buf[sent], len - sent);
        if (n<0) {
            return false;
        }
        sent += n;
    }
    return true;
}

std::string sendServerList() {
    std::vector<nlohmann::json> successfulServers;
    std::vector<nlohmann::json> crashedServers;

	for (const auto& server: successful_list) {
        std::string serverString = server.second.server_string; 
        std::string ip;
        int port = 0;
        std::stringstream ss(serverString);
        std::getline(ss, ip, ':'); 
        ss >> port;                
        nlohmann::json serverObject = {
            {"address", ip},
            {"port", port}
        };
		if (server.second.success) {
			// successful server
			successfulServers.push_back(serverObject);
		} else {
			//crashed server
			crashedServers.push_back(serverObject);
		}

	}
	nlohmann::json resp;
	resp["Active_servers"] = successfulServers;
	resp["Inactive_servers"] = crashedServers;
	std::string json_to_string = resp.dump();
	return json_to_string;
}

void ctrlc_handler(int arg) {
    fprintf(stderr, "closing the connection\n");
    close(be_sock);
    close(fe_sock);
    exit(0);
}

void getAllServers(const std::string& filepath) {
    std::ifstream reader(filepath, std::ios::in);
    if (!reader.is_open()) {
        fprintf(stderr, "Error opening file\n");
        exit(EXIT_FAILURE);
    }
    std::string s_line;
    while (std::getline(reader, s_line)) {

        ServerInfo si;
        si.server_address = s_line;
        allServers.push_back(si);
        allServersMap[s_line] = num_of_servers;
		num_of_servers++;
    }
    reader.close();
}

std::vector<char> formatResponse(Method_data mdata, const std::string &addr) {
    std::string f_string = mdata.httpHeader+ " " +ok_http + "\r\n";
    std::string address_string = "Backend-Address: " +addr+ "\r\n";
    std::string s_string = "Content-Length: "  +std::to_string(0) + "\r\n\r\n";
    std::string to_return = f_string+ address_string + s_string;
    std::vector<char> vectorToReturn;
    vectorToReturn.insert(vectorToReturn.end(), to_return.begin(), to_return.end());
//    fprintf(stderr,"vectorToReturn: %s\n",vectorToReturn.data());
    return vectorToReturn;
}

std::vector<char> formatResponseNewPrimary(int tablet_id, int node_id) {
    std::string f_string =  "NEWPRIMARY /"+ std::to_string(tablet_id)+ +"/"+ std::to_string(node_id)+ " HTTP/1.1\r\n";


    std::string to_return;

        std::string s_string = "Content-Length: "  +std::to_string(0) + "\r\n\r\n";
        to_return = f_string +s_string;
        fprintf(stderr, "sending string:%s\n", to_return.c_str());
        std::vector<char> ret(to_return.begin(), to_return.end());


    return ret;
}

std::vector<char> formatResponseDONE() {
    std::string f_string =  "DONE /test/test HTTP/1.1\r\n";


    std::string to_return;

        std::string s_string = "Content-Length: "  +std::to_string(0) + "\r\n\r\n";
        to_return = f_string +s_string;
        fprintf(stderr, "sending string:%s\n", to_return.c_str());
        std::vector<char> ret(to_return.begin(), to_return.end());


    return ret;
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


std::string return_server_address(const std::string &rowkey) {
    int tablet_id = getTabletForRowKey(rowkey);
    std::vector<int> replica_nodes = getReplicaNodesForTablet(tablet_id);
    if (replica_nodes.empty()) {
        fprintf(stderr, "No replicas found for tablet %d\n", tablet_id);
        return "127.0.0.1:8004";
    }
    fprintf(stderr, "num of replica nodes for this tablet: %ld\n",replica_nodes.size());
    int random_idx = rand() % replica_nodes.size();
    int chosen_node = replica_nodes[random_idx];
    fprintf(stderr, "chosen node index: %d\n", chosen_node);
    fprintf(stderr, "chosen node: %s\n", allServers[chosen_node].server_address.c_str());
    return allServers[chosen_node].server_address;
}

void handleNodeFailure(int failed_node) {
    fprintf(stderr, "Handling node failure for node: %d\n", failed_node);

    // Remove the failed node from all tablet replicas
    removeNodeFromTabletReplicas(failed_node);

    std::vector<int> affected_tablets = getAllTabletsForNode(failed_node);

    // re-elect a new primary (if needed)

    for (int tablet_id : affected_tablets) {
    	bool primary_set = false;
        int current_primary = getPrimaryNodeForTablet(tablet_id);

        if (current_primary == failed_node) {
            // Promote next available replica as primary
            bool promoted = promoteNextReplicaAsPrimary(tablet_id);
            if (!promoted) {
            	fprintf(stderr, "trying to find a live server\n");
                // If no replica could be promoted, pick a live server
                std::vector<int> live_servers;
                for (auto a_server = successful_list.begin(); a_server != successful_list.end();) {
                	if (a_server->second.success) {
                		//means that this is a live server
                		fprintf(stderr, "found a live server %s\n", a_server->second.server_string.c_str());
                		fprintf(stderr, "fd %d\n", a_server->first);
                		live_servers.push_back(allServersMap[a_server->second.server_string]);
                	}
                	++a_server;
                }
                fprintf(stderr, "for loop done\n");
//                for (int i = 0; i < (int)allServers.size(); i++) {
//                    if (i == failed_node) continue; // failed node
//                    // Check if server is still live
//                    bool found = false;
//                    for (auto &kv : backend_servers) {
//                        if (kv.first == i) { found = true; break; }
//                    }
//                    if (found) {
//                        live_servers.push_back(i);
//                    }
//                }

                if (!live_servers.empty()) {
                    int new_primary = live_servers[0];
                    setPrimaryNodeForTablet(tablet_id, new_primary);
                    primary_set = true;
                    fprintf(stderr, "Redistributed tablet %d to new primary %d\n", tablet_id, new_primary);
                } else {
                    fprintf(stderr, "No available servers to redistribute tablet %d. Data unavailable.\n", tablet_id);
                }
            } else {
            	primary_set = true;
            	fprintf(stderr, "primary for tablet: %d", tablet_id);
            	fprintf(stderr, "is node: %d\n", getPrimaryNodeForTablet(tablet_id));
                fprintf(stderr, "Promoted a replica for tablet %d after node %d failed.\n", tablet_id, failed_node);
            }
            if (primary_set) {
            	//this means we need to tell all the other nodes that this primary was set
//            	nlohmann::json resp;
            	int newprim = getPrimaryNodeForTablet(tablet_id);
            	std::vector<char> resp = formatResponseNewPrimary(tablet_id, newprim);
            	//need to send to all the backend servers
            	for (auto be_server = backend_servers.begin(); be_server != backend_servers.end();) {
            		if (be_server->second.success) {
            			//active server
            			std::string serverString = successful_list[be_server->first].server_string;
            			std::string ip;
            			        int port = 0;
            			        std::stringstream ss(serverString);
            			        std::getline(ss, ip, ':');
            			        ss >> port;
            			int sockfd = socket(AF_INET, SOCK_STREAM, 0);
            			struct sockaddr_in server_dest;
            					bzero(&server_dest, sizeof(server_dest));
            					server_dest.sin_family = AF_INET;
            					server_dest.sin_port = htons(port);
            					inet_pton(AF_INET, ip.c_str(), &(server_dest.sin_addr));

            			if (connect(sockfd, (struct sockaddr*)&server_dest, sizeof(server_dest)) < 0) {

            			}
            			do_write_vector(sockfd, resp, (int)resp.size());
            			fprintf(stderr, "sent to %s\n", successful_list[be_server->first].server_string.c_str());
            			char buf[2000];
            				int n = read(sockfd, buf, sizeof(buf));
            				if (n <= 0) {
            					fprintf(stderr, "are in this?? \n");
            					++be_server;
            					continue;
            				}
            				std::string response(buf, n);

            				if (response.find("ACK") != std::string::npos) {
            					fprintf(stderr, "received ACK!");

            				}

            		}
            		++be_server;
            	}


            } else {
            	//this means primary was not set
            	setPrimaryNodeForTabletNull(tablet_id);
            }
        }
    }
}

void handleNodeRestart(int restarted_node, std::string serverString) {
	fprintf(stderr, "Handling node restart for node: %d\n", restarted_node);

	    addNodeBackToTabletReplicas(restarted_node);
	    std::vector<int> affected_tablets = getAllTabletsForNode(restarted_node);

	        // re-elect a new primary (if needed)

	        for (int tablet_id : affected_tablets) {



	                	//this means we need to tell all the other nodes that this primary was set
	    //            	nlohmann::json resp;
	                	int newprim = getPrimaryNodeForTablet(tablet_id);
	                	if (newprim == -1) {
	                		//need to set a new primary
	                		fprintf(stderr, "we need to set a new primary for tablet %d\n",tablet_id);
	                		bool primary_set = false;
	                		            // Promote next available replica as primary
	                		            bool promoted = promoteNextReplicaAsPrimary(tablet_id);
	                		            if (!promoted) {
	                		            	fprintf(stderr, "trying to find a live server\n");
	                		                // If no replica could be promoted, pick a live server
	                		                std::vector<int> live_servers;
	                		                for (auto a_server = successful_list.begin(); a_server != successful_list.end();) {
	                		                	if (a_server->second.success) {
	                		                		//means that this is a live server
	                		                		live_servers.push_back(allServersMap[a_server->second.server_string]);
	                		                	}
	                		                	++a_server;
	                		                }
	                		                fprintf(stderr, "for loop done\n");

	                		                if (!live_servers.empty()) {
	                		                    int new_primary = live_servers[0];
	                		                    setPrimaryNodeForTablet(tablet_id, new_primary);
	                		                    primary_set = true;
	                		                    fprintf(stderr, "Redistributed tablet %d to new primary %d\n", tablet_id, new_primary);
	                		                } else {
	                		                    fprintf(stderr, "No available servers to redistribute tablet %d. Data unavailable.\n", tablet_id);
	                		                }
	                		            } else {
	                		            	primary_set = true;
	                		            	fprintf(stderr, "primary for tablet: %d", tablet_id);
	                		            	fprintf(stderr, "is node: %d\n", getPrimaryNodeForTablet(tablet_id));
//	                		                fprintf(stderr, "Promoted a replica for tablet %d after node %d failed.\n", tablet_id, failed_node);
	                		            }
	                		            if (primary_set) {
	                		            	//this means we need to tell all the other nodes that this primary was set
	                		//            	nlohmann::json resp;
	                		            	int newprim = getPrimaryNodeForTablet(tablet_id);
	                		            	std::vector<char> resp = formatResponseNewPrimary(tablet_id, newprim);
	                		            	//need to send to all the backend servers
	                		            	for (auto be_server = backend_servers.begin(); be_server != backend_servers.end();) {
	                		            		if (be_server->second.success) {
	                		            			//active server
	                		            			std::string serverString = successful_list[be_server->first].server_string;
	                		            			std::string ip;
	                		            			        int port = 0;
	                		            			        std::stringstream ss(serverString);
	                		            			        std::getline(ss, ip, ':');
	                		            			        ss >> port;
	                		            			int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	                		            			struct sockaddr_in server_dest;
	                		            					bzero(&server_dest, sizeof(server_dest));
	                		            					server_dest.sin_family = AF_INET;
	                		            					server_dest.sin_port = htons(port);
	                		            					inet_pton(AF_INET, ip.c_str(), &(server_dest.sin_addr));

	                		            			if (connect(sockfd, (struct sockaddr*)&server_dest, sizeof(server_dest)) < 0) {

	                		            			}
	                		            			do_write_vector(sockfd, resp, (int)resp.size());
	                		            			fprintf(stderr, "sent to %s\n", successful_list[be_server->first].server_string.c_str());
	                		            			char buf[2000];
	                		            				int n = read(sockfd, buf, sizeof(buf));
	                		            				if (n <= 0) {
	                		            					fprintf(stderr, "are in this?? \n");
	                		            				}
	                		            				std::string response(buf, n);

	                		            				if (response.find("ACK") != std::string::npos) {
	                		            					fprintf(stderr, "received ACK!");

	                		            				}

	                		            		}
	                		            		++be_server;
	                		            	}


	                		            } else {
	                		            	//this means primary was not set

	                		            	setPrimaryNodeForTabletNull(tablet_id);
	                		            }


	                	} else  {
	                		newprim = getPrimaryNodeForTablet(tablet_id);
	                			                	std::vector<char> resp = formatResponseNewPrimary(tablet_id, newprim);
	                			                	//need to send to all the backend servers
	                			                			//active server

	                			                			std::string ip;
	                			                			        int port = 0;
	                			                			        std::stringstream ss(serverString);
	                			                			        std::getline(ss, ip, ':');
	                			                			        ss >> port;
	                			                			int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	                			                			struct sockaddr_in server_dest;
	                			                					bzero(&server_dest, sizeof(server_dest));
	                			                					server_dest.sin_family = AF_INET;
	                			                					server_dest.sin_port = htons(port);
	                			                					inet_pton(AF_INET, ip.c_str(), &(server_dest.sin_addr));

	                			                			if (connect(sockfd, (struct sockaddr*)&server_dest, sizeof(server_dest)) < 0) {
	                			                				fprintf(stderr, "couldn't connect to %s\n", serverString.c_str());
	                			                			}
	                			                			do_write_vector(sockfd, resp, (int)resp.size());
	                			                			fprintf(stderr, "sent to %s\n", serverString.c_str());
	                			                			char buf[2000];
	                			                				int n = read(sockfd, buf, sizeof(buf));
	                			                				if (n <= 0) {
	                			                					fprintf(stderr, "are in this?? \n");
	                			                				}
	                			                				std::string response(buf, n);

	                			                				if (response.find("ACK") != std::string::npos) {
	                			                					fprintf(stderr, "received ACK!");

	                			                				}
	                	}




	                }
	        std::vector<char> resp = formatResponseDONE();
	        std::string ip;
			int port = 0;
			std::stringstream ss(serverString);
			std::getline(ss, ip, ':');
			ss >> port;
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in server_dest;
			bzero(&server_dest, sizeof(server_dest));
			server_dest.sin_family = AF_INET;
			server_dest.sin_port = htons(port);
			inet_pton(AF_INET, ip.c_str(), &(server_dest.sin_addr));

	if (connect(sockfd, (struct sockaddr*)&server_dest, sizeof(server_dest)) < 0) {

	}
	do_write_vector(sockfd, resp, (int)resp.size());
	fprintf(stderr, "sent to %s\n", serverString.c_str());
//	char buf[2000];
//		int n = read(sockfd, buf, sizeof(buf));
//		if (n <= 0) {
//			fprintf(stderr, "are in this?? \n");
//		}
//		std::string response(buf, n);

//		if (response.find("ACK") != std::string::npos) {
//			fprintf(stderr, "received ACK!");
//
//		}



}
int check_in_successful_list(std::string namestr) {
	for (const auto& server: successful_list) {
	        std::string serverString = server.second.server_string;
	        if (serverString == namestr) {
	        	//duplicate past server_fd
	        	return server.first;
	        }
	}
	return -1;

}
void newPrimaryAppointment() {
	//need to ask all the other nodes for their log files

}

bool isNodeAlive(std::string server_add) {
	fprintf(stderr, "server:address %s\n", server_add.c_str());
	int acc = server_address_to_accept_fd[server_add];
	fprintf(stderr,"CHECKING FOR %s\n", successful_list[acc].server_string.c_str());
	return successful_list[acc].success;
}

void periodic_checking_heartbeat() {
    time_t curr_time = time(nullptr);
    for (auto be_server = backend_servers.begin(); be_server != backend_servers.end();) {
        if (curr_time - be_server->second.curr_time > HB_INTERVAL) {
//            fprintf(stderr, "backend server crash detected by timeout.");
            int failed_node = be_server->first;
            if (successful_list[failed_node].success) {
            	fprintf(stderr, "backend server crash detected by timeout.");
            	successful_list[failed_node].success = false;
//            	            fprintf(stderr, "SET FALSE FOR %s\n",  successful_list[failed_node].server_string.c_str());
            	            be_server->second.success=false;
            	//            be_server = backend_servers.erase(be_server);
            	            // Handle node failure
            	            int node_id = allServersMap[successful_list[failed_node].server_string];
            	            handleNodeFailure(node_id);
            }

            ++be_server;
        } else {
            ++be_server;
        }
    }
}

void heartbeat_detection(int sock_fd) {


}

void frontend_req(int sock_fd) {
    fprintf(stderr, "are we here \n");
    Method_data mdata;
    std::vector<char> buf_fe(1000);

    int rlen = recv(sock_fd, buf_fe.data(), (int)buf_fe.size(), 0);
    if (rlen <= 0) {
        fprintf(stderr, "rlen < 0");
        fprintf(stderr, "client has disconnected");
        auto find_idx = std::find(frontend_servers.begin(), frontend_servers.end(), sock_fd);
        if (find_idx != frontend_servers.end()) {
            frontend_servers.erase(find_idx);
        }
    } else {
//        fprintf(stderr, "%s\n", buf_fe.data());
        buf_fe.resize(rlen);

        std::string getbuffer(buf_fe.data(), rlen);
        fprintf(stderr,"FE REQUEST:\n");
        fprintf(stderr,"checking: %s\n", getbuffer.substr(0,30).c_str());
        size_t find_eol = getbuffer.find("\r\n");
        fprintf(stderr, "are we here 3\n");
        if (find_eol != std::string::npos) {
            std::string f_line = getbuffer.substr(0, find_eol);
            std::string func_name;
            std::string rowpluscolkey;
            std::string httpheader;
            std::istringstream iss(f_line);
            iss >> func_name >> rowpluscolkey >> httpheader;
//            fprintf(stderr, "the method:%s\n", func_name.c_str());
//            fprintf(stderr, "the rowpluscol:%s\n", rowpluscolkey.c_str());
//            fprintf(stderr, "the httpheader:%s\n", httpheader.c_str());
            size_t getslash = rowpluscolkey.find('/');
            size_t getslash_2 = rowpluscolkey.find('/', getslash + 1);
            size_t second_index = getslash_2 - getslash - 1;
            std::string rowKey = rowpluscolkey.substr(getslash + 1, second_index);
            std::string colKey = rowpluscolkey.substr(getslash_2 + 1);
//            fprintf(stderr, "the row key:%s\n", rowKey.c_str());
//            fprintf(stderr, "the col key:%s\n", colKey.c_str());
            mdata.method = func_name;
            mdata.rowKey = rowKey;
            mdata.colKey = colKey;
            mdata.httpHeader = httpheader;
        }
//        fprintf(stderr, "row key decided%s\n", mdata.rowKey.c_str());
//        fprintf(stderr, "col key decided%s\n", mdata.colKey.c_str());
//        fprintf(stderr, "method decided%s\n", mdata.method.c_str());
//        fprintf(stderr, "HTTP decided%s\n", mdata.httpHeader.c_str());
        if (mdata.rowKey=="get_servers") {
            if (mdata.method == "GET") {
                fprintf(stderr, "reached the right place\n");
                std::string lst_string = sendServerList();
                std::vector<char> resp = formatResponseList(mdata, lst_string);
                do_write_vector(sock_fd, resp, (int)resp.size());
            } else if (mdata.method == "OPTIONS") {
                std::vector<char> resp = formatResponseList(mdata, "");
                do_write_vector(sock_fd, resp, (int)resp.size()); 
            }
        }
        else if (mdata.method == "GET"|| mdata.method == "PUT" || mdata.method == "CPUT" || mdata.method == "DELETE") {

        	std::string addressAndPort = return_server_address(mdata.rowKey);
        	fprintf(stderr, "sending this port %s\n", addressAndPort.c_str());
        	        std::vector<char> resp = formatResponse(mdata, addressAndPort);
        	        do_write_vector(sock_fd, resp, (int)resp.size());
        }

    }
}

int main(int argc, char *argv[])
{
    signal(SIGINT, ctrlc_handler);
    if (argc != 4) {
        fprintf(stderr, "*** Author: Eesha Shekhar (eshekhar)\n");
        exit(1);
    }
    config_file = argv[1];
    char *port_num_be = argv[2];
    char *port_num_fe = argv[3];

    if (port_num_be == NULL) {
        fprintf(stderr, "Port number BE not found \n");
    }
    if (port_num_fe == NULL) {
        fprintf(stderr, "Port number FE not found \n");
    }
    int port_no_be = atoi(port_num_be);
    if (port_no_be <= 0) {
        fprintf(stderr, "Incorrect port number \n");
    }
    int port_no_fe = atoi(port_num_fe);
    if (port_no_fe <= 0) {
        fprintf(stderr, "Incorrect port number \n");
    }
    getAllServers(config_file);
    fprintf(stderr, "number of servers is: %d\n", num_of_servers);

    // Initialize replication config
    initReplicationConfig(15, 3, 3, 5);

    srand(time(NULL));

    be_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (be_sock < 0) {
        fprintf(stderr, "Cannot open socket (%s)\n", strerror(errno));
        exit(1);
    }
    int opt = 1;
        	int ret = setsockopt(be_sock, SOL_SOCKET, SO_REUSEADDR|SO_REUSEPORT, &opt, sizeof(opt));
        	if (ret < 0) {
        		fprintf(stderr, "error with setting sockopt");
        				exit(EXIT_FAILURE);
        	}

    fe_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (fe_sock < 0) {
        fprintf(stderr, "Cannot open socket (%s)\n", strerror(errno));
        exit(1);
    }
    int opt2 = 1;
            	        	int ret2 = setsockopt(fe_sock, SOL_SOCKET, SO_REUSEADDR|SO_REUSEPORT, &opt2, sizeof(opt2));
            	        	if (ret2 < 0) {
            	        		fprintf(stderr, "error with setting sockopt");
            	        				exit(EXIT_FAILURE);
            	        	}

    bzero(&be_dest, sizeof(be_dest));
    be_dest.sin_family = AF_INET;
    be_dest.sin_addr.s_addr = INADDR_ANY;
    be_dest.sin_port = htons(port_no_be);
    if (bind(be_sock, (struct sockaddr *)&be_dest, sizeof(be_dest))<0) {
        fprintf(stderr, "Cannot bind socket\n");
    }

    bzero(&fe_dest, sizeof(fe_dest));
    fe_dest.sin_family = AF_INET;
    fe_dest.sin_addr.s_addr = INADDR_ANY;
    fe_dest.sin_port = htons(port_no_fe);
    if (bind(fe_sock, (struct sockaddr *)&fe_dest, sizeof(fe_dest))<0) {
        fprintf(stderr, "Cannot bind socket\n");
    }
    if (listen(be_sock,SOMAXCONN) <0) {
        fprintf(stderr, "Cannot listen socket\n");
    }
    if (listen(fe_sock,SOMAXCONN) <0) {
        fprintf(stderr, "Cannot listen socket\n");
    }

    fd_set r_set;
    while (true) {
//        fprintf(stderr,"in the loop\n");
        FD_ZERO(&r_set);
        FD_SET(be_sock, &r_set);
        FD_SET(fe_sock, &r_set);
        int curr_max = (be_sock > fe_sock)? be_sock: fe_sock;

        for (auto be_server = backend_servers.begin(); be_server != backend_servers.end(); be_server++) {
            FD_SET(be_server->first, &r_set);
            if (curr_max <  be_server->first) {
                curr_max = be_server->first;
            }
        }
        for (int i = 0; i < (int)frontend_servers.size(); i++) {
            FD_SET(frontend_servers[i], &r_set);
            if (curr_max <  frontend_servers[i]) {
                curr_max = frontend_servers[i];
            }
        }
        int max_fd = curr_max+1;
        struct timeval tm = {5,0};
        int slct = select(max_fd, &r_set, NULL, NULL, &tm);
        if (slct < 0) {
            fprintf(stderr, "Error in calling select()\n");
        }

        if (FD_ISSET(be_sock, &r_set)) {
            struct sockaddr_in clientaddr;
            socklen_t clientaddrlen = sizeof(clientaddr);
            int accept_be = accept(be_sock, (struct sockaddr*)&clientaddr, &clientaddrlen);
            fprintf(stderr, "accepted a new one on the backend\n");
//            if (backend_servers.find(accept_be)) {
//            	if ()
//            	//means that it is active again
//            }
            statusandtime st;
            st.success = true;
            st.curr_time = time(nullptr);

            backend_servers[accept_be] = st;
        }
        if (FD_ISSET(fe_sock, &r_set)) {
            struct sockaddr_in clientaddr;
            socklen_t clientaddrlen = sizeof(clientaddr);
            int accept_fe = accept(fe_sock, (struct sockaddr*)&clientaddr, &clientaddrlen);
            frontend_servers.push_back(accept_fe);
        }

        for (auto be_server = backend_servers.begin(); be_server != backend_servers.end();) {
            if (FD_ISSET(be_server->first, &r_set)) {
                char buf_be[1000];
                int rlen = recv(be_server->first, buf_be, sizeof(buf_be)-1, 0);
                if (rlen > 0) {
                    buf_be[rlen] = 0;
                    std::string get_buffer(buf_be, rlen);
                    fprintf(stderr, "Heartbeat detected from %s\n", get_buffer.c_str());
                    size_t get_end;


                    while ((get_end = get_buffer.find("\r\n")) != std::string::npos) {

                    	fprintf(stderr, "entered loop\n");
                    	fprintf(stderr, "get_end %ld\n", get_end);

                    	backend_servers[be_server->first].curr_time = time(nullptr);
                    	                    backend_servers[be_server->first].success = true;
                    	                    std::string response = get_buffer.substr(0, get_end);

                    	                    fprintf(stderr, "RECEIVED THIS STRING: %s\n", response.c_str());
                    	                    size_t pos = response.find("ALIVE");
                    	                    if (pos != std::string::npos) {
                    	                    	fprintf(stderr, "received question\n");
                    	                    	std::string server_add = response.substr(pos+6);


                    	                    		if (isNodeAlive(server_add)) {
                    	                    		                    		//sending YES
                    	                    		                    		fprintf(stderr, "sending YES\n");
                    	                    		                    		std::string str_to_send = "YES";
                    	                    		                    		send(be_server->first,str_to_send.c_str(), (int)str_to_send.size(), 0);
                    	                    		                    	} else {
                    	                    		                    		//sending NO
                    	                    		                    		std::string str_to_send = "NO";
                    	                    		                    		send(be_server->first,str_to_send.c_str(), (int)str_to_send.size(), 0);
                    	                    		                    		fprintf(stderr, "sending NO\n");
                    	                    		                    	}



                    	                    } else if (response.find("SHUTDOWN") != std::string::npos) {
                    	                    	//need to remove this server from the list
                    	                    	int failed_node = be_server->first;
                    	                    	fprintf(stderr, "failed node string %s\n", successful_list[failed_node].server_string.c_str());
                    	                    	fprintf(stderr, " node ID %d\n", allServersMap[successful_list[failed_node].server_string]);
                    	                    	fprintf(stderr, "fd %d\n", failed_node);
                    	                    	if (successful_list[failed_node].success) {
                    	                    	                    	successful_list[failed_node].success = false;
                    	                    	                    	                    fprintf(stderr, "SET FALSE FOR %s\n",  successful_list[failed_node].server_string.c_str());
                    	                    	                    	                    be_server = backend_servers.erase(be_server);
                    	                    	                    	                    // Handle node failure
                    	                    	                    	                    fprintf(stderr, "IN HERE\n");
                    	                    	                    	                    int node_id = allServersMap[successful_list[failed_node].server_string];
                    	                    	                    	                    handleNodeFailure(node_id);
                    	                    	                    	                    fprintf(stderr, "after handling node failure");

                    	                    	                    }

                    	                    }
                    	                    else {
                    	                    	//heartbeat string

                    							//let us first check if it is a failed server
                    	//						if (!backend_servers[be_server->first].success) {
                    	//							backend_servers[be_server->first].curr_time = time(nullptr);
                    	//							//node has restarted
                    	//						}
                    								//check if this was a failed node
                    								if (successful_list.find(be_server->first) != successful_list.end()) {
                    									if (successful_list[be_server->first].success == false) {
                    										int node_id = allServersMap[successful_list[be_server->first].server_string];
                    										fprintf(stderr, "running the handleNodeRestart function\n");
                    										successful_list[be_server->first].success = true;
                    										handleNodeRestart(node_id, successful_list[be_server->first].server_string);
                    										//checking
                    										std::vector<int> replicas = getReplicaNodesForTablet(14);
                    										fprintf(stderr, "replica for tablet 14\n");
                    										for (int i = 0; i < replicas.size(); i++) {
                    											fprintf(stderr, "node id: %d\n", replicas[i]);

                    										}
                    									}
                    								}


                    							serverStatus ss;
                    							ss.success = true;
                    							std::string namestr = response;
                    	//						fprintf(stderr, "namestr: %s\n", namestr.c_str());
                    							ss.server_string = namestr;
                    							//here we need to check if this was a crashed node
                    	//						fprintf(stderr, "namestr: %s\n", namestr.c_str());
                    	//						fprintf(stderr, "be_server->first: %d\n", be_server->first);
                    							if (server_address_to_accept_fd.find(namestr) != server_address_to_accept_fd.end()) {
                    								//print this value out
                    	//							fprintf(stderr, "server_address_to_accept_fdt: %d\n", server_address_to_accept_fd[namestr]);
                    							}
                    							server_address_to_accept_fd[namestr] = be_server->first;
                    							if (successful_list.find(be_server->first) == successful_list.end()) {
                    								//either a new node or node restarted from ctrl C
                    								int fd = check_in_successful_list(namestr);
                    								if (fd != -1) {
                    									fprintf(stderr, "node %s restarted from ctrl C\n", namestr.c_str());
                    									successful_list.erase(fd);
                    									successful_list[be_server->first] = ss;
                    									int node_id = allServersMap[namestr];
                    									handleNodeRestart(node_id, namestr);

                    								} else {
                    									//new node
                    									successful_list[be_server->first] = ss;
                    																int node_id = allServersMap[namestr];
                    																handleNodeRestart(node_id, namestr);

                    								}

                    							}




                    	                    }
                    	                    get_buffer.erase(0, get_end + 2);

                    }
                    ++be_server;

                } else {
                    // Backend server disconnected
                    int failed_node = be_server->first;
                    if (successful_list[failed_node].success) {
                    	successful_list[failed_node].success = false;
                    	                    fprintf(stderr, "SET FALSE FOR %s\n",  successful_list[failed_node].server_string.c_str());
                    	//                    be_server = backend_servers.erase(be_server);
                    	                    // Handle node failure
                    	                    fprintf(stderr, "IN HERE\n");
                    	                    int node_id = allServersMap[successful_list[failed_node].server_string];
                    	                    handleNodeFailure(node_id);
                    }

                    ++be_server;

                }
            } else {
                ++be_server;
            }
        }
        for (int i = 0; i < (int)frontend_servers.size(); i++) {
            if (FD_ISSET(frontend_servers[i], &r_set)) {
                frontend_req(frontend_servers[i]);
            }
        }

//        fprintf(stderr, "max fd %d", max_fd);
//        fprintf(stderr, "be_sock %d\n", be_sock);
//        fprintf(stderr, "fe_sock %d\n", fe_sock);
        periodic_checking_heartbeat();
    }

    fprintf(stderr, "closing the connection\n");
    close(be_sock);
    close(fe_sock);

    return 0;
}