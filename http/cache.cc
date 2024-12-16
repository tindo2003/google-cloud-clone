#include "cache.h"
#include <ctype.h>

// Global user cache head
UserCache* user_cache_head = NULL;

// Find user cache by user_id
UserCache* find_user_cache(const char* user_id) {
    UserCache* current = user_cache_head;
    while (current != NULL) {
        if (strcmp(current->user_id, user_id) == 0) {
            current->last_access = time(NULL);  // Update last access time
            return current;
        }
        current = current->next;
    }
    return NULL;
}

// Add a new user cache
UserCache* add_user_cache(const char* user_id) {
    UserCache* new_cache = (UserCache*)malloc(sizeof(UserCache));
    if (new_cache == NULL) {
        fprintf(stderr, "[add_user_cache] Memory allocation failed.\n");
        return NULL;
    }
    strncpy(new_cache->user_id, user_id, sizeof(new_cache->user_id) - 1);
    new_cache->user_id[sizeof(new_cache->user_id) - 1] = '\0';
    new_cache->last_access = time(NULL);
    memset(&new_cache->root_dir, 0, sizeof(DirCache));
    strncpy(new_cache->root_dir.dir_id, "root", sizeof(new_cache->root_dir.dir_id) - 1);
    new_cache->root_dir.dir_id[sizeof(new_cache->root_dir.dir_id) - 1] = '\0';
    new_cache->root_dir.name[0] = '\0';
    new_cache->root_dir.subdirs = NULL;
    new_cache->root_dir.next = NULL;

    // Add to the head of the list
    new_cache->next = user_cache_head;
    user_cache_head = new_cache;

    return new_cache;
}

// Delete user cache
void delete_user_cache(UserCache* user_cache) {
    if (user_cache == NULL) {
        return;
    }

    // Remove from the linked list
    if (user_cache_head == user_cache) {
        user_cache_head = user_cache->next;
    } else {
        UserCache* current = user_cache_head;
        while (current != NULL && current->next != user_cache) {
            current = current->next;
        }
        if (current != NULL) {
            current->next = user_cache->next;
        }
    }

    // Delete the directory cache tree
    delete_dir_cache_tree(&user_cache->root_dir);

    // Free the user cache
    free(user_cache);
}

// Clean expired caches
void clean_expired_caches() {
    UserCache* current = user_cache_head;
    UserCache* prev = NULL;
    time_t now = time(NULL);

    while (current != NULL) {
        if (difftime(now, current->last_access) > CACHE_EXPIRATION_TIME) {
            // Cache expired
            UserCache* to_delete = current;
            if (prev == NULL) {
                user_cache_head = current->next;
            } else {
                prev->next = current->next;
            }
            current = current->next;
            delete_user_cache(to_delete);
        } else {
            prev = current;
            current = current->next;
        }
    }
}

// Find directory cache by dir_id
DirCache* find_dir_by_id(DirCache* dir_cache, const char* dir_id) {
    if (dir_cache == NULL || dir_id == NULL) {
        return NULL;
    }

    if (strcmp(dir_cache->dir_id, dir_id) == 0) {
        return dir_cache;
    }

    // Search subdirectories
    DirCache* subdir = dir_cache->subdirs;
    while (subdir != NULL) {
        DirCache* found = find_dir_by_id(subdir, dir_id);
        if (found != NULL) {
            return found;
        }
        subdir = subdir->next;
    }

    return NULL;
}

// Get subdirectory by name
DirCache* get_subdir(DirCache* parent_dir, const char* subdir_name) {
    if (parent_dir == NULL || subdir_name == NULL) {
        printf("[get_subdir] Error: parent_dir or subdir_name is NULL.\n");
        return NULL;
    }
    DirCache* current = parent_dir->subdirs;
    while (current != NULL) {
        printf("[get_subdir] Comparing '%s' with '%s'.\n", current->name, subdir_name);
        if (strcmp(current->name, subdir_name) == 0) {
            printf("[get_subdir] Found subdir '%s'.\n", subdir_name);
            return current;
        }
        current = current->next;
    }
    printf("[get_subdir] Subdir '%s' not found under parent '%s'.\n", subdir_name, parent_dir->name);
    return NULL;
}

