#ifndef STORAGE_API_H
#define STORAGE_API_H
#include "storage_request.h"
#include <stdbool.h>
#include <stddef.h>
#include "cache.h"

// Constants
#define MD5_STR_LEN 32
#define MAX_PATH_DEPTH 1024
#define CHUNK_SIZE (12 * 1024 * 1024)  // 5MB
//#define EMPTY_ENTRY_LIST EntryNode EMPTY_ENTRY_NODE = {"", "", NULL};
#define EMPTY_ENTRY_LIST NULL
#define SAFE_FREE(ptr) do { if ((ptr) != NULL) { free(ptr); (ptr) = NULL; } } while(0)
// Data structures
typedef struct {
    char filename[256];        
    char foldername[256];      
    char file_type[50];        
    size_t file_size;          
    char last_modified[512];    
    char user_id[50];          
    char *relative_path; 
    int chunk_count;      
} FileMetadata;

typedef struct EntryNode {
    char name[256];
    char type[50];
    char id[200] ;
    struct EntryNode *next;
} EntryNode;


// Main API 
bool upload_file(FileMetadata *metadata, void *content);
bool download_file(FileMetadata *metadata, FileMetadata *fileMeta, void **file_content);
bool create_folder(FileMetadata *metadata);
bool rename(FileMetadata *metadata, const char *new_name); // Rename file or folder
EntryNode* list_entries(FileMetadata *metadata,UserCache* user_cache);           // List files and folders under the target folder
bool move(FileMetadata *metadata, char *new_path);         // Move file or folder
bool delete_file(FileMetadata *metadata);
bool delete_folder(FileMetadata *metadata);


// Helper Functions
void *string_to_binary(const char *input, size_t *output_size) ;
char *binary_to_string(const void *binary_data, size_t size) ;
EntryNode* create_node(const char *name, const char *type,const char *id);
void free_list(EntryNode *head);
void generate_uuid(char *uuid_str);
void generate_file_id(char *file_id);
void generate_folder_id(char *folder_id);
int is_file(const char *name);
int is_valid_name(const char *name);
bool find_entry_and_get_id(const char *json_data, const char *name, const char *type, char *out_id, size_t id_size);
bool delete_entry(char *json_data, const char *name, const char *type);
bool append_entry(char *json_data, size_t json_size, const char *name, const char *type, const char *id);
void free_resources(char *path_copy, char **path_parts, int path_depth, char *json_data);
int resolve_parent_folder_id(const char* user_id, const char* relative_path, char* parent_folder_id, size_t id_size, UserCache* user_cache);
bool parse_file_metadata(const char *json_data, FileMetadata *metadata);
void free_file_metadata(FileMetadata *metadata);
bool update_file_metadata( char *json_data, const char *new_name, char **updated_json);
char* handle_upload_request(const char *user_id, const char *relative_path, const char *filename,
                            const char *file_type, size_t file_size, const char *last_modified, void *content,char **status_code);
char* handle_download_request(const char *user_id, const char *relative_path, const char *filename, FileMetadata *fileMeta, void **file_content,char **status_code) ;
char* handle_list_entries_request(const char *user_id, const char *relative_path, const char* foldername, EntryNode **entry_list_head,char **status_code) ;
char* handle_delete_file_request(const char *user_id, const char *relative_path,const char *filename,char **status_code) ;
char* handle_delete_folder_request(const char *user_id, const char *relative_path,const char* foldername,char **status_code) ;
char* handle_create_folder_request(const char *user_id, const char *relative_path, const char *foldername,char **status_code);
char* handle_rename_request(const char *user_id, const char *relative_path, const char *new_name,char **status_code);
char* handle_move_request(const char *user_id, const char *relative_path,  char *new_path,char **status_code) ;
void print_entry_list(EntryNode *head);
int strip_last_component(char* path, char* parent_path, size_t size) ;
#endif 

