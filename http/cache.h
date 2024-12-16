#ifndef CACHE_H
#define CACHE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

// Define cache expiration time (e.g., 1800 seconds = 30 minutes)
#define CACHE_EXPIRATION_TIME 1800

// Forward declaration of DirCache
typedef struct DirCache DirCache;

// Structure to store directory cache
struct DirCache {
    char dir_id[50];         // Directory ID
    char name[256];          // Directory name (for subdirectories)
    DirCache *subdirs;       // Pointer to the first subdirectory
    DirCache *next;          // Pointer to the next directory in the linked list (for subdirs)
};

// Structure to store user cache
typedef struct UserCache {
    char user_id[50];
    time_t last_access;      // Last access time
    DirCache root_dir;       // Root directory cache
    struct UserCache *next;  // Pointer to the next user cache in the linked list
} UserCache;

// Function declarations

// User cache functions
UserCache* find_user_cache(const char* user_id);
UserCache* add_user_cache(const char* user_id);
void delete_user_cache(UserCache* user_cache);
void clean_expired_caches();

// Directory cache functions
DirCache* find_dir_by_id(DirCache* dir_cache, const char* dir_id);
DirCache* get_subdir(DirCache* parent_dir, const char* subdir_name);
void delete_dir_cache_tree(DirCache* dir_cache);
bool parse_directory_content(DirCache* parent_dir, const char* json_data);
DirCache* remove_subdir(DirCache* parent_dir, const char* subdir_name);
void print_user_cache();
void print_dir_cache(DirCache* dir_cache, int indent_level);
#endif 