// Delete directory cache tree
void delete_dir_cache_tree(DirCache* dir_cache) {
    if (dir_cache == NULL) {
        return;
    }

    // Delete subdirectories recursively
    DirCache* subdir = dir_cache->subdirs;
    while (subdir != NULL) {
        DirCache* next_subdir = subdir->next;
        delete_dir_cache_tree(subdir);
        subdir = next_subdir;
    }

    // Free the directory itself if it's not the root
    if (strcmp(dir_cache->dir_id, "root") != 0) {
        free(dir_cache);
    }
}

DirCache* remove_subdir(DirCache* parent_dir, const char* subdir_name) {
    if (parent_dir == NULL || subdir_name == NULL) {
        printf("[remove_subdir] Error: parent_dir or subdir_name is NULL.\n");
        return NULL;
    }

    DirCache* prev = NULL;
    DirCache* current = parent_dir->subdirs;

    while (current != NULL) {
        printf("[remove_subdir] Checking subdir '%s' for removal.\n", current->name);
        if (strcmp(current->name, subdir_name) == 0) {
            // Found the subdir to remove
            if (prev == NULL) {
                // Removing the first subdir in the list
                parent_dir->subdirs = current->next;
            } else {
                prev->next = current->next;
            }
            printf("[remove_subdir] Removing subdir '%s' from parent '%s'.\n", subdir_name, parent_dir->name);
            current->next = NULL; 
            return current;
        }
        prev = current;
        current = current->next;
    }

    printf("[remove_subdir] Subdir '%s' not found under parent '%s'.\n", subdir_name, parent_dir->name);
    return NULL;
}

