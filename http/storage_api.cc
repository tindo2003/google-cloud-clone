#include "storage_api.h"
#include <stdio.h>
#include <string.h>
#include <uuid/uuid.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <regex.h>
#include <time.h>
#include "cache.h"
#include <string>
#include <iostream>

// Converts a string to binary data and returns it 

void *string_to_binary(const char *input, size_t *output_size) {
    if (input == NULL || output_size == NULL) {
        fprintf(stderr, "[string_to_binary] Error: Invalid input.\n");
        return NULL;
    }
    size_t size = strlen(input);
    *output_size = size;

    // Debug statement: output_size
    std::cout << "[INSIDE string_to_binary] output_size = " << *output_size << std::endl;


    void *binary_data = malloc(size);
    if (binary_data == NULL) {
        fprintf(stderr, "[string_to_binary] Error: Memory allocation failed.\n");
        return NULL;
    }
    memcpy(binary_data, input, size);
    return binary_data;
}

// Converts binary data to string and returns it 
char *binary_to_string(const void *binary_data, size_t size) {
    if (binary_data == NULL || size == 0) {
        fprintf(stderr, "[binary_to_string] Error: Invalid input.\n");
        return NULL;
    }
    char *string = (char *)malloc(size + 1);  // +1 for storing '\0'
    if (string == NULL) {
        fprintf(stderr, "[binary_to_string] Error: Memory allocation failed.\n");
        return NULL;
    }
    memcpy(string, binary_data, size);
    string[size] = '\0';
    return string;
}




EntryNode* create_node(const char *name, const char *type,const char *id) {
    EntryNode *node = (EntryNode *)malloc(sizeof(EntryNode));
    if (!node) return NULL;
    strncpy(node->name, name, sizeof(node->name) - 1);
    strncpy(node->type, type, sizeof(node->type) - 1);
    strncpy(node->id, id, sizeof(node->id) - 1);
    node->name[sizeof(node->name) - 1] = '\0';
    node->type[sizeof(node->type) - 1] = '\0';
    node->id[sizeof(node->id) - 1] = '\0';
    node->next = NULL;
    return node;
}

void free_list(EntryNode *head) {
    while (head) {
        EntryNode *temp = head;
        head = head->next;
        free(temp);
    }
}

void generate_uuid(char *uuid_str) {
    uuid_t uuid;
    uuid_generate(uuid);
    uuid_unparse(uuid, uuid_str);
}
// generate file ID
void generate_file_id(char *file_id) {
    char uuid_str[37];
    generate_uuid(uuid_str);
    snprintf(file_id, 50, "file_%s", uuid_str);
}

// generate folder ID
void generate_folder_id(char *folder_id) {
    char uuid_str[37];
    generate_uuid(uuid_str);
    snprintf(folder_id, 50, "%s", uuid_str);
}

// Check whether it is a file
int is_file( const char *name) {
    const char *extensions[] = {".txt", ".pdf", ".doc", ".jpg", ".png", ".mp4", ".mp3", NULL};
    const char *dot = strrchr(name, '.');
    if (dot != NULL) {
        for (int i = 0; extensions[i] != NULL; i++) {
            if (strcasecmp(dot, extensions[i]) == 0) {
                return 1;  // is a file
            }
        }
    }
    return 0;  // not a file
} 

// Check whether the folder name is valid and contains only letters, numbers, underscores, and dashes
int is_valid_name(const char *name) {
    regex_t regex;
    int ret;

    // Only letters, numbers, underscores, and dashes are allowed
    ret = regcomp(&regex, "^[a-zA-Z0-9_-]+$", REG_EXTENDED);
    if (ret != 0) {
        fprintf(stderr, "[is_valid_name] Error: Failed to compile regex.\n");
        return 0;
    }
    ret = regexec(&regex, name, 0, NULL, 0);
    regfree(&regex);
    return ret == 0;
}


bool find_entry_and_get_id(const char * json_data, const char *name, const char *type, char *out_id, size_t id_size) {
    char entry_pattern[150];
    snprintf(entry_pattern, sizeof(entry_pattern), "\"name\": \"%s\", \"type\": \"%s\", \"id\": \"", name, type);
    
    const char *pos = json_data;
    while ((pos = strstr(pos, entry_pattern)) != NULL) {
        pos += strlen(entry_pattern);
        
        const char *end = strchr(pos, '"');
        if (end == NULL) {
            fprintf(stderr, "[find_entry_and_get_id] Error: Malformed JSON, missing closing quote for ID.\n");
            return false;
        }

        size_t id_length = end - pos;
        if (id_length >= id_size) {
            fprintf(stderr, "[find_entry_and_get_id] Error: ID length exceeds buffer size.\n");
            return false;
        }

        if (out_id != NULL) {
            strncpy(out_id, pos, id_length);
            out_id[id_length] = '\0';  
        }
        return true;
    }

    return false;  
}


bool delete_entry(char *json_data, const char *name, const char *type) {
    char search_pattern[1024];
    snprintf(search_pattern, sizeof(search_pattern), "{\"name\": \"%s\", \"type\": \"%s\", \"id\": \"", name, type);
    printf("[delete_entry] json data : %s\n",json_data);
    char *entry_start = strstr(json_data, search_pattern);
    if (entry_start == NULL) {
        fprintf(stderr, "Error: Entry not found.\n");
        return false;
    }
    char *entry_end = strchr(entry_start, '}');
    if (entry_end == NULL) {
        fprintf(stderr, "[delete_entry] Error: Invalid JSON format.\n");
        return false;
    }
    entry_end++; 
    if (*entry_end == ',') {
        entry_end++;
    }
    else if (entry_start > json_data && *(entry_start - 1) == ',') {
        entry_start--;
    }

    memmove(entry_start, entry_end, strlen(entry_end) + 1);
    
    char *remaining_start = json_data + 1; 
    while (*remaining_start == ' ' || *remaining_start == '\t' || *remaining_start == '\n') {
        remaining_start++;
    }
    if (*remaining_start == ']') {
       
        snprintf(json_data, 3, "[]");
    }
    return true;
}



bool append_entry(char *json_data, size_t json_size, const char *name, const char *type, const char *id) {
    char new_entry[1024];
    snprintf(new_entry, sizeof(new_entry), "{\"name\": \"%s\", \"type\": \"%s\", \"id\": \"%s\"}", name, type, id);
    size_t len = strlen(json_data);
    
    if (len == 2 && json_data[0] == '[' && json_data[1] == ']') {
        snprintf(json_data, json_size, "[%s]", new_entry);
        return true;
    } else if (len > 2 && json_data[len - 1] == ']') {
        snprintf(json_data + len - 1, json_size - len, ",%s]", new_entry);
        return true;
    } else {
        fprintf(stderr, "[append_entry] Error: Invalid JSON format.\n");
        return false;
    }
}

void free_resources(char *path_copy, char **path_parts, int path_depth, char *json_data) {
    if (path_copy) {
        free(path_copy);
        path_copy = NULL; 
    }
    
    for (int i = 0; i < path_depth; ++i) {
        if (path_parts[i]) {
            free(path_parts[i]);
            path_parts[i] = NULL; 
        }
    }

    if (json_data) {
        free(json_data);
        json_data = NULL; 
    }
}

