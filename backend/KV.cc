#include "KV.h"
#include <functional>
#include <algorithm>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <string>
#include <vector>
#include <iostream>
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
using namespace std;

KVstore kvs;

static int TOTAL_TABLETS;
static int TOTAL_NODES;
static int REPLICAS;
static int TABLETS_PER_GROUP;

typedef struct  {
	int node_id;
	bool active = true;

} replicaNode;

static std::vector<int> primaryNodesForTablet;          
static std::vector<std::vector<replicaNode>> replicasForTablet;


// Function to print Row contents
void printRow(const Row& row) {
    for (const auto& colPair : row.cols) {
        std::cout << " Column Key: " << colPair.first << "\n";
        for (const char& c : colPair.second.binary_data) {
          std::cout << c;
        }
        std::cout << "\n";
    }
}

// Function to print KVstore contents
void printKVstore(const KVstore& store) {
  cout << "PRINTING KVS STORE ---------------------" << endl;
    for (const auto& rowPair : store.KVmap) {
        std::cout << "Row Key: " << rowPair.first << "\n";
        printRow(rowPair.second);
        cout << "------- end of row ------" << endl;
    }
}

void initReplicationConfig(int total_tablets, int total_nodes, int replicas, int tablets_per_group) {
    TOTAL_TABLETS = total_tablets;
    TOTAL_NODES = total_nodes;
    REPLICAS = replicas;
    TABLETS_PER_GROUP = tablets_per_group;

    primaryNodesForTablet.resize(TOTAL_TABLETS);
    replicasForTablet.resize(TOTAL_TABLETS);

    for (int tablet_id = 0; tablet_id < TOTAL_TABLETS; tablet_id++) {
        int group_id = tablet_id / TABLETS_PER_GROUP;
        int start_node = group_id * REPLICAS;
        // replicas for this tablet
        std::vector<replicaNode> replica_set;
        for (int i = 0; i < REPLICAS; i++) {
            int node_id = (start_node + i) % TOTAL_NODES;
            replicaNode r;
            r.node_id = node_id;
            r.active = true;
            replica_set.push_back(r);
        }
//        fprintf(stderr, "for tablet id %d\n", tablet_id);
//        for (int j = 0; j < replica_set.size(); j++) {
//
//        	fprintf(stderr, "REPLICA NODE: %d ", j+1);
//        	fprintf(stderr, "stored at: %d\n", replica_set[j]);
//        }
        replicasForTablet[tablet_id] = replica_set;
        primaryNodesForTablet[tablet_id] = replica_set[0].node_id; // first replica is primary
    }
}

int getTabletForRowKey(const std::string &rowKey) {
    std::hash<std::string> get_hash_function;
    int index = (int)(get_hash_function(rowKey) % TOTAL_TABLETS);
    return index;
}

int getPrimaryNodeForTablet(int tablet_id) {
    return primaryNodesForTablet[tablet_id];
}

void setPrimaryNodeForTabletNull(int tablet_id) {
    primaryNodesForTablet[tablet_id] = -1;
}

std::vector<int> getReplicaNodesForTablet(int tablet_id) {
	std::vector<int> toReturn;
	for (int i = 0; i < replicasForTablet[tablet_id].size(); i++) {
		if (replicasForTablet[tablet_id][i].active) {
			toReturn.push_back(replicasForTablet[tablet_id][i].node_id);
		}
	}
    return toReturn;
}

void setPrimaryNodeForTablet(int tablet_id, int node_id) {
    // Ensure node_id is in the replicasForTablet list. If not, insert it.
	bool found = false;
    auto &replicas = replicasForTablet[tablet_id];
    for (int i = 0; i < replicas.size(); i++) {
    	if (replicas[i].node_id == node_id) {
    		found = true;
    	}
    }
    if (!found) {
    	replicaNode r;
    	r.node_id = node_id;
    	r.active = true;
    	replicas.push_back(r);
    }
//    if (std::find(replicas.begin(), replicas.end(), node_id) == replicas.end()) {
//        replicas.push_back(node_id);
//    }
    primaryNodesForTablet[tablet_id] = node_id;
}

bool promoteNextReplicaAsPrimary(int tablet_id) {
	fprintf(stderr, "Attempting appoint a node to be replica\n");
    // Find the current primary
    int current_primary = primaryNodesForTablet[tablet_id];
    auto &replicas = replicasForTablet[tablet_id];
    for (int i = 0; i < replicas.size(); i++) {
    	if (replicas[i].node_id != current_primary && replicas[i].active) {
    		fprintf(stderr, "set up replica: %d to be primary\n", replicas[i].node_id);
    		primaryNodesForTablet[tablet_id] = replicas[i].node_id;
    		return true;
    	}
    }
    return false;
//    for (int candidate : replicas) {
//        if (candidate != current_primary) {
//            // Set this candidate as new primary
//            primaryNodesForTablet[tablet_id] = candidate;
//            return true;
//        }
//    }
    // No other replica found
    return false;
}

void removeNodeFromTabletReplicas(int node_id) {
	fprintf(stderr, "node Id editing is: %d\n", node_id);
    for (int tablet_id = 0; tablet_id < TOTAL_TABLETS; tablet_id++) {
        auto &replicas = replicasForTablet[tablet_id];
        // erase node_id if present

//        fprintf(stderr,"before modifying the replicas for tablet %d\n",tablet_id);
//                for (int j = 0; j < replicas.size(); j++) {
//                	fprintf(stderr, "nodeid: %d\n", replicas[j]);
//                }
        for (int i = 0; i < replicas.size(); i++) {
            	if (replicas[i].node_id == node_id) {
            		replicas[i].active = false;
            	}
            }
//        replicas.erase(std::remove(replicas.begin(), replicas.end(), node_id), replicas.end());
//		for (auto j = replicas.begin(); j != replicas.end();) {
//					if (*j == node_id) {
//
//					}
//				}
//        fprintf(stderr,"after modifying the replicas for tablet %d\n",tablet_id);
//        for (int j = 0; j < replicas.size(); j++) {
//        	fprintf(stderr, "nodeid: %d\n", replicas[j]);
//        }
//        fprintf(stderr, "NEXT\n");
//        int current_primary = primaryNodesForTablet[tablet_id];
//        fprintf(stderr, "CURRENT PRIMARY IS: %d\n", current_primary);
//        if (current_primary == node_id) {
//
//            primaryNodesForTablet[tablet_id] = -1;
//        }
    }
}