// Parse directory content
bool parse_directory_content(DirCache* parent_dir, const char* json_data) {
    if (parent_dir == NULL || json_data == NULL) {
        return false;
    }

    const char* current = json_data;

    // Skip leading whitespace
    while (*current && isspace((unsigned char)*current)) current++;

    // Check for opening '['
    if (*current != '[') {
        fprintf(stderr, "[parse_directory_content] Error: JSON data does not start with '['\n");
        return false;
    }
    current++;  // Skip '['

    // Loop through the array elements
    while (*current) {
        // Skip whitespace and commas
        while (*current && (isspace((unsigned char)*current) || *current == ',')) current++;
        if (*current == ']') {
            // End of array
            break;
        }

        if (*current != '{') {
            fprintf(stderr, "[parse_directory_content] Error: Expected '{' at position %ld\n", current - json_data);
            return false;
        }
        current++;  // Skip '{'

        // Parse key-value pairs
        char name[256] = {0};
        char type[16] = {0};
        char id[50] = {0};

        while (*current && *current != '}') {
            // Skip whitespace
            while (*current && isspace((unsigned char)*current)) current++;

            // Parse key
            if (*current != '"') {
                fprintf(stderr, "[parse_directory_content] Error: Expected '\"' at position %ld\n", current - json_data);
                return false;
            }
            current++;  // Skip '"'

            // Extract key
            const char* key_start = current;
            while (*current && *current != '"') current++;
            if (*current != '"') {
                fprintf(stderr, "[parse_directory_content] Error: Missing closing '\"' for key at position %ld\n", current - json_data);
                return false;
            }
            size_t key_len = current - key_start;
            char key[64];
            if (key_len >= sizeof(key)) key_len = sizeof(key) - 1;
            strncpy(key, key_start, key_len);
            key[key_len] = '\0';
            current++;  // Skip '"'

            // Skip whitespace and ':'
            while (*current && isspace((unsigned char)*current)) current++;
            if (*current != ':') {
                fprintf(stderr, "[parse_directory_content] Error: Expected ':' after key at position %ld\n", current - json_data);
                return false;
            }
            current++;  // Skip ':'

            // Skip whitespace
            while (*current && isspace((unsigned char)*current)) current++;

            // Parse value
            if (*current == '"') {
                current++;  // Skip '"'

                // Extract value
                const char* value_start = current;
                while (*current && *current != '"') current++;
                if (*current != '"') {
                    fprintf(stderr, "[parse_directory_content] Error: Missing closing '\"' for value at position %ld\n", current - json_data);
                    return false;
                }
                size_t value_len = current - value_start;
                char value[256];
                if (value_len >= sizeof(value)) value_len = sizeof(value) - 1;
                strncpy(value, value_start, value_len);
                value[value_len] = '\0';
                current++;  // Skip '"'

                // Assign value to the corresponding field
                if (strcmp(key, "name") == 0) {
                    strncpy(name, value, sizeof(name) - 1);
                } else if (strcmp(key, "type") == 0) {
                    strncpy(type, value, sizeof(type) - 1);
                } else if (strcmp(key, "id") == 0) {
                    strncpy(id, value, sizeof(id) - 1);
                }
            } else {
                // Handle non-string values if necessary
                fprintf(stderr, "[parse_directory_content] Error: Expected '\"' at position %ld\n", current - json_data);
                return false;
            }

            // Skip whitespace and comma
            while (*current && isspace((unsigned char)*current)) current++;
            if (*current == ',') {
                current++;  // Skip ','
            }
        }

        if (*current != '}') {
            fprintf(stderr, "[parse_directory_content] Error: Expected '}' at position %ld\n", current - json_data);
            return false;
        }
        current++;  // Skip '}'

        // If the type is "dir", add it to the cache
        if (strcmp(type, "dir") == 0) {
            // Check if the subdirectory already exists in the cache
            DirCache* existing_subdir = get_subdir(parent_dir, name);
            if (existing_subdir == NULL) {
                // Create a new DirCache for the subdirectory
                DirCache* new_dir = (DirCache*)malloc(sizeof(DirCache));
                if (new_dir == NULL) {
                    fprintf(stderr, "[parse_directory_content] Memory allocation failed.\n");
                    return false;
                }
                strncpy(new_dir->dir_id, id, sizeof(new_dir->dir_id) - 1);
                new_dir->dir_id[sizeof(new_dir->dir_id) - 1] = '\0';
                strncpy(new_dir->name, name, sizeof(new_dir->name) - 1);
                new_dir->name[sizeof(new_dir->name) - 1] = '\0';
                new_dir->subdirs = NULL;
                new_dir->next = NULL;

                // Add to the parent's subdirs linked list (insert at the beginning)
                new_dir->next = parent_dir->subdirs;
                parent_dir->subdirs = new_dir;
            }
        }

        // Skip whitespace
        while (*current && isspace((unsigned char)*current)) current++;

        // If the next character is ',', skip it
        if (*current == ',') {
            current++;  // Skip ','
            continue;
        } else if (*current == ']') {
            // End of array
            break;
        }
    }

    return true;
}

void print_user_cache() {
    UserCache* current_user = user_cache_head;
    printf("===== User Cache State =====\n");
    while (current_user != NULL) {
        printf("User ID: %s\n", current_user->user_id);
        // Print the root directory for this user
        print_dir_cache(&current_user->root_dir, 1);
        current_user = current_user->next;
    }
    printf("===== End of User Cache =====\n");
}

void print_dir_cache(DirCache* dir_cache, int indent_level) {
    if (dir_cache == NULL) {
        return;
    }

    // Indentation for better readability
    for (int i = 0; i < indent_level; i++) {
        printf("  ");
    }

    printf("Directory ID: %s, Name: %s\n", dir_cache->dir_id, dir_cache->name);

    // Print subdirectories
    DirCache* subdir = dir_cache->subdirs;
    while (subdir != NULL) {
        print_dir_cache(subdir, indent_level + 1);
        subdir = subdir->next;
    }
}