int resolve_parent_folder_id(const char* user_id, const char* relative_path, char* parent_folder_id, size_t id_size, UserCache* user_cache) {
    if (user_cache == NULL) {
        printf("[resolve_parent_folder_id] Error: user_cache is NULL.\n");
        return -3; 
    }
    if (user_id == NULL || relative_path == NULL || parent_folder_id == NULL) {
        printf("[resolve_parent_folder_id] Error: Invalid arguments.\n");
        return -1;
    }
    char* path_parts[MAX_PATH_DEPTH];
    int path_depth = 0;
    // Make a mutable copy of relative_path for tokenization
    char path_copy[1024];
    strncpy(path_copy, relative_path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    // Tokenize the path
    char* token = strtok(path_copy, "/");
    while (token != NULL) {
        if (strcmp(token, "") == 0 || strcmp(token, ".") == 0) {
            // Skip empty or current directory
            token = strtok(NULL, "/");
            continue;
        }
        if (strcmp(token, "..") == 0) {
            // Handle parent directory
            if (path_depth > 0) {
                path_depth--;
            }
            token = strtok(NULL, "/");
            continue;
        }
        if (path_depth >= MAX_PATH_DEPTH) {
            printf("[resolve_parent_folder_id] Error: Path too deep.\n");
            return -1;
        }
        path_parts[path_depth++] = token;
        token = strtok(NULL, "/");
    }

    if (path_depth == 0) {
        // Path is root
        strncpy(parent_folder_id, "root", id_size);
        parent_folder_id[id_size - 1] = '\0';
        return 0;
    }

    // Determine if the last component is a file
    const char* last_component = path_parts[path_depth - 1];
    if (is_file(last_component)) {
        printf("[resolve_parent_folder_id] Last component '%s' is identified as a file. Resolving up to its parent directory.\n", last_component);
        path_depth--; // Exclude the file from the path
        if (path_depth == 0) {
            // Parent is root
            strncpy(parent_folder_id, "root", id_size);
            parent_folder_id[id_size - 1] = '\0';
            return 0;
        }
    }

    // Initialize traversal from root
    DirCache* current_dir = &user_cache->root_dir;
    strncpy(parent_folder_id, "root", id_size);
    parent_folder_id[id_size - 1] = '\0';

    printf("[resolve_parent_folder_id] Starting traversal for path_depth: %d\n", path_depth);

    for (int i = 0; i < path_depth; i++) { // Traverse up to the parent directory
        printf("[resolve_parent_folder_id] Processing path part [%d]: '%s'\n", i, path_parts[i]);

        // Try to find the next directory in the cache
        DirCache* next_dir = get_subdir(current_dir, path_parts[i]);
        if (next_dir != NULL) {
            // Cache hit
            printf("[resolve_parent_folder_id] Cache hit for directory '%s'.\n", path_parts[i]);
            current_dir = next_dir;
            strncpy(parent_folder_id, current_dir->dir_id, id_size);
            parent_folder_id[id_size - 1] = '\0';
            continue;
        }

        // Cache miss; fetch from KV store and update cache
        char dir_key[256];
        snprintf(dir_key, sizeof(dir_key), "dir_%s", parent_folder_id);

        // Fetch directory content from KV store
        ResponseResult result = init_response_result();
        if (!get_response(SERVER_IP, SERVER_PORT, "GET", user_id, dir_key, NULL, 0, NULL, 0, &result)) {
            printf("[resolve_parent_folder_id] Error: Failed to retrieve directory data for '%s'.\n", dir_key);
            return -1;
        }

        char* existing_data = binary_to_string(result.content, result.content_length);
        if (existing_data == NULL) {
            printf("[resolve_parent_folder_id] Error: Failed to convert directory data for '%s'.\n", dir_key);
            SAFE_FREE(result.content);
            return -1;
        }

        // Parse directory content and update cache
        if (!parse_directory_content(current_dir, existing_data)) {
            printf("[resolve_parent_folder_id] Error: Failed to parse directory content for '%s'.\n", dir_key);
            SAFE_FREE(existing_data);
            SAFE_FREE(result.content);
            return -1;
        }

        SAFE_FREE(existing_data);
        SAFE_FREE(result.content);

        // Attempt to find the directory in the cache again
        next_dir = get_subdir(current_dir, path_parts[i]);
        if (next_dir != NULL) {
            printf("[resolve_parent_folder_id] After fetching, cache hit for directory '%s'.\n", path_parts[i]);
            current_dir = next_dir;
            strncpy(parent_folder_id, current_dir->dir_id, id_size);
            parent_folder_id[id_size - 1] = '\0';
            continue;
        }

        // If still not found, return error
        printf("[resolve_parent_folder_id] Error: Folder '%s' does not exist.\n", path_parts[i]);
        return -1;
    }

    printf("[resolve_parent_folder_id] Resolved parent_folder_id: '%s'\n", parent_folder_id);
    return 0;
}

//TODO if the same file already exists, the interaction with the user whether replace, is currently a message indicating that the user already exists, upload failed
//TODO Get retry times if failure occurs
// Function for uploading files
bool upload_file(FileMetadata *metadata, void *content) {
    size_t binary_size = 0;
    void *binary_data = NULL;
    char *json_data = NULL;
    char *target_folder_data = NULL;
    ResponseResult result = init_response_result();
    bool success = false; 
    char *file_name = metadata->filename; 
    size_t json_size = 4096;             
    size_t required_space; 
    // Get the old file or folder name from relative_path
    char target_folder_id[50];
    char target_dir_key[200];
    // related to file chunking
    size_t total_size = metadata->file_size;
    if (total_size == 0) {
        printf("[upload_file] Warning: Attempting to upload an empty file '%s'.\n", file_name);
        return false;
    }
    size_t offset = 0;
    int chunk_index = 1; // starting from 1
    int chunk_count= 0;
    const char *target_folder_name = strrchr(metadata->relative_path, '/');
    if (target_folder_name != NULL) {
        if (strcmp(target_folder_name, "/") == 0) {
          target_folder_name  = "/";  
          fprintf(stderr, "[upload_file] target_folder_name is / \n");
        }
        else{
            target_folder_name++;  
            if (*target_folder_name == '\0') {
                printf("[upload_file] Error: target_folder_name is empty string");
                return false;
            }
        }
        
    } else {
        printf("[upload_file] Error: Invalid path, '/' not found.\n");
        return false; 
    }
    
    printf("[upload_file] target_folder_name: %s\n",target_folder_name);

    if(is_file(target_folder_name)){
        printf("[upload_file] Error: target_folder_name isn't a folder, you can't upload a file to a file.");
        return false;
    }

    // Get parent folder ID
    UserCache* user_cache = find_user_cache(metadata->user_id);
    if (user_cache == NULL) {
        user_cache = add_user_cache(metadata->user_id);
        if (user_cache == NULL) {
            fprintf(stderr, "[upload_file] Error: Failed to create user cache for user '%s'.\n", metadata->user_id);
            return false;
        }
    }

    if (resolve_parent_folder_id(metadata->user_id, metadata->relative_path, target_folder_id, sizeof(target_folder_id), user_cache) != 0) {
        goto cleanup;
    }

    printf("[upload_file] Current target_folder_id for target folder '%s': %s\n", target_folder_name, target_folder_id);

    snprintf(target_dir_key, sizeof(target_dir_key), "dir_%s", target_folder_id);
    SAFE_FREE(result.content); 
    if (!get_response(SERVER_IP, SERVER_PORT, "GET", metadata->user_id, target_dir_key, NULL, 0, NULL, 0, &result)) {
            fprintf(stderr, "[upload_file] Error: Get response failed for folder '%s' and it's id is '%s' .\n",target_folder_name, target_folder_id);
            goto cleanup;
    }
    target_folder_data = binary_to_string(result.content, result.content_length);
    if (target_folder_data == NULL) {
        fprintf(stderr, "[upload_file] Critical Error: Folder '%s' exists but data retrieval failed (target_folder_data is NULL).\n", target_folder_name);
        goto cleanup;
    }
    
    fprintf(stderr, "[upload_file] target_folder_data is:  '%s'.\n",target_folder_data);

    // Check if target folder is already have same file name
    if (find_entry_and_get_id(target_folder_data, file_name, "file", NULL, 0)) {
        printf("[upload_file] Error: File '%s' already exists at target folder '%s'.\n", file_name, target_folder_name);
        goto cleanup;
    }

    char file_id[50];
    generate_file_id(file_id);

    json_data = (char *)malloc(json_size);
    if (json_data == NULL) {
        fprintf(stderr, "[upload_file] Error: Initial allocation failed.\n");
        goto cleanup;
    }

    strncpy(json_data, target_folder_data, json_size - 1);
    json_data[json_size - 1] = '\0';

    required_space = strlen(file_name) + strlen("file") + strlen(file_id) + strlen("{\"name\": \"\", \"type\": \"\", \"id\": \"\"}") + 20;
    while (strlen(json_data) + required_space >= json_size) {
        json_size *= 2;
        char *new_buffer = (char *)realloc(json_data, json_size);
        if (new_buffer == NULL) {
            fprintf(stderr, "[upload_file] Error: Memory reallocation failed.\n");
            goto cleanup;
        }
        json_data = new_buffer;
    }

    if (!append_entry(json_data, json_size, file_name, "file", file_id)){
        fprintf(stderr, "[upload_file] Error: Failed to append_entry.\n");
        goto cleanup;
    }
    fprintf(stderr, "[upload_file] after append json_data  is:  '%s'.\n",json_data);

    binary_data = string_to_binary(json_data, &binary_size);
    if (binary_data == NULL) {
        fprintf(stderr, "[upload_file] Error: Failed to convert JSON data to binary.\n");
        goto cleanup;
    }

    SAFE_FREE(result.content); 
    if (!get_response(SERVER_IP, SERVER_PORT, "PUT", metadata->user_id, target_dir_key, binary_data, binary_size, NULL, 0, &result)) {
        fprintf(stderr, "[upload_file] Error: PUT operation failed when updating directory data.\n");
        goto cleanup;
    }
    free(binary_data);
    binary_data = NULL;

    // add chunk_count into file meta data
    chunk_count = total_size / CHUNK_SIZE;
    if (total_size % CHUNK_SIZE != 0) {
        chunk_count += 1;
    }

    char file_meta_entry[4096];
    snprintf(file_meta_entry, sizeof(file_meta_entry),
         "{\"filename\": \"%s\", \"file_type\": \"%s\", \"file_size\": %zu, \"last_modified\": \"%s\", \"user_id\": \"%s\", \"relative_path\": \"%s\", \"chunk_count\": %d}",
         metadata->filename, metadata->file_type, metadata->file_size,
         metadata->last_modified, metadata->user_id, metadata->relative_path, chunk_count);

    binary_data = string_to_binary(file_meta_entry, &binary_size);
    if (binary_data == NULL) {
        fprintf(stderr, "[upload_file] Error: Failed to convert file metadata to binary.\n");
        goto cleanup;
    }

    char meta_key[400];
    snprintf(meta_key, sizeof(meta_key), "file_meta_%s", file_id);
    SAFE_FREE(result.content); 
    if (!get_response(SERVER_IP, SERVER_PORT, "PUT", metadata->user_id, meta_key, binary_data, binary_size, NULL, 0, &result)) {
        fprintf(stderr, "[upload_file] Error: PUT operation failed when storing file metadata.\n");
        goto cleanup;
    }

    free(binary_data);
    binary_data = NULL;
    char content_key[400];

    // Upload file content in blocks
    while (offset < total_size) {
        size_t chunk_size = total_size - offset > CHUNK_SIZE ? CHUNK_SIZE : total_size - offset;
        snprintf(content_key, sizeof(content_key), "file_content_chunk%d_%s",chunk_index, file_id);
        SAFE_FREE(result.content); 
        if (!get_response(SERVER_IP, SERVER_PORT, "PUT", metadata->user_id, content_key, (char *)content + offset, chunk_size, NULL, 0, &result)) {
            fprintf(stderr, "[upload_file] Error: PUT operation failed for chunk %d.\n", chunk_index);
            goto cleanup;
        }
        offset += chunk_size;
        chunk_index++;
    }

    success = true;
cleanup:
    SAFE_FREE(json_data);
    SAFE_FREE(target_folder_data);
    SAFE_FREE(result.content);
    SAFE_FREE(binary_data);
    return success;
}


bool parse_file_metadata(const char *json_data, FileMetadata *metadata) {
    if (json_data == NULL || metadata == NULL) {
        fprintf(stderr, "[parse_file_metadata] Error: Input is NULL.\n");
        return false;
    }
    printf("[parse_file_metadata] json_data: %s\n", json_data);

    const char *current = json_data;
    const char *field_start = NULL;

    // Extract "filename" field
    printf("[parse_file_metadata] Parsing filename...\n");
    field_start = strstr(current, "\"filename\": \"");
    if (field_start) {
        field_start += strlen("\"filename\": \"");
        const char *field_end = strchr(field_start, '"');
        if (!field_end) {
            fprintf(stderr, "[parse_file_metadata] Error: Missing closing quote for filename.\n");
            goto parse_error;
        }
        size_t len = field_end - field_start;
        if (len >= sizeof(metadata->filename)) len = sizeof(metadata->filename) - 1;
        strncpy(metadata->filename, field_start, len);
        metadata->filename[len] = '\0';
        current = field_end + 1;
    } else {
        fprintf(stderr, "[parse_file_metadata] Error: filename field not found.\n");
        goto parse_error;
    }

    // Extract "file_type" field
    printf("[parse_file_metadata] Parsing file_type...\n");
    field_start = strstr(current, "\"file_type\": \"");
    if (field_start) {
        field_start += strlen("\"file_type\": \"");
        const char *field_end = strchr(field_start, '"');
        if (!field_end) {
            fprintf(stderr, "[parse_file_metadata] Error: Missing closing quote for file_type.\n");
            goto parse_error;
        }
        size_t len = field_end - field_start;
        if (len >= sizeof(metadata->file_type)) len = sizeof(metadata->file_type) - 1;
        strncpy(metadata->file_type, field_start, len);
        metadata->file_type[len] = '\0';
        current = field_end + 1;
    } else {
        fprintf(stderr, "[parse_file_metadata] Error: file_type field not found.\n");
        goto parse_error;
    }

    // Extract "file_size" field
    printf("[parse_file_metadata] Parsing file_size...\n");
    field_start = strstr(current, "\"file_size\": ");
    if (field_start) {
        field_start += strlen("\"file_size\": ");
        char *end_ptr = NULL;
        metadata->file_size = strtoul(field_start, &end_ptr, 10);
        if (end_ptr == field_start || (*end_ptr != ',' && *end_ptr != '}')) {
            fprintf(stderr, "[parse_file_metadata] Error: Invalid number for file_size.\n");
            goto parse_error;
        }
        current = end_ptr;
    } else {
        fprintf(stderr, "[parse_file_metadata] Error: file_size field not found.\n");
        goto parse_error;
    }

    // Extract "last_modified" field
    printf("[parse_file_metadata] Parsing last_modified...\n");
    field_start = strstr(current, "\"last_modified\": \"");
    if (field_start) {
        field_start += strlen("\"last_modified\": \"");
        const char *field_end = strchr(field_start, '"');
        if (!field_end) {
            fprintf(stderr, "[parse_file_metadata] Error: Missing closing quote for last_modified.\n");
            goto parse_error;
        }
        size_t len = field_end - field_start;
        if (len >= sizeof(metadata->last_modified)) len = sizeof(metadata->last_modified) - 1;
        strncpy(metadata->last_modified, field_start, len);
        metadata->last_modified[len] = '\0';
        current = field_end + 1;
    } else {
        fprintf(stderr, "[parse_file_metadata] Error: last_modified field not found.\n");
        goto parse_error;
    }

    // Extract "user_id" field
    printf("[parse_file_metadata] Parsing user_id...\n");
    field_start = strstr(current, "\"user_id\": \"");
    if (field_start) {
        field_start += strlen("\"user_id\": \"");
        const char *field_end = strchr(field_start, '"');
        if (!field_end) {
            fprintf(stderr, "[parse_file_metadata] Error: Missing closing quote for user_id.\n");
            goto parse_error;
        }
        size_t len = field_end - field_start;
        if (len >= sizeof(metadata->user_id)) len = sizeof(metadata->user_id) - 1;
        strncpy(metadata->user_id, field_start, len);
        metadata->user_id[len] = '\0';
        current = field_end + 1;
    } else {
        fprintf(stderr, "[parse_file_metadata] Error: user_id field not found.\n");
        goto parse_error;
    }

    // Extract "relative_path" field
    printf("[parse_file_metadata] Parsing relative_path...\n");

    field_start = strstr(current, "\"relative_path\": \"");
    if (field_start) {
        field_start += strlen("\"relative_path\": \"");
        const char *field_end = strchr(field_start, '\"');
        if (!field_end) {
            fprintf(stderr, "[parse_file_metadata] Error: Missing closing quote for relative_path.\n");
            goto parse_error;
        }
        size_t len = field_end - field_start;

        // Allocate memory for relative_path and copy the value
        metadata->relative_path = (char *)malloc(len + 1);
        if (metadata->relative_path == NULL) {
            fprintf(stderr, "[parse_file_metadata] Error: Failed to allocate memory for relative_path.\n");
            goto parse_error;
        }
        strncpy(metadata->relative_path, field_start, len);
        metadata->relative_path[len] = '\0';
        current = field_end + 1;
        printf("[parse_file_metadata] Parsed relative_path: %s\n", metadata->relative_path);
    } else {
        fprintf(stderr, "[parse_file_metadata] Error: relative_path field not found.\n");
        goto parse_error;
    }

    // Extract "chunk_count" field
    printf("[parse_file_metadata] Parsing chunk_count...\n");
    field_start = strstr(current, "\"chunk_count\": ");
    if (field_start) {
        field_start += strlen("\"chunk_count\": ");
        char *end_ptr = NULL;
        metadata->chunk_count = strtol(field_start, &end_ptr, 10);

        if (end_ptr == field_start || (*end_ptr != ',' && *end_ptr != '}')) {
            fprintf(stderr, "[parse_file_metadata] Error: Invalid number for chunk_count.\n");
            goto parse_error;
        }
        current = end_ptr;
    } else {
        fprintf(stderr, "[parse_file_metadata] Error: chunk_count field not found.\n");
        goto parse_error;
    }

    return true;

parse_error:
    fprintf(stderr, "[parse_file_metadata] Error: Failed to parse metadata.\n");
    return false;
}

void free_file_metadata(FileMetadata *metadata) {
    if (metadata == NULL) {
        return;
    }
    if (metadata->relative_path != NULL) {
        free(metadata->relative_path);
        metadata->relative_path = NULL;
    }
}


bool update_file_metadata( char *json_data, const char *new_name, char **updated_json) {
    if (json_data == NULL || new_name == NULL ) {
        return false;
    }
    FileMetadata metadata ={0};
    metadata.relative_path=NULL;
    if (!parse_file_metadata(json_data, &metadata)) {
        fprintf(stderr, "[update_file_metadata] Error: Failed to parse old metadata.\n");
        free_file_metadata(&metadata);
        return false;
    }
    // Update the filename
    strncpy(metadata.filename, new_name, sizeof(metadata.filename) - 1);
    metadata.filename[sizeof(metadata.filename) - 1] = '\0';

    // Update the last_modified field to the current time
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    if (strftime(metadata.last_modified, sizeof(metadata.last_modified), "%Y-%m-%d %H:%M:%S", tm_info) == 0) {
        fprintf(stderr, "[update_file_metadata] Error: Failed to format current time.\n");
        free_file_metadata(&metadata);
        return false;
    }

    // Generate new JSON string
    size_t buffer_size = 4096; 
    *updated_json = (char *)malloc(buffer_size);
    if (*updated_json == NULL) {
        fprintf(stderr, "[update_file_metadata] Error: Memory allocation failed for updated JSON.\n");
        free_file_metadata(&metadata);
        return false;
    }

    int written = snprintf(
        *updated_json,
        buffer_size,
        "{\"filename\": \"%s\", \"file_type\": \"%s\", \"file_size\": %zu, \"last_modified\": \"%s\", \"user_id\": \"%s\", \"relative_path\": \"%s\", \"chunk_count\": %d}",
        metadata.filename,
        metadata.file_type,
        metadata.file_size,
        metadata.last_modified,
        metadata.user_id,
        metadata.relative_path,
        metadata.chunk_count
    );

    if (written < 0 || (size_t)written >= buffer_size) {
        fprintf(stderr, "[update_file_metadata] Error: Failed to generate updated JSON string. Buffer overflow.\n");
        free(*updated_json);
        *updated_json = NULL;
        free_file_metadata(&metadata);
        return false;
    }
    free_file_metadata(&metadata);
    return true;
}

// // free file content when you're done
// The caller needs to initialize a fileMeta to ensure that fileMeta is not NULL
// The header header is set according to the type of the file after the binary is sent
bool download_file(FileMetadata *metadata, FileMetadata *fileMeta, void **file_content) {
    char parent_folder_id[50];
    char *file_name = NULL;
    char dir_key[200];
    char *existing_data = NULL;
    char file_id[50];
    char *file_meta_data = NULL;
    char *file_data = NULL;
    ResponseResult result = init_response_result();
    bool success = false; 
    *file_content = NULL;
    size_t offset = 0;

    if (strlen(metadata->filename) > 0) {
        file_name = metadata->filename;
    } else {
        file_name = strrchr(metadata->relative_path, '/'); 
        if (file_name != NULL) {
            if (strcmp(file_name, "/") == 0) {
                fprintf(stderr, "[download_file] Error: This isn't a file \n");
                return false;
            } else {
                file_name++;
                if (*file_name == '\0') {
                    printf("[download_file] Error: Invalid path, file name is missing after '/'.\n");
                    return false;
                } 
            }
        } else {
            printf("[download_file] Error: Invalid path, '/' not found.\n");
            return false;
        }
    }

    if(!is_file(file_name)){
        printf("[download_file] Error: This isn't a file,you can't download a folder.\n");
        return false;
    }

    UserCache* user_cache = find_user_cache(metadata->user_id);
    if (user_cache == NULL) {
        user_cache = add_user_cache(metadata->user_id);
        if (user_cache == NULL) {
            fprintf(stderr, "[download_file] Error: Failed to create user cache for user '%s'.\n", metadata->user_id);
            return false;
        }
    }

    if (resolve_parent_folder_id(metadata->user_id, metadata->relative_path, parent_folder_id, sizeof(parent_folder_id), user_cache) != 0) {
        fprintf(stderr, "[download_file] Error: Failed to resolve parent_folder_id for file '%s'\n", metadata->filename);
        goto cleanup;
    }
    printf("Current parent folder ID for '%s': %s\n", file_name, parent_folder_id);

    snprintf(dir_key, sizeof(dir_key), "dir_%s", parent_folder_id);

    if (!get_response(SERVER_IP, SERVER_PORT, "GET", metadata->user_id, dir_key, NULL, 0, NULL, 0, &result)) {
        fprintf(stderr, "[download_file] Error: Get response failed for folder '%s'.\n", parent_folder_id);
        goto cleanup;
    }
    existing_data = binary_to_string(result.content, result.content_length);
    if (existing_data == NULL) {
        fprintf(stderr, "[download_file] Critical Error: Folder '%s' exists but data retrieval failed (existing_data is NULL).\n", parent_folder_id);
        goto cleanup;
    }

    if (!find_entry_and_get_id(existing_data, file_name, "file", file_id, sizeof(file_id))){
        fprintf(stderr, "[download_file] Error: File '%s' does not exist at path '%s'.\n", file_name, metadata->relative_path);
        goto cleanup;
    }
    
    //Get meta data of file
    char meta_key[200];
    snprintf(meta_key, sizeof(meta_key), "file_meta_%s", file_id);
    SAFE_FREE(result.content);
    if (!get_response(SERVER_IP, SERVER_PORT, "GET", metadata->user_id, meta_key, NULL, 0, NULL, 0, &result)) {
        fprintf(stderr, "[download_file] Error: Failed to get the response of file metadata,file name is: '%s'.\n", file_name);
        goto cleanup;
    }
    file_meta_data = binary_to_string(result.content, result.content_length);
    if (file_meta_data == NULL)  {
        fprintf(stderr, "[download_file] Error: Failed to retrieve metadata for file '%s'.\n", file_name);
        goto cleanup;
    }
    fprintf(stderr, "[download_file] file name : '%s' file meta data is : '%s' .\n", file_name,file_meta_data);

    // Parse meta data 
    if (!parse_file_metadata(file_meta_data, fileMeta)) {
        fprintf(stderr, "[download_file] Error: Failed to parse metadata for file '%s'.\n", file_name);
        goto cleanup;
    }

    *file_content = malloc(fileMeta->file_size);
    if (*file_content == NULL) {
        fprintf(stderr, "[download_file] Error: Memory allocation failed for file content.\n");
        goto cleanup;
    }
    memset(*file_content, 0, fileMeta->file_size); 

    // Read file content, handle both single chunk and multiple chunks
    char content_key[400];
    for (int chunk_index = 1; chunk_index <= fileMeta->chunk_count; chunk_index++) {
        
        snprintf(content_key, sizeof(content_key), "file_content_chunk%d_%s", chunk_index, file_id);

        SAFE_FREE(result.content);
        if (!get_response(SERVER_IP, SERVER_PORT, "GET", metadata->user_id, content_key, NULL, 0, NULL, 0, &result)) {
            fprintf(stderr, "[download_file] Error: Failed to get the response of file chunk %d, file name is: '%s'.\n", chunk_index, file_name);
            goto cleanup;
        }
        file_data = binary_to_string(result.content, result.content_length);
        if (file_data == NULL) {
            fprintf(stderr, "[download_file] Error: Failed to retrieve content for chunk %d of file: %s\n", chunk_index, file_name);
            goto cleanup;
        }
        memcpy((char *)(*file_content) + offset, file_data, result.content_length);
        offset += result.content_length;
        SAFE_FREE(file_data);
    }
    fprintf(stderr, "[download_file] successfully! file name : '%s' .\n", file_name);
    success = true; 

cleanup:
    SAFE_FREE(existing_data);
    SAFE_FREE(file_meta_data);
    SAFE_FREE(file_data);
    SAFE_FREE(result.content);
    if (!success) {
        SAFE_FREE(*file_content);
    }
    return success;
}

// /A/B/C  C1
bool create_folder(FileMetadata *metadata) {
    char *folder_name = metadata->foldername;
    DirCache* parent_dir = NULL;
    char dir_key[200];
    char *json_data = NULL;
    void *binary_data = NULL;
    size_t binary_size = 0;
    ResponseResult result = init_response_result();
    bool success = false; 
    size_t required_space;
    size_t json_size = 4096;
    char target_folder_id[50];
    char target_dir_key[200];
    char *target_folder_data = NULL;
    const char *target_folder_name = strrchr(metadata->relative_path, '/');
    if (target_folder_name != NULL) {
        if (strcmp(target_folder_name, "/") == 0) {
          target_folder_name  = "/";  
          fprintf(stderr, "[create_folder] target_folder_name is / \n");
        }
        else{
            target_folder_name++;  
            if (*target_folder_name == '\0') {
                printf("[create_folder] Error: target_folder_name is empty string");
                return false;
            }
        }
    } else {
        printf("[create_folder] Error: Invalid path, '/' not found.\n");
        return false; 
    }
    printf("[create_folder] target_folder_name: %s\n",target_folder_name);

    if (is_file(target_folder_name)) {
        fprintf(stderr, "[create_folder] Error: '%s' appears to be a file, not a folder.\n", target_folder_name);
        return false;
    }

    if (!is_valid_name(folder_name)) {
        fprintf(stderr, "[create_folder] Error: Invalid folder name '%s'. Only letters, numbers, underscores, and hyphens are allowed.\n", folder_name);
        return false;
    }

    UserCache* user_cache = find_user_cache(metadata->user_id);
    if (user_cache == NULL) {
        user_cache = add_user_cache(metadata->user_id);
        if (user_cache == NULL) {
            fprintf(stderr, "[create_folder] Error: Failed to create user cache for user '%s'.\n", metadata->user_id);
            return false;
        }
    }

    if (resolve_parent_folder_id(metadata->user_id, metadata->relative_path, target_folder_id, sizeof(target_folder_id), user_cache) != 0) {
        fprintf(stderr, "[create_folder] Error: Failed to resolve target_folder_id for folder '%s'.\n", metadata->foldername);
        return false;
    }

    fprintf(stderr, "[create_folder] target_folder_id : '%s' target folder name : '%s'.\n",target_folder_id ,target_folder_name);

    // if found target folder
    snprintf(target_dir_key, sizeof(target_dir_key), "dir_%s", target_folder_id);
        
    SAFE_FREE(result.content);
    if (!get_response(SERVER_IP, SERVER_PORT, "GET", metadata->user_id, target_dir_key, NULL, 0, NULL, 0, &result)) {
        fprintf(stderr, "[create_folder] Error: Get response failed for folder '%s' and it's id is '%s' .\n",target_folder_name, target_folder_id);
        goto cleanup;
    }
    target_folder_data = binary_to_string(result.content, result.content_length);
    if (target_folder_data == NULL) {
        fprintf(stderr, "[create_folder] Critical Error: Folder '%s' exists but data retrieval failed (target_folder_data is NULL).\n", target_folder_name);
        goto cleanup;
    }
    fprintf(stderr, "[create_folder] target_folder name is : '%s'  target_folder_data is:  '%s'.\n",target_folder_name,target_folder_data);

    // Check whether the folder with the same name already exists
    if (find_entry_and_get_id(target_folder_data , folder_name, "dir", NULL, 0)) {
        fprintf(stderr, "[create_folder] Error: Folder '%s' already exists at path '%s'.\n", folder_name, metadata->relative_path);
        goto cleanup;
    }

    char folder_id[50];
    generate_folder_id(folder_id);

    json_data = (char *)malloc(json_size);
    if (json_data == NULL) {
        fprintf(stderr, "[create_folder] Error: Initial allocation failed.\n");
        goto cleanup;
    }

    strncpy(json_data, target_folder_data, json_size - 1);
    json_data[json_size - 1] = '\0';

    required_space = strlen(folder_name) + strlen("dir") + strlen(folder_id) + strlen("{\"name\": \"\", \"type\": \"\", \"id\": \"\"}") + 20; // 额外冗余空间
    while (strlen(json_data) + required_space >= json_size) {
        json_size *= 2;
        char *new_buffer = (char *)realloc(json_data, json_size);
        if (new_buffer == NULL) {
            fprintf(stderr, "[create_folder] Error: Memory reallocation failed.\n");
            goto cleanup;
        }
        json_data = new_buffer;
    }

    
    if (!append_entry(json_data, json_size, folder_name, "dir", folder_id)){
        fprintf(stderr, "[create_folder] Error: Failed to append_entry.\n");
        goto cleanup;
    }

    binary_data = string_to_binary(json_data, &binary_size);
    if (binary_data == NULL) {
        fprintf(stderr, "[create_folder] Error: Failed to convert JSON data to binary.\n");
        goto cleanup;
    }
    // Update the parent directory
    SAFE_FREE(result.content);
    if (!get_response(SERVER_IP, SERVER_PORT, "PUT", metadata->user_id, target_dir_key, binary_data, binary_size, NULL, 0, &result)) {
        fprintf(stderr, "[create_folder] Error: Failed to update directory data for '%s'.\n", dir_key);
        goto cleanup;
    }
    free(binary_data);
    binary_data = NULL;

    // Create a new directory key with an initial value of "[]"
    char new_dir_key[200];
    snprintf(new_dir_key, sizeof(new_dir_key), "dir_%s", folder_id);

    binary_data = string_to_binary("[]", &binary_size);
    if (binary_data == NULL) {
        fprintf(stderr, "[create_folder] Error: Failed to convert '[]' to binary.\n");
        goto cleanup;
    }

    SAFE_FREE(result.content);
    if (!get_response(SERVER_IP, SERVER_PORT, "PUT", metadata->user_id, new_dir_key, binary_data, binary_size, NULL, 0, &result)) {
        fprintf(stderr, "[create_folder] Error: Failed to create new directory '%s'.\n", new_dir_key);
        goto cleanup;
    }
    free(binary_data);
    binary_data = NULL;

    parent_dir = find_dir_by_id(&user_cache->root_dir, target_folder_id);
    if (parent_dir != NULL) {
        DirCache* new_dir = (DirCache*)malloc(sizeof(DirCache));
        if (new_dir == NULL) {
            fprintf(stderr, "[create_folder] Error: Memory allocation failed for new directory cache.\n");
            goto cleanup;
        }
        strncpy(new_dir->dir_id, folder_id, sizeof(new_dir->dir_id) - 1);
        new_dir->dir_id[sizeof(new_dir->dir_id) - 1] = '\0';
        strncpy(new_dir->name, folder_name, sizeof(new_dir->name) - 1);
        new_dir->name[sizeof(new_dir->name) - 1] = '\0';
        new_dir->subdirs = NULL;
        new_dir->next = parent_dir->subdirs;
        parent_dir->subdirs = new_dir;
    }
    print_user_cache();
    success = true;

cleanup:
    SAFE_FREE(json_data);
    SAFE_FREE(target_folder_data);
    SAFE_FREE(result.content);
    SAFE_FREE(binary_data);
    return success;
}

int strip_last_component(char* path, char* parent_path, size_t size) {
    if (path == NULL || parent_path == NULL) {
        fprintf(stderr, "[strip_last_component] Error: NULL argument provided.\n");
        return -1;
    }
    // Make a mutable copy of the path to manipulate
    char path_copy[1024];
    strncpy(path_copy, path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    size_t len = strlen(path_copy);
    if (len == 0) {
        fprintf(stderr, "[strip_last_component] Error: Empty path provided.\n");
        return -2;
    }
    // Remove trailing slashes
    while (len > 1 && path_copy[len - 1] == '/') {
        path_copy[len - 1] = '\0';
        len--;
    }
    // Find the position of the last '/'
    char* last_slash = strrchr(path_copy, '/');
    if (last_slash == NULL) {
        // No '/' found in the path; cannot determine parent
        fprintf(stderr, "[strip_last_component] Error: No '/' found in the path '%s'. Cannot determine parent directory.\n", path);
        return -3;
    }
    if (last_slash == path_copy) {
        // The path is at the root directory (e.g., "/folder")
        // Parent is root "/"
        if (snprintf(parent_path, size, "/") >= size) {
            fprintf(stderr, "[strip_last_component] Error: Buffer size too small for root path.\n");
            return -4;
        }
        return 0;
    }
    // Calculate the length of the parent path
    size_t parent_len = last_slash - path_copy;
    if (parent_len >= size) {
        fprintf(stderr, "[strip_last_component] Error: Buffer size too small for parent path.\n");
        return -5;
    }
    // Copy the parent path
    strncpy(parent_path, path_copy, parent_len);
    parent_path[parent_len] = '\0'; // Null-terminate the string
    return 0;
}



// Rename folder
//Rename file
//      /A        /A.txt
bool rename(FileMetadata *metadata, const char *new_name) {
    // Get the old file or folder name from relative_path
    const char *old_name = strrchr(metadata->relative_path, '/');
    if (old_name != NULL) {
    if (strcmp(old_name, "/") == 0) {
        fprintf(stderr, "[rename] you can't rename root \n");
        return false;
    } else {
        old_name++;  
        if (*old_name == '\0') {
            fprintf(stderr, "[rename] Error: old_name is empty string\n");
            return false;
        }
    }
    } else {
        fprintf(stderr, "[rename] Error: Invalid path, '/' not found.\n");
        return false; 
    }

    printf("[rename] Old name: %s\n", old_name);
    const char *type = is_file(old_name) ? "file" : "dir";

    if (strcmp(type, "dir") == 0 &&!is_valid_name(new_name)) {
        fprintf(stderr, "[rename] Error: Invalid new name '%s'. Only letters, numbers, underscores, and hyphens are allowed.\n", new_name);
        return false;
    }
    if (strcmp(type, "file") == 0 &&!is_file(new_name)) {
        fprintf(stderr, "[rename] Error: Invalid new name : '%s' .\n", new_name);
        return false;
    }

    char parent_folder_id[50];
    UserCache* user_cache = find_user_cache(metadata->user_id);
    if (user_cache == NULL) {
        user_cache = add_user_cache(metadata->user_id);
        if (user_cache == NULL) {
            fprintf(stderr, "[rename] Error: Failed to create user cache for user '%s'.\n", metadata->user_id);
            return false;
        }
    }
    char parent_path[500]; 

    if(strip_last_component(metadata->relative_path, parent_path,sizeof(parent_path))!=0) {
        fprintf(stderr, "[rename] Error: Failed to strip_last_component for '%s'.\n", metadata->relative_path);
        return false;
    }

    fprintf(stderr, "[rename]: old path is '%s' after strip_last_component new path is '%s' .\n", metadata->relative_path,parent_path);

    if (resolve_parent_folder_id(metadata->user_id, parent_path, parent_folder_id, sizeof(parent_folder_id), user_cache) != 0) {
        fprintf(stderr, "[rename] Error: Failed to resolve parent_folder_id for '%s'.\n", metadata->foldername);
        return false;
    }
    printf("[rename] Old name: %s,  parent id: %s\n", old_name,parent_folder_id);
    char dir_key[200];
    snprintf(dir_key, sizeof(dir_key), "dir_%s", parent_folder_id);
    ResponseResult result = init_response_result();
    char *existing_data = NULL;
    char *json_data = NULL;
    void *binary_data = NULL;
    size_t binary_size = 0;
    bool success = false;
    size_t required_space;
    size_t json_size =0 ; 
    char file_meta_key[200];
    char *new_json = NULL;
    char id[50];
    // Get data of parent folder
    if (!get_response(SERVER_IP, SERVER_PORT, "GET", metadata->user_id, dir_key, NULL, 0, NULL, 0, &result)) {
        fprintf(stderr, "[rename] Critical Error: Failed to retrieve directory data for '%s'.\n", parent_folder_id);
        goto cleanup;
    }
    existing_data = binary_to_string(result.content, result.content_length);
    if (existing_data == NULL) {
        fprintf(stderr, "[rename] Critical Error: Failed to convert directory data for '%s'.\n", parent_folder_id);
        goto cleanup;
    }
    // Check if old name does exist in the path
    if (!find_entry_and_get_id(existing_data, old_name, type, id, sizeof(id))) {
        fprintf(stderr, "[rename] Error: %s '%s' does not exist at path '%s'.\n", type, old_name, metadata->relative_path);
        goto cleanup;
    }

    // Check whether the new name already exists in the same directory
    if (find_entry_and_get_id(existing_data, new_name, type, NULL, 0)) {
        fprintf(stderr, "[rename] Error: %s '%s' already exists at path '%s'.\n", type, new_name, metadata->relative_path);
        goto cleanup;
    }

    if (!delete_entry(existing_data, old_name, type)) {
        fprintf(stderr, "[rename] Error: Failed to delete old entry '%s'.\n", old_name);
        goto cleanup;
    }
    json_size = strlen(existing_data) + 4096; 
    json_data = (char *)malloc(json_size);
    if (json_data == NULL) {
        fprintf(stderr, "[rename] Error: Initial allocation failed.\n");
        goto cleanup;
    }
    strncpy(json_data, existing_data, json_size - 1);
    json_data[json_size - 1] = '\0';

    required_space = strlen(new_name) + strlen(type) + strlen(id) + strlen("{\"name\": \"\", \"type\": \"\", \"id\": \"\"}") + 20;
    while (strlen(json_data) + required_space >= json_size) {
        json_size *= 2;
        char *new_buffer = (char *)realloc(json_data, json_size);
        if (new_buffer == NULL) {
            fprintf(stderr, "[rename] Error: Memory reallocation failed.\n");
            goto cleanup;
        }
        json_data = new_buffer;
    }

    if (!append_entry(json_data, json_size, new_name, type, id)){
        fprintf(stderr, "[rename] Error: Failed to append entry.\n");
        goto cleanup;
    }

    binary_data = string_to_binary(json_data, &binary_size);
    if (binary_data == NULL) {
        fprintf(stderr, "[rename] Error: Failed to convert JSON data to binary.\n");
        goto cleanup;
    }
    SAFE_FREE(result.content);
    if (!get_response(SERVER_IP, SERVER_PORT, "PUT", metadata->user_id, dir_key, binary_data, binary_size, NULL, 0, &result)) {
        fprintf(stderr, "[rename] Error: Failed to update directory data for '%s'.\n", dir_key);
        goto cleanup;
    }

    if (strcmp(type, "dir") == 0) {
        DirCache* parent_dir = find_dir_by_id(&user_cache->root_dir, parent_folder_id);
        if (parent_dir != NULL) {
            DirCache* dir_to_rename = get_subdir(parent_dir, old_name);
            if (dir_to_rename != NULL) {
                strncpy(dir_to_rename->name, new_name, sizeof(dir_to_rename->name) - 1);
                dir_to_rename->name[sizeof(dir_to_rename->name) - 1] = '\0';
            }
        }
    }
    print_user_cache();

    // if type is file we need to adjust the file meta data
    if (strcmp(type, "file") == 0 ){
        snprintf(file_meta_key, sizeof(file_meta_key), "file_meta_%s", id);
        SAFE_FREE(result.content);
        if (!get_response(SERVER_IP, SERVER_PORT, "GET", metadata->user_id, file_meta_key, NULL, 0, NULL, 0, &result)) {
            fprintf(stderr, "[rename] Error: Failed to retrieve file metadata for '%s' id: '%s' .\n", old_name,id);
            goto cleanup;
        }
        char *file_meta_data = binary_to_string(result.content, result.content_length);
        if (file_meta_data == NULL) {
            fprintf(stderr, "[rename] Error: Failed to convert file metadata for '%s' id: '%s' .\n", old_name, id);
            goto cleanup;
        }
        
        if(!update_file_metadata(file_meta_data, new_name, &new_json)){
            fprintf(stderr, "[rename] Error: Failed to update file metadata for '%s' id: '%s' .\n", old_name, id);
            free(file_meta_data);
            goto cleanup;
        }
        free(file_meta_data);
        binary_size =0;
        binary_data = string_to_binary(new_json, &binary_size);
        if (binary_data == NULL) {
            fprintf(stderr, "[rename] Error: Failed to convert JSON data to binary.\n");
            goto cleanup;
        }
        SAFE_FREE(result.content);
        if (!get_response(SERVER_IP, SERVER_PORT, "PUT", metadata->user_id, file_meta_key, binary_data, binary_size, NULL, 0, &result)) {
            fprintf(stderr, "[rename] Error: Failed to update file meta data for '%s' id: '%s' .\n", old_name, id);
            goto cleanup;
        }
    }
    success = true;

cleanup:
    SAFE_FREE(existing_data);
    SAFE_FREE(json_data);
    SAFE_FREE(new_json);
    SAFE_FREE(result.content);
    SAFE_FREE(binary_data);
    return success;
}


// List  

EntryNode* list_entries(FileMetadata *metadata, UserCache* user_cache) {
    if (user_cache == NULL){
        printf("[list_entries] Error: user_cache is null.\n");
        return NULL; 
    }
    char dir_key[200];
    DirCache* parent_dir = NULL;
    char *existing_data = NULL;
    ResponseResult result = init_response_result();
    EntryNode *head = NULL, *tail = NULL;
    bool success = false;
    char *current = NULL;
    const char *folder_name;
    char target_folder_id[50];
    char target_key[400];
    if (strlen(metadata->foldername) > 0) {
        folder_name = metadata->foldername;
    } else {
        folder_name = strrchr(metadata->relative_path, '/'); 
        if (folder_name != NULL) {
            if (strcmp(folder_name, "/") == 0) {
                folder_name = "/";  
            } else {
                folder_name++;
                if (*folder_name == '\0') {
                    printf("[list_entries] Error: Invalid path, folder name is missing after '/'.\n");
                    return NULL;
                } 
            }
        } else {
            printf("[list_entries] Error: Invalid path, '/' not found.\n");
            return NULL;
        }
    }

    if (resolve_parent_folder_id(metadata->user_id, metadata->relative_path, target_folder_id, sizeof(target_folder_id), user_cache) != 0) {
        fprintf(stderr, "[list_entries] Error: Failed to resolve parent_folder_id for path '%s'.\n", metadata->relative_path);
        return NULL;
    }
    print_user_cache();
    printf("[list_entries] Resolved target_folder_id:'%s' target folder name is '%s' \n", target_folder_id,folder_name);

    // Gets the data for the destination folder
    snprintf(target_key, sizeof(target_key), "dir_%s", target_folder_id);
    SAFE_FREE(result.content);
    printf("[list_entries] **********IP:'%s' ************Port: '%d' \n", SERVER_IP,SERVER_PORT);
    if (!get_response(SERVER_IP, SERVER_PORT, "GET", metadata->user_id, target_key, NULL, 0, NULL, 0, &result)) {
        fprintf(stderr, "[list_entries] Critical Error: Failed to retrieve directory data for '%s'.\n", target_folder_id);
        goto cleanup;
    }
    existing_data = binary_to_string(result.content, result.content_length);
    if (existing_data == NULL) {
        fprintf(stderr, "[list_entries] Critical Error: Failed to convert directory data for '%s'.\n", target_folder_id);
        goto cleanup;
    }
    printf("[list_entries] Converted target folder data: %s\n", existing_data);

    if (existing_data && strcmp(existing_data, "[]") == 0) {
        success = true;
        head = EMPTY_ENTRY_LIST;  
        printf("[list_entries] there isn't any data in this folder, value is [] \n");
        goto cleanup;
    }

    current = existing_data;
    if (*current == '[') current++;

    while (*current != '\0' && *current != ']') {
        char name[256] = {0};
        char type[16] = {0};
        char id[200] = {0};

        // Gets the value of the name field
        char *name_pos = strstr(current, "\"name\": \"");
        if (name_pos) {
            name_pos += strlen("\"name\": \"");
            char *name_end = strchr(name_pos, '"');
            if (!name_end) goto parse_error;
            size_t name_len = name_end - name_pos;
            strncpy(name, name_pos, name_len);
            name[name_len] = '\0';
            printf("[list_entries] Extracted name: %s\n", name);
        } else {
            goto parse_error;
        }

        // Gets the value of the "type" field
        char *type_pos = strstr(current, "\"type\": \"");
        if (type_pos) {
            type_pos += strlen("\"type\": \"");
            char *type_end = strchr(type_pos, '"');
            if (!type_end) goto parse_error;
            size_t type_len = type_end - type_pos;
            strncpy(type, type_pos, type_len);
            type[type_len] = '\0';
            printf("[list_entries] Extracted type: %s\n", type);
        } else {
            goto parse_error;
        }

        // Get id
        char *id_pos = strstr(current, "\"id\": \"");
        if (id_pos) {
            id_pos += strlen("\"id\": \"");
            char *id_end = strchr(id_pos, '"');
            if (!id_end) goto parse_error;
            size_t id_len = id_end - id_pos;
            strncpy(id, id_pos, id_len);
            id[id_len] = '\0';
        } else {
            goto parse_error;
        }

        // If type is "dir" then build cache
        if (strcmp(type, "dir") == 0) {
            if (find_dir_by_id(&user_cache->root_dir, id) == NULL) {
                DirCache* parent_dir = find_dir_by_id(&user_cache->root_dir, target_folder_id);
                if (parent_dir != NULL) {
                    DirCache* new_dir = (DirCache*)malloc(sizeof(DirCache));
                    if (new_dir == NULL) {
                        fprintf(stderr, "[create_folder] Error: Memory allocation failed for new directory cache.\n");
                        goto parse_error;
                    }
                    strncpy(new_dir->dir_id, id, sizeof(new_dir->dir_id) - 1);
                    new_dir->dir_id[sizeof(new_dir->dir_id) - 1] = '\0';
                    strncpy(new_dir->name, name, sizeof(new_dir->name) - 1);
                    new_dir->name[sizeof(new_dir->name) - 1] = '\0';
                    new_dir->subdirs = NULL;
                    new_dir->next = parent_dir->subdirs;
                    parent_dir->subdirs = new_dir;
                }
            }
        }
        
        EntryNode *node = create_node(name, type,id);
        if (!node) goto parse_error;
        if (!head) {
            head = tail = node;
        } else {
            tail->next = node;
            tail = node;
        }

        current = strchr(type_pos, '}');
        if (!current) break;
        current++;
        while (*current == ',' || *current == ' ' || *current == '\n' || *current == '\r') current++;
    }

    success = true;
    goto cleanup;

parse_error:
    fprintf(stderr, "[list_entries] Error: Invalid JSON format.\n");
    if (head) {
        free_list(head);
        head = NULL;
    }
cleanup:
    SAFE_FREE(existing_data);
    SAFE_FREE(result.content);
    return success ? head : NULL;
}

//Move file or folder
// old /A  /A.txt       new /A/B
bool move(FileMetadata *metadata,  char *new_path) {
    const char *old_name = strrchr(metadata->relative_path, '/');
    if (old_name != NULL) {
        if (strcmp(old_name, "/") == 0) {
            printf("[move] Error: Sorry you can't move the root.\n");
            return false;  
        } else {
            old_name++; 
            if (*old_name == '\0') {
                printf("[move] Error: Invalid path, folder name is missing after '/'.\n");
                return false;
            }
        }
    } else {
        printf("[move] Error: Invalid path, '/' not found.\n");
        return false;
    }
    const char *type  = is_file(old_name) ? "file" : "dir";
    char old_parent_folder_id[50];
    char old_dir_key[200];
    DirCache* parent_dir = NULL;
    DirCache* new_parent_dir = NULL;
    DirCache* old_parent_dir = NULL;
    DirCache* moved_dir = NULL;
    char target_folder_id[50];
    char target_dir_key[200];
    char id[50];
    char *old_existing_data = NULL;
    char *target_existing_data = NULL;
    char *new_json_data = NULL;
    void *binary_data = NULL;
    size_t binary_size = 0;
    ResponseResult result = init_response_result();
    bool success = false;
    size_t json_size= 0;
    size_t required_space =0;
    const char *new_folder_name = strrchr(new_path, '/');
    if (new_folder_name != NULL) {
        if (strcmp(new_folder_name, "/") == 0) {
            new_folder_name = "/";  
            printf("[move] new_folder_name is '/'.\n");
        } else {
            new_folder_name++; 
            if (*new_folder_name == '\0') {
                printf("[move] Error: Invalid path, folder name is missing after '/'.\n");
                return false;
            }
        }
    } else {
        printf("[move] Error: Invalid path, '/' not found.\n");
        return false;
    }
    // Check if the new folder name is a file name(eg: B.txt)
    if (is_file(new_folder_name)){
        printf("[move] Error: This new folder name is invalid since it is: %s\n", new_folder_name);
        return false;
    }

    UserCache* user_cache = find_user_cache(metadata->user_id);
    if (user_cache == NULL) {
        user_cache = add_user_cache(metadata->user_id);
        if (user_cache == NULL) {
            fprintf(stderr, "[move] Error: Failed to create user cache for user '%s'.\n", metadata->user_id);
            return false;
        }
    }

    char old_parent_path[500]; 
    if(strip_last_component(metadata->relative_path, old_parent_path,sizeof(old_parent_path))!=0) {
        fprintf(stderr, "[move] Error: Failed to strip_last_component for '%s'.\n", metadata->relative_path);
        return false;
    }
    fprintf(stderr, "[move]: old path is '%s' after strip_last_component old_parent_path is '%s' .\n", metadata->relative_path,old_parent_path);

    if (resolve_parent_folder_id(metadata->user_id, old_parent_path, old_parent_folder_id, sizeof(old_parent_folder_id), user_cache) != 0) {
        fprintf(stderr, "[move] Error: Failed to resolve parent_folder_id for old path '%s'.\n", metadata->relative_path);
        return false;
    }
    printf("[move] old name is: '%s' and it's parent folder id is : '%s'.\n",old_name,old_parent_folder_id);


    snprintf(old_dir_key, sizeof(old_dir_key), "dir_%s", old_parent_folder_id);

    if (!get_response(SERVER_IP, SERVER_PORT, "GET", metadata->user_id, old_dir_key, NULL, 0, NULL, 0, &result)) {
        fprintf(stderr, "[move] Critical Error: Failed to retrieve directory data for old path '%s'.\n", old_parent_folder_id);
        goto cleanup;
    }
    old_existing_data = binary_to_string(result.content, result.content_length);
    if (old_existing_data == NULL) {
        fprintf(stderr, "[move] Critical Error: Failed to convert directory data for old path '%s'.\n", old_parent_folder_id);
        goto cleanup;
    }

    printf("[move] old_existing_data(parent data)  is: '%s' \n", old_existing_data);

    // find id of old file or folder in parent data
    if (!find_entry_and_get_id(old_existing_data, old_name, type, id, sizeof(id))) {
        fprintf(stderr, "[move] Error: %s '%s' does not exist in old path '%s'.\n", type, old_name, metadata->relative_path);
        goto cleanup;
    }

    // Resolve the new parent directory ID
    if (resolve_parent_folder_id(metadata->user_id, new_path, target_folder_id, sizeof(target_folder_id), user_cache) != 0) {
        fprintf(stderr, "[move] Error: Failed to resolve target folder ID for new path '%s'.\n", new_path);
        goto cleanup;
    }

    printf("[move] new folder name is: '%s' and target_folder_id is : '%s'.\n",new_folder_name,target_folder_id);

    snprintf(target_dir_key, sizeof(target_dir_key), "dir_%s", target_folder_id);
    // Get the existing data of the target directory
    SAFE_FREE(result.content);
    if (!get_response(SERVER_IP, SERVER_PORT, "GET", metadata->user_id, target_dir_key, NULL, 0, NULL, 0, &result)) {
        fprintf(stderr, "[move] Critical Error: Failed to retrieve directory data for target folder '%s'.\n", new_folder_name);
        goto cleanup;
    }
    target_existing_data = binary_to_string(result.content, result.content_length);
    if (target_existing_data == NULL) {
        fprintf(stderr, "[move] Critical Error: Failed to convert directory data for target folder '%s'.\n", target_folder_id);
        goto cleanup;
    }

    // Check whether a file or folder with the same name already exists in the destination path
    if (find_entry_and_get_id(target_existing_data, old_name, type, NULL, 0)) {
        fprintf(stderr, "[move] Error: %s '%s' already exists in target path '%s'.\n", type, old_name, new_path);
        goto cleanup;
    }

    // Removes the entry from the old directory data
    if (!delete_entry(old_existing_data, old_name, type)) {
        fprintf(stderr, "[move] Error: Failed to delete the entry '%s' from the old directory.\n", old_name);
        goto cleanup;
    }

    binary_data = string_to_binary(old_existing_data, &binary_size);
    if (binary_data == NULL) {
        fprintf(stderr, "[move] Error: Failed to convert old directory data to binary.\n");
        goto cleanup;
    }
    SAFE_FREE(result.content);
    if (!get_response(SERVER_IP, SERVER_PORT, "PUT", metadata->user_id, old_dir_key, binary_data, binary_size, NULL, 0, &result)) {
        fprintf(stderr, "[move] Error: Failed to update old directory data for '%s'.\n", old_dir_key);
        goto cleanup;
    }
    free(binary_data);
    binary_data = NULL;

    old_parent_dir = find_dir_by_id(&user_cache->root_dir, old_parent_folder_id);
    if (old_parent_dir != NULL) {
        if (strcmp(type, "dir") == 0){
            moved_dir = remove_subdir(old_parent_dir, old_name);
            if (moved_dir == NULL) {
                fprintf(stderr, "[move] Error: Failed to remove '%s' from old parent cache.\n", old_name);
                goto cleanup;
            }
        }
    } else {
        fprintf(stderr, "[move] Error: Old parent directory not found in cache for ID '%s'.\n", old_parent_folder_id);
        goto cleanup;
    }

   

    // Prepare new directory data
    json_size = strlen(target_existing_data) + 4096; 
    new_json_data = (char *)malloc(json_size);
    if (new_json_data == NULL) {
        fprintf(stderr, "[move] Error: Initial memory allocation failed.\n");
        goto cleanup;
    }
    strncpy(new_json_data, target_existing_data, json_size - 1);
    new_json_data[json_size - 1] = '\0';

    required_space = strlen(old_name) + strlen(type) + strlen(id) + strlen("{\"name\": \"\", \"type\": \"\", \"id\": \"\"}") + 20;
    while (strlen(new_json_data) + required_space >= json_size) {
        json_size *= 2;
        char *new_buffer = (char *)realloc(new_json_data, json_size);
        if (new_buffer == NULL) {
            fprintf(stderr, "[move] Error: Memory reallocation failed.\n");
            goto cleanup;
        }
        new_json_data = new_buffer;
    }
    if(!append_entry(new_json_data, json_size, old_name, type, id)){
          fprintf(stderr, "[move] Error: Failed to append.\n");
          goto cleanup;
    }
    binary_data = string_to_binary(new_json_data, &binary_size);
    if (binary_data == NULL) {
        fprintf(stderr, "[move] Error: Failed to convert new directory data to binary.\n");
        goto cleanup;
    }
    SAFE_FREE(result.content);
    if (!get_response(SERVER_IP, SERVER_PORT, "PUT", metadata->user_id, target_dir_key, binary_data, binary_size, NULL, 0, &result)) {
        fprintf(stderr, "[move] Error: Failed to update target directory data for '%s'.\n", target_dir_key);
        goto cleanup;
    }
    free(binary_data);
    binary_data = NULL;
    new_parent_dir = find_dir_by_id(&user_cache->root_dir, target_folder_id);
    if (new_parent_dir != NULL) {
        if (strcmp(type, "dir") == 0) {
            moved_dir->next = new_parent_dir->subdirs;
            new_parent_dir->subdirs = moved_dir;
            printf("[move] Cache updated: Moved subdir '%s' (ID: %s) to new parent '%s' (ID: %s).\n",
                   moved_dir->name, moved_dir->dir_id, new_parent_dir->name, new_parent_dir->dir_id);
        } 
    } else {
        printf("[move] Error: New parent directory not found in cache for ID '%s'.\n", target_folder_id);
    }

    print_user_cache();
    success = true;

cleanup:
    SAFE_FREE(old_existing_data);
    SAFE_FREE(target_existing_data);
    SAFE_FREE(new_json_data);
    SAFE_FREE(result.content);
    SAFE_FREE(binary_data);
    return success;
}


// Delete file
bool delete_file(FileMetadata *metadata) {
    const char *file_name;
    if (strlen(metadata->filename) > 0) {
        file_name = metadata->filename;
    } else {
        file_name = strrchr(metadata->relative_path, '/'); 
        if (file_name != NULL) {
            if (strcmp(file_name, "/") == 0) {
                printf("[delete_file] Error: Sorry you can't delete the root.\n");
                return false;   
            }else{
                file_name++; 
                if (*file_name == '\0') {
                    printf("[delete_file] Error: Invalid path, file name is missing after '/'.\n");
                    return false;
                }
            }
        } else {
            printf("[delete_file] Error: Invalid path, '/' not found.\n");
            return false;
        }
    }
    
    if (!is_file(file_name)){
        printf("[delete_file] Error: This isn't a file since the file name is: %s\n", file_name );
        return false;
    }

    char parent_folder_id[50];
    char dir_key[200];
    char *parent_data = NULL;
    char file_id[50];
    ResponseResult result = init_response_result();
    void *binary_data = NULL;
    size_t binary_size = 0;
    bool success = false;
    char *file_meta_data = NULL;
    FileMetadata fileMeta = {0};    

    UserCache* user_cache = find_user_cache(metadata->user_id);
    if (user_cache == NULL) {
        user_cache = add_user_cache(metadata->user_id);
        if (user_cache == NULL) {
            fprintf(stderr, "[delete_file] Error: Failed to create user cache for user '%s'.\n", metadata->user_id);
            return false;
        }
    }

    if (resolve_parent_folder_id(metadata->user_id, metadata->relative_path, parent_folder_id, sizeof(parent_folder_id), user_cache) != 0) {
        fprintf(stderr, "[delete_file] Error: Failed to resolve parent_folder_id for path '%s'.\n", metadata->relative_path);
        return false;
    }
    // column key of parent folder
    snprintf(dir_key, sizeof(dir_key), "dir_%s", parent_folder_id);

    // Get parent directory data
    if (!get_response(SERVER_IP, SERVER_PORT, "GET", metadata->user_id, dir_key, NULL, 0, NULL, 0, &result)) {
        fprintf(stderr, "[delete_file] Critical Error: Failed to retrieve directory data for '%s'.\n", parent_folder_id);
        goto cleanup;
    }
    parent_data = binary_to_string(result.content, result.content_length);
    if (parent_data == NULL) {
        fprintf(stderr, "[delete_file] Critical Error: Failed to convert directory data for '%s'.\n", parent_folder_id);
        goto cleanup;
    }

    // Find the ID of the file needs to delete
    if (!find_entry_and_get_id(parent_data, file_name, "file", file_id, sizeof(file_id))) {
        fprintf(stderr, "[delete_file] Error: File '%s' does not exist in path '%s'.\n", file_name, metadata->relative_path);
        goto cleanup;
    }

    // Delete a file entry from the parent directory data
    if (!delete_entry(parent_data, file_name, "file")) {
        fprintf(stderr, "[delete_file] Error: Failed to delete entry for file '%s'.\n", file_name);
        goto cleanup;
    }
    binary_data = string_to_binary(parent_data, &binary_size);
    if (binary_data == NULL) {
        fprintf(stderr, "[delete_file] Error: Failed to convert updated directory data to binary.\n");
        goto cleanup;
    }
    SAFE_FREE(result.content);
    if (!get_response(SERVER_IP, SERVER_PORT, "PUT", metadata->user_id, dir_key, binary_data, binary_size, NULL, 0, &result)) {
        fprintf(stderr, "[delete_file] Error: Failed to update directory data for '%s'.\n", dir_key);
        goto cleanup;
    }
    free(binary_data);
    binary_data = NULL;

    // Delete file metadata and content
    char file_meta[400];
    char file_content[400];
    snprintf(file_meta, sizeof(file_meta), "file_meta_%s", file_id);

    SAFE_FREE(result.content);
    if (!get_response(SERVER_IP, SERVER_PORT, "GET", metadata->user_id, file_meta, NULL, 0, NULL, 0, &result)) {
        fprintf(stderr, "[delete_file] Error: Failed to get the response of file metadata,file name is: '%s'.\n", file_name);
        goto cleanup;
    }
    file_meta_data = binary_to_string(result.content, result.content_length);
    if (file_meta_data == NULL)  {
        fprintf(stderr, "[delete_file] Error: Failed to retrieve metadata for file '%s'.\n", file_name);
        goto cleanup;
    }
    fprintf(stderr, "[delete_file] file name : '%s' file meta data is : '%s' .\n", file_name,file_meta_data);
    // Parse meta data 
    if (!parse_file_metadata(file_meta_data, &fileMeta)) {
        fprintf(stderr, "[delete_file] Error: Failed to parse metadata for file '%s'.\n", file_name);
        goto cleanup;
    }
    // delete filemeta
    SAFE_FREE(result.content);
    if (!get_response(SERVER_IP, SERVER_PORT, "DELETE", metadata->user_id, file_meta, NULL, 0, NULL, 0, &result)) {
        fprintf(stderr, "[delete_file] Error: Failed to delete file metadata '%s'.\n", file_meta);
        goto cleanup;
    }

    // delete all chunks of the file
    for (int chunk_index = 1; chunk_index <= fileMeta.chunk_count; chunk_index++){
        snprintf(file_content, sizeof(file_content), "file_content_chunk%d_%s", chunk_index, file_id);
        SAFE_FREE(result.content);
        if (!get_response(SERVER_IP, SERVER_PORT, "DELETE", metadata->user_id, file_content, NULL, 0, NULL, 0, &result)) {
            fprintf(stderr, "[delete_file] Error: Failed to delete file content '%s'.\n", file_content);
            goto cleanup;
        }
    }
    printf("[delete_file] Successfully deleted file '%s' from path '%s'.\n", file_name, metadata->relative_path);
    success = true;

cleanup:
    SAFE_FREE(parent_data);
    SAFE_FREE(result.content);
    SAFE_FREE(binary_data);
    SAFE_FREE(file_meta_data);
    free_file_metadata(metadata);
    return success;
}

// Delete folder
//  /A    /A/B
bool delete_folder(FileMetadata *metadata) {
    bool success = false;
    char *original_path = NULL;
    char *folder_data = NULL;
    char *new_path = NULL;
    char parent_folder_id[50];
    char folder_id[50];
    char dir_key[200];
    char target_dir_key[200];
    ResponseResult result = init_response_result();
    void *binary_data = NULL;
    size_t binary_size = 0;
    char *parent_data = NULL;
    const char *folder_name;
            folder_name = strrchr(metadata->relative_path, '/');
            if (folder_name != NULL) {
                if (strcmp(folder_name, "/") == 0) {
                    printf("[delete_folder] Error: Sorry you can't delete the root.\n");
                    return false;  
                } else {
                    folder_name++; 
                    if (*folder_name == '\0') {
                        printf("[delete_folder] Error: Invalid path, folder name is missing after '/'.\n");
                        return false;
                    }
                }
        } else {
            printf("[delete_folder] Error: Invalid path, '/' not found.\n");
            return false;
        } 
    printf("[delete_folder] *********folder name********** is: %s\n",folder_name );
    printf("[delete_folder] *********relative path********** is: %s\n",metadata->relative_path );

    if (is_file(folder_name)){
        printf("[delete_folder] Error: This isn't a folder since the folder name is: %s\n",folder_name );
        return false;
    }

    UserCache* user_cache = find_user_cache(metadata->user_id);
    if (user_cache == NULL) {
        user_cache = add_user_cache(metadata->user_id);
        if (user_cache == NULL) {
            fprintf(stderr, "[delete_folder] Error: Failed to create user cache for user '%s'.\n", metadata->user_id);
            return false;
        }
    }
    char parent_path[500]; 

    if(strip_last_component(metadata->relative_path, parent_path,sizeof(parent_path))!=0) {
        fprintf(stderr, "[delete_folder] Error: Failed to strip_last_component for '%s'.\n", metadata->relative_path);
        return false;
    }

    fprintf(stderr, "[delete_folder]: old path is '%s' after strip_last_component new path is '%s' .\n", metadata->relative_path,parent_path);

    if (resolve_parent_folder_id(metadata->user_id, parent_path, parent_folder_id, sizeof(parent_folder_id), user_cache) != 0) {
        fprintf(stderr, "[delete_folder] Error: Failed to resolve parent_folder_id for path '%s'.\n", metadata->relative_path);
        goto cleanup;
    }

    // column key of parent folder
    snprintf(dir_key, sizeof(dir_key), "dir_%s", parent_folder_id);
    if (!get_response(SERVER_IP, SERVER_PORT, "GET", metadata->user_id, dir_key, NULL, 0, NULL, 0, &result)) {
        fprintf(stderr, "[delete_folder] Critical Error: Failed to retrieve directory data for '%s'.\n", dir_key);
        goto cleanup;
    }

    parent_data = binary_to_string(result.content, result.content_length);
    if (parent_data == NULL) {
        fprintf(stderr, "[delete_folder] Critical Error: Failed to convert directory data for '%s'.\n", dir_key);
        goto cleanup;
    }

    if (!find_entry_and_get_id(parent_data, folder_name, "dir", folder_id, sizeof(folder_id))) {
        fprintf(stderr, "[delete_folder] Error: Folder '%s' does not exist in path '%s'.\n", folder_name, metadata->relative_path);
        goto cleanup;
    }

    // Get content of target folder
    snprintf(target_dir_key, sizeof(target_dir_key), "dir_%s", folder_id); // column key of target folder
    SAFE_FREE(result.content);
    if (!get_response(SERVER_IP, SERVER_PORT, "GET", metadata->user_id, target_dir_key, NULL, 0, NULL, 0, &result)) {
        fprintf(stderr, "[delete_folder] Critical Error: Failed to retrieve directory data for folder '%s'.\n", folder_id);
        goto cleanup;
    }
    folder_data = binary_to_string(result.content, result.content_length);
    if (folder_data == NULL) {
        fprintf(stderr, "[delete_folder] Critical Error: Failed to convert directory data for folder '%s'.\n", folder_id);
        goto cleanup;
    }

    // Check if target folder is empty
    if (strcmp(folder_data, "[]") == 0) {
        if (!delete_entry(parent_data, folder_name, "dir")) {
            fprintf(stderr, "[delete_folder] Error1: Failed to delete entry for folder '%s'.\n", folder_name);
            goto cleanup;
        }
        // Update value of parent folder
        binary_data = string_to_binary(parent_data, &binary_size);
        if (binary_data == NULL) {
            fprintf(stderr, "[delete_folder] Error: Failed to convert updated parent directory data to binary.\n");
            goto cleanup;
        }
        SAFE_FREE(result.content);
        if (!get_response(SERVER_IP, SERVER_PORT, "PUT", metadata->user_id, dir_key, binary_data, binary_size, NULL, 0, &result)) {
            fprintf(stderr, "[delete_folder] Error: Failed to update directory data for '%s'.\n", dir_key);
            goto cleanup;
        }
        free(binary_data);
        binary_data = NULL;

        // Delete value of target folder
        SAFE_FREE(result.content);
        if (!get_response(SERVER_IP, SERVER_PORT, "DELETE", metadata->user_id, target_dir_key, NULL, 0, NULL, 0, &result)) {
            fprintf(stderr, "[delete_folder] Error: Failed to delete directory key '%s'.\n", target_dir_key);  // Corrected variable
            goto cleanup;
        }


        DirCache* parent_dir = find_dir_by_id(&user_cache->root_dir, parent_folder_id);
        if (parent_dir != NULL) {
            remove_subdir(parent_dir, folder_name);
        }

        printf("[delete_folder] Successfully deleted empty folder '%s' from path '%s'.\n", folder_name, metadata->relative_path);
        success = true;
        goto cleanup;
    } else {
        // If the folder is not empty, parse the content and delete recursively
        char *current = folder_data;
        if (*current == '[') current++;

        while (*current != '\0' && *current != ']') {
            char name[256] = {0};
            char type[16] = {0};

            // Get the value of name field
            char *name_pos = strstr(current, "\"name\": \"");
            if (name_pos) {
                name_pos += strlen("\"name\": \"");
                char *name_end = strchr(name_pos, '"');
                if (!name_end) goto parse_error;
                size_t name_len = name_end - name_pos;
                strncpy(name, name_pos, name_len);
                name[name_len] = '\0';
                printf("[delete_folder] name is %s \n", name);
            } else {
                goto parse_error;
            }

            // Get the value of type field
            char *type_pos = strstr(current, "\"type\": \"");
            if (type_pos) {
                type_pos += strlen("\"type\": \"");
                char *type_end = strchr(type_pos, '"');
                if (!type_end) goto parse_error;
                size_t type_len = type_end - type_pos;
                strncpy(type, type_pos, type_len);
                type[type_len] = '\0';
                printf("[delete_folder] type is %s \n", type);
            } else {
                goto parse_error;
            }

            // Save original path
            original_path = strdup(metadata->relative_path);
            printf("[delete_folder] original path is %s \n", original_path);
            if (original_path == NULL) {
                fprintf(stderr, "[delete_folder] Error: Failed to allocate memory for original_path.\n");
                goto cleanup;
            }
            // Build new path
            size_t new_path_len = strlen(original_path) + strlen(name) + 2;
            new_path = (char *)malloc(new_path_len);
            if (new_path == NULL) {
                fprintf(stderr, "[delete_folder] Error: Failed to allocate memory for new_path.\n");
                free(original_path);
                original_path = NULL;
                goto cleanup;
            }
            snprintf(new_path, new_path_len, "%s/%s", original_path, name);
            printf("[delete_folder] new path is %s/%s \n", original_path, name);

            // Update metadata->relative_path
            free(metadata->relative_path);
            metadata->relative_path = new_path;

            // Delete files or folders by type
            if (strcmp(type, "file") == 0) {
                if (!delete_file(metadata)) {
                    fprintf(stderr, "[delete_folder] Error: Failed to delete file '%s'.\n", name);
                    goto cleanup;
                }

            } else if (strcmp(type, "dir") == 0) {
                if (!delete_folder(metadata)) {
                    fprintf(stderr, "[delete_folder] Error: Failed to delete subfolder '%s'.\n", name);
                    goto cleanup;
                }
            }

            // Restore metadata->relative_path and free memory
            free(metadata->relative_path);
            metadata->relative_path = original_path;
            printf("[delete_folder] after deleting relative path is %s , and original path is %s  \n", metadata->relative_path,original_path);
            original_path = NULL;
            new_path = NULL;

            // Move to the next entry
            current = strchr(type_pos, '}');
            if (!current) break;
            current++;
            while (*current == ',' || *current == ' ' || *current == '\n' || *current == '\r') current++;
        }
        fprintf(stderr, "[delete_folder]********relative path 222********* is '%s'.\n",metadata->relative_path );
        folder_name = strrchr(metadata->relative_path, '/');
            if (folder_name != NULL) {
                if (strcmp(folder_name, "/") == 0) {
                    printf("[delete_folder] Error: Sorry you can't delete the root.\n");
                    return false;  
                } else {
                    folder_name++; 
                    if (*folder_name == '\0') {
                        printf("[delete_folder] Error: Invalid path, folder name is missing after '/'.\n");
                        return false;
                    }
                }
        } else {
            printf("[delete_folder] Error: Invalid path, '/' not found.\n");
            return false;
        } 
        fprintf(stderr, "[delete_folder]********folder name********* is '%s'.\n", folder_name);
        fprintf(stderr, "[delete_folder]********parent_data********* is '%s'.\n", parent_data);
        // Delete target folder entry and update the parent directory
        if (!delete_entry(parent_data, folder_name, "dir")) {
            fprintf(stderr, "[delete_folder] Error2: Failed to delete entry for folder '%s'.\n", folder_name);
            goto cleanup;
        }

        // Update value of parent folder
        binary_data = string_to_binary(parent_data, &binary_size);
        if (binary_data == NULL) {
            fprintf(stderr, "[delete_folder] Error: Failed to convert updated parent directory data to binary.\n");
            goto cleanup;
        }
        SAFE_FREE(result.content);
        if (!get_response(SERVER_IP, SERVER_PORT, "PUT", metadata->user_id, dir_key, binary_data, binary_size, NULL, 0, &result)) {
            fprintf(stderr, "[delete_folder] Error: Failed to update directory data for '%s'.\n", dir_key);
            goto cleanup;
        }
        free(binary_data);
        binary_data = NULL;

        // Delete target folder
        SAFE_FREE(result.content);
        if (!get_response(SERVER_IP, SERVER_PORT, "DELETE", metadata->user_id, target_dir_key, NULL, 0, NULL, 0, &result)) {
            fprintf(stderr, "[delete_folder] Error: Failed to delete directory key '%s'.\n", target_dir_key); 
            goto cleanup;
        }

        DirCache* parent_dir = find_dir_by_id(&user_cache->root_dir, parent_folder_id);
        if (parent_dir != NULL) {
            remove_subdir(parent_dir, folder_name);
        }

        printf("[delete_folder] Successfully deleted folder '%s' from path '%s'.\n", folder_name, metadata->relative_path);

        print_user_cache();
        success = true;
    }

    goto cleanup;

parse_error:
    fprintf(stderr, "[delete_folder] Error: Invalid JSON format in folder data.\n");
    success = false;
    goto cleanup;  // Ensure cleanup is called

cleanup:
    SAFE_FREE(original_path);
    SAFE_FREE(new_path);
    SAFE_FREE(parent_data);
    SAFE_FREE(folder_data);
    SAFE_FREE(binary_data);
    SAFE_FREE(result.content);
    return success;
}


char* handle_upload_request(const char *user_id, const char *relative_path, const char *filename,
                            const char *file_type, size_t file_size, const char *last_modified, void *content, char **status_code) {
    if (user_id == NULL || strlen(user_id) == 0) {
        fprintf(stderr, "Error: user_id is missing in the request body.\n");
        if (status_code){
          free(*status_code);
         *status_code = strdup("400 Bad Request");  
        } 
        return strdup("<html><body><h1>Error: Missing user_id.</h1></body></html>");
    }
    if (relative_path == NULL || strlen(relative_path) == 0) {
        fprintf(stderr, "Error: relative_path is missing in the request body.\n");
        if (status_code){
            free(*status_code);
            *status_code = strdup("400 Bad Request");
        }
        return strdup("<html><body><h1>Error: Missing relative_path.</h1></body></html>");
    }
    if (filename == NULL || strlen(filename) == 0) {
        fprintf(stderr, "Error: filename is missing in the request body.\n");
        if (status_code){
             free(*status_code);
             *status_code = strdup("400 Bad Request");
        } 
        return strdup("<html><body><h1>Error: Missing filename.</h1></body></html>");
    }
    if (file_type == NULL || strlen(file_type) == 0) {
        fprintf(stderr, "Error: file_type is missing in the request body.\n");
        if (status_code){
            free(*status_code);
            *status_code = strdup("400 Bad Request");
        } 
        return strdup("<html><body><h1>Error: Missing file_type.</h1></body></html>");
    }
    if (file_size == 0) {
        fprintf(stderr, "Error: file_size is missing or invalid in the request body.\n");
        if (status_code){
            free(*status_code);
            *status_code = strdup("400 Bad Request");
        } 
        return strdup("<html><body><h1>Error: Invalid file_size.</h1></body></html>");
    }
    if (content == NULL) {
        fprintf(stderr, "Error: content is missing in the request body.\n");
        if (status_code){
            free(*status_code);
            *status_code = strdup("400 Bad Request");
        } 
        return strdup("<html><body><h1>Error: Missing content.</h1></body></html>");
    }
    FileMetadata metadata = {0};
    snprintf(metadata.user_id, sizeof(metadata.user_id), "%s", user_id);
    snprintf(metadata.filename, sizeof(metadata.filename), "%s", filename);
    snprintf(metadata.file_type, sizeof(metadata.file_type), "%s", file_type);
    metadata.file_size = file_size;

    if (last_modified != NULL && strlen(last_modified) > 0) {
        snprintf(metadata.last_modified, sizeof(metadata.last_modified), "%s", last_modified);
    } else {
        snprintf(metadata.last_modified, sizeof(metadata.last_modified), "Unknown"); // Default value
    }

    metadata.relative_path = strdup(relative_path);
    if (metadata.relative_path == NULL) {
        fprintf(stderr, "Error: Memory allocation failed for relative_path.\n");
        if (status_code){
            free(*status_code);
            *status_code = strdup("500 Internal Server Error");
        } 
        return strdup("<html><body><h1>Error: Internal server error.</h1></body></html>");
    }
    bool upload_success = upload_file(&metadata, content);

    free(metadata.relative_path);
    if (upload_success) {
        return strdup("<html><body><h1>File uploaded successfully.</h1></body></html>");
    } else {
        if (status_code){
            free(*status_code);
            *status_code = strdup("500 Internal Server Error");
        } 
        return strdup("<html><body><h1>Error: Failed to upload file. Please try again later.</h1></body></html>");
    }
}

char* handle_download_request(const char *user_id, const char *relative_path, const char *filename, FileMetadata *fileMeta, void **file_content,char **status_code) {
    if (strlen(user_id) == 0) {
        fprintf(stderr, "Warning: user_id is missing in the request body.\n");
        if (status_code){
            free(*status_code);
            *status_code = strdup("400 Bad Request");
        } 
        return strdup("<html><body><h1>Error: Missing user_id.</h1></body></html>");
    }
    if (strlen(relative_path) == 0) {
        fprintf(stderr, "Warning: relative_path is missing in the request body.\n");
        if (status_code){
            free(*status_code);
            *status_code = strdup("400 Bad Request");
        } 
        return strdup("<html><body><h1>Error: Missing relative_path.</h1></body></html>");
    }
     // if (strlen(filename) == 0) {
    //     fprintf(stderr, "Warning: filename is missing in the request body.\n");
    // }
    // if (is_file(filename) == 0) {
    //     // it's not a file
    //     return strdup("<html><body><h1>Error: This isn't a file.</h1></body></html>");
    // }

    // Build the FileMetadata structure
    FileMetadata metadata = {0};
    snprintf(metadata.user_id, sizeof(metadata.user_id), "%s", user_id);
    metadata.relative_path = strdup(relative_path);
    snprintf(metadata.filename, sizeof(metadata.filename), "%s", filename);

    // Call download_file function
    bool download_success = download_file(&metadata, fileMeta, file_content);

    free(metadata.relative_path);

    if (download_success) {
        return strdup("<html><body><h1>File downloaded successfully.</h1></body></html>");
    } else {
        if (status_code){
            free(*status_code);
            *status_code =  strdup("500 Internal Server Error");
        } 
        return strdup("<html><body><h1>Error: Failed to download file.</h1></body></html>");
    }
}

char* handle_list_entries_request(const char *user_id, const char *relative_path, const char* foldername, EntryNode **entry_list_head,char **status_code) {
    if (strlen(user_id) == 0) {
        fprintf(stderr, "Warning: user_id is missing in the request body.\n");
        if (status_code){
            free(*status_code);
            *status_code = strdup("400 Bad Request");
        } 
        return strdup("<html><body><h1>Error: Missing user_id.</h1></body></html>");
    }
    if (strlen(relative_path) == 0) {
        fprintf(stderr, "Warning: relative_path is missing in the request body.\n");
        if (status_code){
            free(*status_code);
            *status_code = strdup("400 Bad Request");
        } 
        return strdup("<html><body><h1>Error: Missing relative_path.</h1></body></html>");
    }
    // if (strlen(foldername) == 0) {
    //     fprintf(stderr, "Warning: foldername is missing in the request body.\n");
    // }
    // if (is_file(foldername) == 1) {
    //     return strdup("<html><body><h1>Error: This isn't a folder, so you can't call list_entries.</h1></body></html>");
    // }
    FileMetadata metadata = {0};
    snprintf(metadata.user_id, sizeof(metadata.user_id), "%s", user_id);
    metadata.relative_path = strdup(relative_path);
    snprintf(metadata.foldername, sizeof(metadata.foldername), "%s", foldername);

    UserCache* user_cache = find_user_cache(metadata.user_id);
    if (user_cache == NULL) {
        user_cache = add_user_cache(metadata.user_id);
        if (user_cache == NULL) {
            fprintf(stderr, "[handle_list_entries_request] Error: Failed to create user cache for user '%s'.\n", metadata.user_id);
            if (status_code) {
                free(*status_code);
                *status_code = strdup("500 Internal Server Error");
            }
            free(metadata.relative_path);
            return strdup("<html><body><h1>Error: Internal server error.</h1></body></html>");
        }
    }

    // **Pass the user_cache to list_entries**
    *entry_list_head = list_entries(&metadata, user_cache);


    print_entry_list(*entry_list_head);
    
    free(metadata.relative_path);

    // special case
    if (entry_list_head == EMPTY_ENTRY_LIST) {
        return strdup("<html><body><h1>Entries listed successfully but folder is empty.</h1></body></html>");
    }
    if (entry_list_head != NULL) {
        return strdup("<html><body><h1>Entries listed successfully.</h1></body></html>");
    } else {
        if (status_code){
            free(*status_code);
            *status_code = strdup("500 Internal Server Error");
        } 
        return strdup("<html><body><h1>Error: Failed to list entries.</h1></body></html>");
    }
}

char* handle_delete_file_request(const char *user_id, const char *relative_path,const char *filename,char **status_code) {
    if (user_id == NULL || strlen(user_id) == 0) {
        fprintf(stderr, "Error: user_id is missing in the request body.\n");
        if (status_code){
            free(*status_code);
            *status_code = strdup("400 Bad Request");
        } 
        return strdup("<html><body><h1>Error: Missing user_id.</h1></body></html>");
    }
    if (relative_path == NULL || strlen(relative_path) == 0) {
        fprintf(stderr, "Error: relative_path is missing in the request body.\n");
        if (status_code){
            free(*status_code);
            *status_code = strdup("400 Bad Request");
        } 
        return strdup("<html><body><h1>Error: Missing relative_path.</h1></body></html>");
    }
    // if (filename == NULL || strlen(filename) == 0) {
    //     fprintf(stderr, "Error: filename is missing in the request body.\n");
    //     return strdup("<html><body><h1>Error: Missing filename.</h1></body></html>");
    // }
    // if (is_file(filename) == 0) {
    //     // it's not a file
    //     return strdup("<html><body><h1>Error: This isn't a file.</h1></body></html>");
    // }

    FileMetadata metadata = {0};
    snprintf(metadata.user_id, sizeof(metadata.user_id), "%s", user_id);
    snprintf(metadata.filename, sizeof(metadata.filename), "%s", filename);
    metadata.relative_path = strdup(relative_path);
    if (metadata.relative_path == NULL) {
        fprintf(stderr, "Error: Memory allocation failed for relative_path.\n");
        if (status_code){
            free(*status_code);
            *status_code = strdup("500 Internal Server Error");
        } 
        return strdup("<html><body><h1>Error: Internal server error.</h1></body></html>");
    }

    bool delete_success = delete_file(&metadata);

    free(metadata.relative_path);

    if (delete_success) {
        return strdup("<html><body><h1>File deleted successfully.</h1></body></html>");
    } else {
        fprintf(stderr, "Error: Failed to delete file at relative_path: %s.\n", relative_path);
        if (status_code){
            free(*status_code);
            *status_code = strdup("500 Internal Server Error");
        } 
        return strdup("<html><body><h1>Error: Failed to delete file.</h1></body></html>");
    }
}


char* handle_delete_folder_request(const char *user_id, const char *relative_path, const char* foldername,char **status_code) {
    if (user_id == NULL || strlen(user_id) == 0) {
        fprintf(stderr, "Error: user_id is missing in the request body.\n");
        if (status_code){
            free(*status_code);
            *status_code = strdup("400 Bad Request");
        } 
        return strdup("<html><body><h1>Error: Missing user_id.</h1></body></html>");
    }
    if (relative_path == NULL || strlen(relative_path) == 0) {
        fprintf(stderr, "Error: relative_path is missing in the request body.\n");
        if (status_code){
            free(*status_code);
            *status_code = strdup("400 Bad Request");
        } 
        return strdup("<html><body><h1>Error: Missing relative_path.</h1></body></html>");
    }
    // if (strlen(foldername) == 0) {
    //     fprintf(stderr, "Warning: foldername is missing in the request body.\n");
    // }
    // if (is_file(foldername) == 1) {
    //     return strdup("<html><body><h1>Error: This isn't a folder, so you can't call delete_folder .</h1></body></html>");
    // }
    FileMetadata metadata = {0};
    snprintf(metadata.user_id, sizeof(metadata.user_id), "%s", user_id);
    metadata.relative_path = strdup(relative_path);
    if (metadata.relative_path == NULL) {
        fprintf(stderr, "Error: Memory allocation failed for relative_path.\n");
        if (status_code){
            free(*status_code);
            *status_code = strdup("500 Internal Server Error");
        } 
        return strdup("<html><body><h1>Error: Internal server error.</h1></body></html>");
    }
    snprintf(metadata.foldername, sizeof(metadata.foldername), "%s", foldername);

    bool delete_success = delete_folder(&metadata);

    free(metadata.relative_path);

    if (delete_success) {
        return strdup("<html><body><h1>Folder deleted successfully.</h1></body></html>");
    } else {
        fprintf(stderr, "Error: Failed to delete folder at relative_path: %s.\n", relative_path);
        if (status_code){
            free(*status_code);
            *status_code = strdup("500 Internal Server Error");
        } 
        return strdup("<html><body><h1>Error: Failed to delete folder.</h1></body></html>");
    }
}

char* handle_create_folder_request(const char *user_id, const char *relative_path, const char *foldername,char **status_code) {
    if (user_id == NULL || strlen(user_id) == 0) {
        fprintf(stderr, "Error: user_id is missing in the request body.\n");
        if (status_code){
            free(*status_code);
            *status_code = strdup("400 Bad Request");
        } 
        return strdup("<html><body><h1>Error: Missing user_id.</h1></body></html>");
    }
    if (relative_path == NULL || strlen(relative_path) == 0) {
        fprintf(stderr, "Error: relative_path is missing in the request body.\n");
        if (status_code){
            free(*status_code);
            *status_code = strdup("400 Bad Request");
        } 
        return strdup("<html><body><h1>Error: Missing relative_path.</h1></body></html>");
    }
    if (foldername == NULL || strlen(foldername) == 0) {
        fprintf(stderr, "Error: foldername is missing in the request body.\n");
        if (status_code){
            free(*status_code);
            *status_code = strdup("400 Bad Request");
        } 
        return strdup("<html><body><h1>Error: Missing foldername.</h1></body></html>");
    }

    FileMetadata metadata = {0};
    snprintf(metadata.user_id, sizeof(metadata.user_id), "%s", user_id);
    snprintf(metadata.foldername, sizeof(metadata.foldername), "%s", foldername);

    metadata.relative_path = strdup(relative_path);
    if (metadata.relative_path == NULL) {
        fprintf(stderr, "Error: Memory allocation failed for relative_path.\n");
         if (status_code){
            free(*status_code);
            *status_code = strdup("500 Internal Server Error");
        } 
        return strdup("<html><body><h1>Error: Internal server error.</h1></body></html>");
    }

    bool create_success = create_folder(&metadata);

    free(metadata.relative_path);

    if (create_success) {
        return strdup("<html><body><h1>Folder created successfully.</h1></body></html>");
    } else {
        fprintf(stderr, "Error: Failed to create folder '%s' at relative_path '%s'.\n", foldername, relative_path);
        if (status_code){
            free(*status_code);
            *status_code = strdup("500 Internal Server Error");
        } 
        return strdup("<html><body><h1>Error: Failed to create folder. Please check the inputs and try again.</h1></body></html>");
    }
}

char* handle_rename_request(const char *user_id, const char *relative_path, const char *new_name,char **status_code) {
    if (user_id == NULL || strlen(user_id) == 0) {
        fprintf(stderr, "Error: user_id is missing in the request body.\n");
          if (status_code){
            free(*status_code);
            *status_code = strdup("400 Bad Request");
        } 
        return strdup("<html><body><h1>Error: Missing user_id.</h1></body></html>");
    }
    if (relative_path == NULL || strlen(relative_path) == 0) {
        fprintf(stderr, "Error: relative_path is missing in the request body.\n");
          if (status_code){
            free(*status_code);
            *status_code = strdup("400 Bad Request");
        } 
        return strdup("<html><body><h1>Error: Missing relative_path.</h1></body></html>");
    }
    if (new_name == NULL || strlen(new_name) == 0) {
        fprintf(stderr, "Error: new_name is missing in the request body.\n");
          if (status_code){
            free(*status_code);
            *status_code = strdup("400 Bad Request");
        } 
        return strdup("<html><body><h1>Error: Missing new_name.</h1></body></html>");
    }

    FileMetadata metadata = {0};
    snprintf(metadata.user_id, sizeof(metadata.user_id), "%s", user_id);

    metadata.relative_path = strdup(relative_path);
    if (metadata.relative_path == NULL) {
        fprintf(stderr, "Error: Memory allocation failed for relative_path.\n");
         if (status_code){
            free(*status_code);
            *status_code = strdup("500 Internal Server Error");
        } 
        return strdup("<html><body><h1>Error: Internal server error.</h1></body></html>");
    }

    bool rename_success = rename(&metadata, new_name);

    free(metadata.relative_path);

    if (rename_success) {
        return strdup("<html><body><h1>Rename successful.</h1></body></html>");
    } else {
        fprintf(stderr, "Error: Failed to rename '%s' to '%s'.\n", relative_path, new_name);
        if (status_code){
            free(*status_code);
            *status_code = strdup("500 Internal Server Error");
        } 
        return strdup("<html><body><h1>Error: Failed to rename. Please check the inputs and try again.</h1></body></html>");
    }
}

char* handle_move_request(const char *user_id, const char *relative_path, char *new_path ,char **status_code) {
    if (user_id == NULL || strlen(user_id) == 0) {
        fprintf(stderr, "Error: user_id is missing in the request body.\n");
        if (status_code){
            free(*status_code);
            *status_code = strdup("400 Bad Request");
        } 
        return strdup("<html><body><h1>Error: Missing user_id.</h1></body></html>");
    }
    if (relative_path == NULL || strlen(relative_path) == 0) {
        fprintf(stderr, "Error: relative_path is missing in the request body.\n");
        if (status_code){
            free(*status_code);
            *status_code = strdup("400 Bad Request");
        } 
        return strdup("<html><body><h1>Error: Missing relative_path.</h1></body></html>");
    }
    if (new_path == NULL || strlen(new_path) == 0) {
        fprintf(stderr, "Error: new_path is missing in the request body.\n");
        if (status_code){
            free(*status_code);
            *status_code = strdup("400 Bad Request");
        } 
        return strdup("<html><body><h1>Error: Missing new_path.</h1></body></html>");
    }

    FileMetadata metadata = {0};
    snprintf(metadata.user_id, sizeof(metadata.user_id), "%s", user_id);

    metadata.relative_path = strdup(relative_path);
    if (metadata.relative_path == NULL) {
        fprintf(stderr, "Error: Memory allocation failed for relative_path.\n");
        if (status_code){
            free(*status_code);
            *status_code = strdup("500 Internal Server Error");
        } 
        return strdup("<html><body><h1>Error: Internal server error.</h1></body></html>");
    }
    bool move_success = move(&metadata, new_path);

    free(metadata.relative_path);

    if (move_success) {
        return strdup("<html><body><h1>Move successful.</h1></body></html>");
    } else {
        fprintf(stderr, "Error: Failed to move '%s' to '%s'.\n", relative_path, new_path);
        if (status_code){
            free(*status_code);
            *status_code = strdup("500 Internal Server Error");
        } 
        return strdup("<html><body><h1>Error: Failed to move file or folder. Please check the inputs and try again.</h1></body></html>");
    }
}


void print_entry_list(EntryNode *head) {
    if (head == EMPTY_ENTRY_LIST ) { 
        printf("Entry list is empty.\n");
        return;
    }
    printf("Entry list:\n");
    EntryNode *current = head;
    while (current != NULL) {
        printf("  - Name: %s\n", current->name);
        printf("    Type: %s\n", current->type);
        current = current->next;
    }
}