void addNodeBackToTabletReplicas(int node_id) {
	fprintf(stderr, "node Id editing is: %d\n", node_id);
    for (int tablet_id = 0; tablet_id < TOTAL_TABLETS; tablet_id++) {
        auto &replicas = replicasForTablet[tablet_id];

        for (int i = 0; i < replicas.size(); i++) {
            	if (replicas[i].node_id == node_id) {
            		replicas[i].active = true;
            	}
            }

    }
}

std::vector<int> getAllTabletsForNode(int node_id) {
    std::vector<int> tablets_for_node;
    for (int tablet_id = 0; tablet_id < TOTAL_TABLETS; tablet_id++) {
        const auto &replicas = replicasForTablet[tablet_id];
        for (int i = 0; i < replicas.size(); i++) {
        	if (replicas[i].node_id == node_id) {
        		tablets_for_node.push_back(tablet_id);
        	}
        }

    }
    return tablets_for_node;
}

// KV operations:
Status GET(KVstore &kvs, const std::string &row, const std::string &col) {
    Status status;
    if (kvs.KVmap.find(row) == kvs.KVmap.end()) {
        status.success = false;
        return status;
    }
    Row &getRow = kvs.KVmap[row];
    pthread_mutex_lock(&getRow.mutex_per_row);
    if (getRow.cols.find(col) == getRow.cols.end()) {
        pthread_mutex_unlock(&getRow.mutex_per_row);
        status.success = false;
        return status;
    }
    status.binary_data = getRow.cols[col].binary_data;
    status.data_type = getRow.cols[col].data_type;
    pthread_mutex_unlock(&getRow.mutex_per_row);
    status.success = true;
//     printKVstore(kvs);
    return status;
}

Status PUT(KVstore &kvs, const std::string &row, const std::string &col, const std::vector<char> &value, const std::string &content_type, int size) {
	fprintf(stderr, "JUST RAN PUT ON %s and", row.c_str());
	fprintf(stderr, "AND %s\n", col.c_str());
    Status status;
    if (kvs.KVmap.find(row) == kvs.KVmap.end()) {
        Row add_row;
        pthread_mutex_init(&add_row.mutex_per_row, NULL);
        kvs.KVmap[row] = add_row;
    }
    Row &getRow = kvs.KVmap[row];
    pthread_mutex_lock(&getRow.mutex_per_row);
    getRow.cols[col].binary_data = value;
    getRow.cols[col].data_type = content_type;
    getRow.cols[col].size = size;
    pthread_mutex_unlock(&getRow.mutex_per_row);
    status.success = true;
    std::ofstream writer("error/logging",std::ios::out | std::ios::app);
    for (const auto& row_packed : kvs.KVmap) {
    		const std::string& rowkey = row_packed.first;
    		const auto& columns = row_packed.second;
    		for (const auto& col_packed : columns.cols) {
    			const std::string& colKey = col_packed.first;
    			const auto& celldata = col_packed.second;
    			writer << rowkey << " " << colKey << " " << celldata.data_type << " " << celldata.size << " ";
    			writer.write(celldata.binary_data.data(), celldata.size);
    			writer << "\n";
    		}
     	}

//      printKVstore(kvs);
    return status;
}

Status CPUT(KVstore &kvs, const std::string &row, const std::string &col, const std::vector<char> &old_value, const std::vector<char> &new_value, const std::string &content_type, int size) {
    Status status;
    if (kvs.KVmap.find(row) == kvs.KVmap.end()) {
        status.success = false;
        return status;
    }
    Row &getRow = kvs.KVmap[row];
    pthread_mutex_lock(&getRow.mutex_per_row);
    if (getRow.cols.find(col) == getRow.cols.end()) {
        pthread_mutex_unlock(&getRow.mutex_per_row);
        status.success = false;
        return status;
    }
    if (getRow.cols[col].binary_data == old_value) {
        getRow.cols[col].binary_data = new_value;
        getRow.cols[col].data_type = content_type;
        getRow.cols[col].size = size;
        status.success = true;
    } else {
        status.success = false;
    }
    pthread_mutex_unlock(&getRow.mutex_per_row);
    status.success = true;



    
    return status;
}

Status DELETE(KVstore &kvs, const std::string &row, const std::string &col) {
    Status status;
    auto rowFound = kvs.KVmap.find(row);
    if (rowFound == kvs.KVmap.end()) {
        status.success = false;
        return status;
    }
    Row &getRow = rowFound->second;
    pthread_mutex_lock(&getRow.mutex_per_row);
    auto getCol = getRow.cols.find(col);
    if (getCol == getRow.cols.end()) {
        pthread_mutex_unlock(&getRow.mutex_per_row);
        status.success = false;
        return status;
    }
    getRow.cols.erase(col);
    if (getRow.cols.empty()) {
        pthread_mutex_unlock(&getRow.mutex_per_row);
        kvs.KVmap.erase(row);
    } else {
        pthread_mutex_unlock(&getRow.mutex_per_row);
    }
    status.success = true;




//        printKVstore(kvs);


    return status;
}
