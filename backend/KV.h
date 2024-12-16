#ifndef KV_MAP
#define KV_MAP

#include <unordered_map>
#include <pthread.h>
#include <string>
#include <vector>

typedef struct {
    std::vector<char> binary_data;
    std::string data_type;
    int size;
} Cell;

typedef struct {
    std::unordered_map<std::string, Cell> cols;
    pthread_mutex_t mutex_per_row;
} Row;

typedef struct {
    std::unordered_map<std::string, Row> KVmap;
} KVstore;

typedef struct {
    bool success = false;
    std::vector<char> binary_data;
    std::string data_type;
} Status;

extern KVstore kvs;

// Function to print Row contents
void printRow(const Row& row);

// Function to print KVstore contents
void printKVstore(const KVstore& store);


Status GET(KVstore &kvs, const std::string &row, const std::string &col);
Status PUT(KVstore &kvs, const std::string &row, const std::string &col, const std::vector<char> &value, const std::string &content_type, int size);
Status CPUT(KVstore &kvs, const std::string &row, const std::string &col, const std::vector<char> &old_value, const std::vector<char> &new_value, const std::string &content_type, int size);
Status DELETE(KVstore &kvs, const std::string &row, const std::string &col);

// Replication helpers
void initReplicationConfig(int total_tablets, int total_nodes, int replicas, int tablets_per_group);
int getTabletForRowKey(const std::string &rowKey);
int getPrimaryNodeForTablet(int tablet_id);
std::vector<int> getReplicaNodesForTablet(int tablet_id);

// Dynamic replication management
void setPrimaryNodeForTablet(int tablet_id, int node_id);
bool promoteNextReplicaAsPrimary(int tablet_id);
void removeNodeFromTabletReplicas(int node_id);
void addNodeBackToTabletReplicas(int node_id);
std::vector<int> getAllTabletsForNode(int node_id);
void setPrimaryNodeForTabletNull(int tablet_id);

#endif
