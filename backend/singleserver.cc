#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <string>
#include<pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <regex>
#include <fstream>
#include <dirent.h>
#include <sys/file.h>
#include "KV.h"
//#define SEPARATOR "123"
#define SEPARATOR "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF"
/*
<METHOD>/row_key/column_key <HTTP_VERSION>\r\n

<HEADER_KEY>: <HEADER_VALUE>\r\n

 ...

\r\n <BODY_CONTENT>
 * */
/*curr_state
 * 0 -> HELO/first command
 * 1 -> MAIL FROM received
 * 2 -> RCPT TO received
 * 3 -> currently in transaction state
 * */
typedef struct {
	bool HELO_received = false;
	int curr_state = 0;
	std::string HELO_domain;
	std::string mail_from;
	std::vector<std::string> rcpt_email_address_arr;
	std::vector<std::string> rcpt_mailbox_path_arr;
	std::string email_data;
	bool first_time_writing_to_email = true;
} state;
typedef struct {
	std::string method;
    std::string rowKey;
    std::string colKey;
    std::string httpHeader;
    int content_length;
    std::string content_type;
    std::vector<char> binary_data;
    bool ready_for_binary_data = false;
    bool finished_processing_bin_data = false;
} Method_data;
std::map <std::string, pthread_mutex_t> mutexes_map;
std::map <std::string, std::ofstream> ofstream_mailbox_path_map;
volatile bool shutting_down = false;
volatile int connections_arr[100] = {0};
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
pthread_t pthreads[100];
int listen_fd;
std::string mbox;
int retries = 5;
std::string ok_http = "200 OK";
std::string bad_http = "500 Internal Server Error";

std::vector<char> formatResponse(Status status, Method_data mdata) {
	if (mdata.method == "GET") {
		std::string f_string;
		if (status.success) {
			 f_string = mdata.httpHeader+ " " +ok_http + "\r\n";
		} else {
			 f_string = mdata.httpHeader+ " " +bad_http + "\r\n";
		}
		int len = status.binary_data.size();
		std::string d_type = status.data_type;
		std::string s_string = "Content-Length: "  +std::to_string(len) + "\r\n";
		std::string t_string = "Content-Type: "  +d_type + "\r\n" + "\r\n";
		std::string to_return = f_string+ s_string + t_string;
		std::vector<char> vectorToReturn;
		vectorToReturn.insert(vectorToReturn.end(), to_return.begin(), to_return.end());
		vectorToReturn.insert(vectorToReturn.end(), status.binary_data.data(), status.binary_data.data() + status.binary_data.size());
		fprintf(stderr,"vectorToReturn: %s\n",vectorToReturn.data());
		return vectorToReturn;

	} else {
		std::string f_string;
		if (status.success) {
			 f_string = mdata.httpHeader+ " " +ok_http + "\r\n";
		} else {
			 f_string = mdata.httpHeader+ " " +bad_http + "\r\n";
		}
		int len = status.binary_data.size();
		std::string d_type = status.data_type;
		std::string s_string = "Content-Length: "  +std::to_string(len) + "\r\n" +"\r\n";
		std::string to_return = f_string+ s_string;
		std::vector<char> vectorToReturn;
		vectorToReturn.insert(vectorToReturn.end(), to_return.begin(), to_return.end());
		vectorToReturn.insert(vectorToReturn.end(), status.binary_data.data(), status.binary_data.data() + status.binary_data.size());
		fprintf(stderr,"vectorToReturn: %s\n", vectorToReturn.data());
		return vectorToReturn;
	}


}
bool do_write(int fd, const char *buf, int len) {
	int sent = 0;
	while (sent < len) {
		int n = write(fd, &buf[sent],len-sent);
		if (n<0) {
			//client shutdown
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
			//client shutdown
			return false;
		}

		sent += n;
	}
	return true;
}

void initialize_mutex_map() {
	DIR* directory;
	struct dirent* file;
	directory = opendir(mbox.c_str());
	if (directory != NULL) {
		file = readdir(directory);
		while (file != NULL) {
			num_of_mailboxes++;
			std::string file_name = file->d_name;
			pthread_mutex_t mutex_for_mailbox;
			pthread_mutex_init(&mutex_for_mailbox, NULL);
			std::string file_path = mbox + "/" + file_name;
			mutexes_map[file_path] = mutex_for_mailbox;
			file = readdir(directory);
		}
		closedir(directory);

	}

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
// when CTRL + C is pressed
void ctrlc_handler(int arg) {
	shutting_down = true;
	for (int i = 0; i < 100 ; i++) {
		//closing all the client worker threads
		if (connections_arr[i] != 0) {
			//making the connection non blocking
			non_blocking_connection(connections_arr[i]);
			//writing the shutdown message
			if (do_write(connections_arr[i], shutting_down_msg, strlen(shutting_down_msg)) == false) {
				fprintf(stderr, "Error in writing server shut down message\n");
			}
			//closing the connection fd
			if (close(connections_arr[i]) < 0) {
				fprintf(stderr, "Error in closing connection\n");
				exit(EXIT_FAILURE);
			}
			fprintf(stderr, "[%d] Connection closed \r\n ", connections_arr[i]);
			connections_arr[i] = 0;

		}
	}
//	for (auto &entry: ofstream_mailbox_path_map) {
//		if (entry.second.is_open()) {
//			entry.second.close();
//		}
//	}
//	for (int file_desc : fds) {
//
//		if (fcntl(file_desc, F_GETFD) > -1) {//file is open
//			close(file_desc);
//		}
//	}
	if (close(listen_fd) < 0) {
		fprintf(stderr, "ran into issue with closing listener socket");
		exit(EXIT_FAILURE);
	}
	exit(0);
}

bool do_read(int fd, char *buf, int len) {
	int rcvd = 0;
	while (rcvd < len) {
		int n = read(fd, &buf[rcvd], len-rcvd);
		if (n<0) {
			//client shutdown
			return false;
		}

		rcvd += n;
	}
	return true;
}
bool is_valid_email(const std::string& email_address) {
	std::regex to_match("^<[a-zA-Z0-9.]+@[a-zA-Z0-9.]+>$");
	return std::regex_match(email_address, to_match);
}

void *clientFunc(void *arg) {
	//client descriptor index
	int index = *(int*) arg;
	Status status;
	size_t lensep = std::strlen(SEPARATOR);
	std::vector<char> separator_vector(SEPARATOR,SEPARATOR+ lensep);
	std::vector<char> buffer(2000);
	int rcvd = 0;
	int comm_fd = connections_arr[index];
	//New connection debug output
	if (print_output == 1) {
		fprintf(stderr, "[%d] New connection\r\n", comm_fd);
	}
	//server ready comment

	if (print_output == 1) {
								fprintf(stderr, "[%d] ", comm_fd);
								fprintf(stderr, "S: %s\r\n", first_msg);
							}
	bool waiting_for_content;
	bool received_content;
	bool content_size;
	Method_data mdata;
	while (shutting_down == false) {
		//resizing
		if (rcvd + 1000 > buffer.size()) {
			buffer.resize((rcvd + 1000)*2);
		}
		int n = read(comm_fd, buffer.data() + rcvd, 1000);
		if (n <= 0) {
			//close files

			//client shutdown
			if (close(comm_fd) < 0) {
				fprintf(stderr, "[%d] Error in closing connection \r\n", comm_fd);
			}
			connections_arr[index] = 0;
			if (print_output == 1) {
				fprintf(stderr, "[%d] Connection closed \r\n ", comm_fd);
			}
			pthread_detach(pthreads[index]);
			//exit the thread
			pthread_exit(NULL);

		}
		rcvd +=n;
		if (mdata.ready_for_binary_data) {
			fprintf(stderr,"current rcvd size: %d\n", rcvd);
			fprintf(stderr,"mdata.content_length: %d\n", mdata.content_length);
			if (rcvd >= mdata.content_length) {
				//we have received all the binary data we want and can process the request

				mdata.binary_data = std::vector<char>(buffer.begin(), buffer.begin() + mdata.content_length);
				fprintf(stderr,"binary_data: %s\n", mdata.binary_data.data());
				buffer.erase(buffer.begin(), buffer.begin() + mdata.content_length);
				//now can process request and send back
				//need to shift buffer by this much
				rcvd -= mdata.content_length;
					//shift to the beginning of the buffer

					//set the remaining indices to be 0

			}
			mdata.ready_for_binary_data = false;
			if (mdata.method == "PUT") {
												//PUT
												status = PUT(kvs, mdata.rowKey, mdata.colKey, mdata.binary_data, mdata.content_type, mdata.content_length);
												std::vector<char> resp = formatResponse(status, mdata);
												do_write_vector(comm_fd, resp, resp.size());


											} else if (mdata.method == "CPUT") {
												//CPUT
												//need to get both old value and new value
												auto getIndex = std::search(mdata.binary_data.begin(), mdata.binary_data.end(), separator_vector.begin(), separator_vector.end());
												if (getIndex != mdata.binary_data.end()) {
													//this means we have found the location of the separator
													std::vector<char> old_value(mdata.binary_data.begin(), getIndex);
													size_t sepsize = separator_vector.size();
													std::vector<char> new_value(getIndex + sepsize, mdata.binary_data.end());
													fprintf(stderr,"Separator: %s\n",separator_vector.data());
													fprintf(stderr,"old_value: %s\n",old_value.data());
													fprintf(stderr,"new_value: %s\n",new_value.data());
													status = CPUT(kvs, mdata.rowKey, mdata.colKey, old_value, new_value, mdata.content_type, mdata.content_length);
													std::vector<char> resp = formatResponse(status, mdata);
													do_write_vector(comm_fd, resp, resp.size());
												}
											}
		}
		char *crlf_found = NULL;
		if (!mdata.ready_for_binary_data) {
			crlf_found = strstr(buffer.data(), "\r\n\r\n");
		}
		//this means we found a crlf and can process the command
		while (crlf_found != NULL) {
			if (print_output == 1) {
				fprintf(stderr, "[%d] ", comm_fd);
				fprintf(stderr, "C: %s\r\n", buffer.data());
			}

			//terminate trailing white spaces
						//iterate backwards from crlf_found until we finally don't see a new space
//						char *one_before = crlf_found-1;
//						while (one_before >=  buffer.data() && *one_before == ' ') {
//							//keep moving back
//							*one_before = '\0';
//							one_before--;
//						}
//						crlf_found = one_before+1;
//						int total_char = crlf_found - buffer.data();
//						//null terminate the string where \r\n has been found

						char *to_null;
						to_null = crlf_found+2;
						*to_null = '\0';
						int length_msg = to_null -(buffer.data());

						fprintf(stderr, "Here\n");
						std::string getbuffer(buffer.data(), length_msg);
						size_t find_eol = getbuffer.find("\r\n");
						std::string remaining;
						if (find_eol != std::string::npos) {
							//found /r/n
							std::string f_line = getbuffer.substr(0, find_eol);
							std::string func_name;
							std::string rowpluscolkey;
							std::string httpheader;

							std::istringstream iss(f_line);
							iss >> func_name >> rowpluscolkey >> httpheader;
							fprintf(stderr, "the method:%s\n", func_name.c_str());
							fprintf(stderr, "the rowpluscol:%s\n", rowpluscolkey.c_str());
							fprintf(stderr, "the httpheader:%s\n", httpheader.c_str());
							size_t getslash = rowpluscolkey.find('/');
							size_t getslash_2 = rowpluscolkey.find('/', getslash + 1);
							size_t second_index = getslash_2 - getslash - 1;
							std::string rowKey = rowpluscolkey.substr(getslash + 1, second_index);
							std::string colKey = rowpluscolkey.substr(getslash_2 + 1);
							fprintf(stderr, "the row key:%s\n", rowKey.c_str());
							fprintf(stderr, "the col key:%s\n", colKey.c_str());
							mdata.method = func_name;
							mdata.rowKey = rowKey;
							mdata.colKey = colKey;
							mdata.httpHeader = httpheader;
							remaining = getbuffer.substr(find_eol+2);

						}
						size_t find_eol_2 = remaining.find("\r\n");
						std::string remaining_2;
						if (find_eol_2 != std::string::npos) {
							//found /r/n
							std::string s_line = remaining.substr(0, find_eol_2);
							size_t get_space = s_line.find(' ');
							get_space+=1;
							size_t get_eol = s_line.find("\r\n");
							std::string get_num_str = s_line.substr(get_space, get_eol);
							mdata.content_length = std::stoul(get_num_str);
							fprintf(stderr, "the content length is %d\n", mdata.content_length);


							remaining_2 = remaining.substr(find_eol_2+2);

						}
						if (mdata.method == "PUT" || mdata.method == "CPUT") {
							//need to also get the content type
							fprintf(stderr, "the remaining %s\n", remaining_2.c_str());
							size_t find_eol_3 = remaining_2.find("\r\n");
							std::string remaining_3;
							if (find_eol_3 != std::string::npos) {
								//found /r/n
								std::string t_line = remaining_2.substr(0, find_eol_3);
								fprintf(stderr, "the line retrieved %s\n", t_line.c_str());
								size_t get_space = t_line.find(' ');
								get_space+=1;
								size_t get_eol = t_line.find("\r\n");
								std::string get_content_type = t_line.substr(get_space, get_eol);
								mdata.content_type = get_content_type;
								fprintf(stderr, "the content type is %s\n", mdata.content_type.c_str());

								remaining_3 = remaining_2.substr(find_eol_3+2);

							}
							mdata.ready_for_binary_data = true;
							//check if the data is already there in the buffer
							rcvd -= (crlf_found - buffer.data()) + 4;
															//shift to the beginning of the buffer

															//set the remaining indices to be 0
															memmove(buffer.data(),crlf_found + 4, rcvd);
															memset(buffer.data() + rcvd, 0, buffer.size() - rcvd);
							if (rcvd >= mdata.content_length) {
								mdata.binary_data = std::vector<char>(buffer.begin(), buffer.begin() + mdata.content_length);
								fprintf(stderr,"binary_data: %s\n", mdata.binary_data.data());
												buffer.erase(buffer.begin(), buffer.begin() + mdata.content_length);
												//now can process request and send back
												//need to shift buffer by this much
												rcvd -= mdata.content_length;
																mdata.ready_for_binary_data = false;
																//Now we need to process the function
								if (mdata.method == "PUT") {
									//PUT
									status = PUT(kvs, mdata.rowKey, mdata.colKey, mdata.binary_data, mdata.content_type, mdata.content_length);
									std::vector<char> resp = formatResponse(status, mdata);
									do_write_vector(comm_fd, resp, resp.size());


								} else if (mdata.method == "CPUT") {
									//CPUT
									//need to get both old value and new value
									auto getIndex = std::search(mdata.binary_data.begin(), mdata.binary_data.end(), separator_vector.begin(), separator_vector.end());
									if (getIndex != mdata.binary_data.end()) {
										//this means we have found the location of the separator
										std::vector<char> old_value(mdata.binary_data.begin(), getIndex);
										size_t sepsize = separator_vector.size();
										std::vector<char> new_value(mdata.binary_data.begin() + sepsize, mdata.binary_data.end());
										fprintf(stderr,"old_value: %s\n",old_value.data());
										fprintf(stderr,"new_value: %s\n",new_value.data());
										status = CPUT(kvs, mdata.rowKey, mdata.colKey, old_value, new_value, mdata.content_type, mdata.content_length);
										std::vector<char> resp = formatResponse(status, mdata);
										do_write_vector(comm_fd, resp, resp.size());
									}

								}
							}

						} else {
							if (mdata.method == "GET") {
								//get
								status = GET(kvs, mdata.rowKey, mdata.colKey);
								std::vector<char> resp = formatResponse(status, mdata);
								do_write_vector(comm_fd, resp, resp.size());
							} else if (mdata.method == "DELETE") {
								status = DELETE(kvs, mdata.rowKey, mdata.colKey);
								std::vector<char> resp = formatResponse(status, mdata);
								do_write_vector(comm_fd, resp, resp.size());

							}
						}

//						fprintf(stderr, "Here1\n");
//						char *first_line = strstr(buffer.data(), "\r\n");
//						fprintf(stderr, "Here2\n");
//						//now we have the whole header and need to split it up into different lines
//						int first_line_length = first_line -(buffer.data());
//						fprintf(stderr, "Here first_line_length %d\n", first_line_length);
//
//						fprintf(stderr, "Here3\n");
//						fprintf(stderr, "the first line:%s\n", f_line.c_str());



				//case: GET
//				if (strncasecmp(buffer.data(), "GET ", 4) == 0) {
//					int length_msg = crlf_found -(buffer.data()+4);
//					if (print_output == 1) {
//							fprintf(stderr, "[%d] ", comm_fd);
//							fprintf(stderr, "S: %s", ok_msg.c_str());
//							fprintf(stderr, "%s", buffer.data() + 4);
//							fprintf(stderr, "%s", end);
//						}
//				//need to get the row and column key
//				std::string rowkey_and_colkey(buffer.data()+4, length_msg);
//				size_t get_space = rowkey_and_colkey.find(' ');
//				std::string rowKey = rowkey_and_colkey.substr(0, get_space);
//				std::string colKey = rowkey_and_colkey.substr(get_space + 1);
//				status = GET(kvs, rowKey, colKey);
//				std::string message_to_send;
//				if (status.success) {
//					message_to_send = "SUCCESS, ";
//				} else {
//					message_to_send = "FAILURE, ";
//				}
//				//now need to append the data return
//				message_to_send.append(status.binary_data.begin(), status.binary_data.end());
//				message_to_send.append("\r\n");
//				do_write(comm_fd, message_to_send.c_str(), message_to_send.size());
//
//				}
//
//				//case: PUT
//				if (strncasecmp(buffer.data(), "PUT ", 4) == 0) {
//					int length_msg = crlf_found -(buffer.data()+4);
//					if (print_output == 1) {
//							fprintf(stderr, "[%d] ", comm_fd);
//							fprintf(stderr, "S: %s", ok_msg.c_str());
//							fprintf(stderr, "%s", buffer.data() + 4);
//							fprintf(stderr, "%s", end);
//						}
//				//need to get the row and column key
//				std::string rowkey_and_colkey(buffer.data()+4, length_msg);
//				size_t get_space_1 = rowkey_and_colkey.find(' ');
//				size_t get_space_2 = rowkey_and_colkey.find(' ', get_space_1 + 1);
//				std::string rowKey = rowkey_and_colkey.substr(0, get_space_1);
//				size_t second_index = get_space_2 - get_space_1 - 1;
//				std::string colKey = rowkey_and_colkey.substr(get_space_1 + 1, second_index);
//				std::string val = rowkey_and_colkey.substr(get_space_2 + 1);
//				std::vector<char> binary_data(val.begin(), val.end());
//				status = PUT(kvs, rowKey, colKey, binary_data);
//				std::string message_to_send;
//				if (status.success) {
//					message_to_send = "SUCCESS, ";
//				} else {
//					message_to_send = "FAILURE, ";
//				}
//				//now need to append the data return
//				message_to_send.append(status.binary_data.begin(), status.binary_data.end());
//				message_to_send.append("\r\n");
//				do_write(comm_fd, message_to_send.c_str(), message_to_send.size());
//
//				}
//				//case: CPUT
//				if (strncasecmp(buffer.data(), "CPUT ", 5) == 0) {
//					int length_msg = crlf_found -(buffer.data()+5);
//					if (print_output == 1) {
//							fprintf(stderr, "[%d] ", comm_fd);
//							fprintf(stderr, "S: %s", ok_msg.c_str());
//							fprintf(stderr, "%s", buffer.data() + 5);
//							fprintf(stderr, "%s", end);
//						}
//				//need to get the row and column key
//				std::string rowkey_and_colkey(buffer.data()+5, length_msg);
//				size_t get_space_1 = rowkey_and_colkey.find(' ');
//				size_t get_space_2 = rowkey_and_colkey.find(' ', get_space_1 + 1);
//				size_t get_space_3 = rowkey_and_colkey.find(' ', get_space_2 + 1);
//				std::string rowKey = rowkey_and_colkey.substr(0, get_space_1);
//				size_t second_index = get_space_2 - get_space_1 - 1;
//				std::string colKey = rowkey_and_colkey.substr(get_space_1 + 1, second_index);
//				size_t third_index = get_space_3 - get_space_2 - 1;
//				std::string old_val = rowkey_and_colkey.substr(get_space_2 + 1, third_index);
//				std::string new_val = rowkey_and_colkey.substr(get_space_3 + 1);
//				std::vector<char> binary_data_old_val(old_val.begin(), old_val.end());
//				std::vector<char> binary_data_new_val(new_val.begin(), new_val.end());
//				status = CPUT(kvs, rowKey, colKey, binary_data_old_val, binary_data_new_val);
//				std::string message_to_send;
//				if (status.success) {
//					message_to_send = "SUCCESS, ";
//				} else {
//					message_to_send = "FAILURE, ";
//				}
//				//now need to append the data return
//				message_to_send.append(status.binary_data.begin(), status.binary_data.end());
//				message_to_send.append("\r\n");
//				do_write(comm_fd, message_to_send.c_str(), message_to_send.size());
//
//				}
//				//case: DELETE
//				else if (strncasecmp(buffer.data(), "DELETE ", 7) == 0) {
//					int length_msg = crlf_found -(buffer.data()+7);
//					if (print_output == 1) {
//							fprintf(stderr, "[%d] ", comm_fd);
//							fprintf(stderr, "S: %s", ok_msg.c_str());
//							fprintf(stderr, "%s", buffer.data() + 7);
//							fprintf(stderr, "%s", end);
//						}
//				//need to get the row and column key
//				std::string rowkey_and_colkey(buffer.data()+7, length_msg);
//				size_t get_space = rowkey_and_colkey.find(' ');
//				std::string rowKey = rowkey_and_colkey.substr(0, get_space);
//				std::string colKey = rowkey_and_colkey.substr(get_space + 1);
//				status = DELETE(kvs, rowKey, colKey);
//				std::string message_to_send;
//				if (status.success) {
//					message_to_send = "SUCCESS, ";
//				} else {
//					message_to_send = "FAILURE, ";
//				}
//				//now need to append the data return
//				message_to_send.append(status.binary_data.begin(), status.binary_data.end());
//				message_to_send.append("\r\n");
//				do_write(comm_fd, message_to_send.c_str(), message_to_send.size());
//
//				}
//
//				//case: QUIT
//				else if (strncasecmp(buffer.data(), "QUIT", 5) == 0) {
//					if (print_output == 1) {
//						fprintf(stderr, "[%d] ", comm_fd);
//						fprintf(stderr, "S: %s\r\n", quit_msg.c_str());
//					}
//
//					do_write(comm_fd, quit_msg.c_str(), quit_msg.size());
//					//close the client
//					if (close(comm_fd) < 0) {
//						fprintf(stderr, "[%d] Error in closing connection \r\n", comm_fd);
//					}
//					connections_arr[index] = 0;
//					if (print_output == 1) {
//						fprintf(stderr, "[%d] Connection closed \r\n ", comm_fd);
//					}
//					pthread_detach(pthreads[index]);
//					//exit the thread
//					pthread_exit(NULL);
//						}
//
//				//case: invalid command
//				else {
//					if (print_output == 1) {
//						fprintf(stderr, "[%d] ", comm_fd);
//						fprintf(stderr, "S: %s\r\n", unknown_cmd_msg.c_str());
//					}
//					if (do_write(comm_fd, unknown_cmd_msg.c_str(), unknown_cmd_msg.size()) == false) {
//						if (close(comm_fd) < 0) {
//							fprintf(stderr, "[%d] Error in closing connection \r\n", comm_fd);
//						}
//						connections_arr[index] = 0;
//						if (print_output == 1) {
//							fprintf(stderr, "[%d] Connection closed \r\n ", comm_fd);
//						}
//						pthread_detach(pthreads[index]);
//						//exit the thread
//						pthread_exit(NULL);
//						}
//					memset(buffer.data(), 0 , buffer.size());
//					rcvd = 0;
//					//check if it exists for the next command
//					crlf_found = strstr(buffer.data(), "\r\n");
//					continue;
//									}
				//need to clear the buffer up till this point and shift the remaining characters that have already been read
							//number of characters left in the buffer

							//check if it exists for the next command
						if (mdata.method == "PUT" || mdata.method == "CPUT") {
							//this means we have already done the shifting
							if (!mdata.ready_for_binary_data) {
								crlf_found = strstr(buffer.data(), "\r\n\r\n");
							} else {
								crlf_found = NULL;
							}
						} else {
							//need to shift
							rcvd -= (crlf_found - buffer.data()) + 4;
															//shift to the beginning of the buffer

															//set the remaining indices to be 0
															memmove(buffer.data(),crlf_found + 4, rcvd);
															memset(buffer.data() + rcvd, 0, buffer.size() - rcvd);

															crlf_found = strstr(buffer.data(), "\r\n\r\n");

						}


			}
			}

	if (close(comm_fd) < 0) {
		if (print_output == 1) {
			fprintf(stderr, "[%d] Error in closing connection \r\n", comm_fd);

		}
	}
	connections_arr[index] = 0;
	if (print_output == 1) {
		fprintf(stderr, "[%d] Connection closed \r\n ", comm_fd);
	}
	pthread_detach(pthreads[index]);
	pthread_exit(NULL);
}

void init_server(int port_no) {
	//initializing mutex map
	initialize_mutex_map();
	//registering the signal handler
	int state[100];
	listen_fd = socket(PF_INET, SOCK_STREAM, 0);
	int opt = 1;
	int ret = setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR|SO_REUSEPORT, &opt, sizeof(opt));
	if (ret < 0) {
		fprintf(stderr, "error with setting sockopt");
				exit(EXIT_FAILURE);
	}
	std::string binding_address_for_main = "127.0.0.1";
	struct sockaddr_in servaddr;
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port_no);
	inet_pton(AF_INET, binding_address_for_main.c_str(), &(servaddr.sin_addr));
	//binding server to specific port port_no

	if (bind(listen_fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
		fprintf(stderr, "error with binding server");
		exit(EXIT_FAILURE);
	}
	if (listen(listen_fd, 10) < 0) {
			fprintf(stderr, "error with listening");
			exit(EXIT_FAILURE);
		}
	std::vector<char> binary_value = {'[', ']'};
	PUT(kvs, "1", "dir_root", binary_value, "application/octet-stream", binary_value.size());
	while (shutting_down == false) { //server is still running
		//client code
		struct sockaddr_in clientaddr;
		socklen_t clientaddrlen = sizeof(clientaddr);
		int comm_fd = accept(listen_fd, (struct sockaddr*)&clientaddr, &clientaddrlen);
		if (comm_fd < 0) {
			if (shutting_down == true) {
				break;
			}
			else {
				fprintf(stderr, "error with Accept");
				continue;
			}
		}
		int spot_found = 0;
		int spot_in_arr = -1;
		for (int i = 0; i < 100 && spot_found == 0; i++) {
			if (connections_arr[i] == 0) {//spot found!
				spot_found = 1;
				if (shutting_down == true) {
					if (close(comm_fd) < 0) {
						fprintf(stderr, "[%d] Error in closing connection \r\n", comm_fd);
					}
								spot_found = 0;
								break;
							}
				connections_arr[i] = comm_fd;
				spot_in_arr  = i;
				break;
			}
		}
		if (shutting_down == false) {
			if (spot_found == 0) {
						//this means that we are at capacity with connections
						fprintf(stderr, "Currently at capacity with connections");
						fprintf(stderr, "[%d] Connection closed\r\n", comm_fd);
						if (close(comm_fd) < 0) {
							fprintf(stderr, "[%d] Error in closing connection \r\n", comm_fd);
						}
						continue;
					}

					//create the thread for the client thread
						pthread_create(&pthreads[spot_in_arr], NULL, clientFunc ,&spot_in_arr);
		}


	}


}

int main(int argc, char *argv[])
{
	signal(SIGINT, ctrlc_handler);
		int port_no = 2500;
		int input_arg;
		while ((input_arg = getopt(argc, argv, "p:av")) != -1) {
		switch(input_arg) {
		case 'p': {
			//p argument
			port_no = atoi(optarg);
			break;
		}
		case 'a': {
			//a argument
			fprintf(stderr,"Author: Eesha Shekhar / eshekhar\n");
			exit(EXIT_SUCCESS);
			break;

		}
		case 'v': {
					//v argument
					print_output = 1;
					break;

				}
		default: {
			fprintf(stderr,"Incorrect command\n");
			return -1;
		}
		}
	}
//	if (optind < argc) {
//		//argv[optind] contains the mbox
//		mbox = argv[optind];
//	} else {
//		//mailbox not provided
//		fprintf(stderr,"Error: Mailbox not provided\n");
//	}

	init_server(port_no);

  return 0;
}
