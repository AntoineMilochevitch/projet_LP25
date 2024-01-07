#include "sync.h"
#include <dirent.h>
#include <string.h>
#include "processes.h"
#include "utility.h"
#include "messages.h"
#include "file-properties.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <unistd.h>
#include <sys/msg.h>
#include <stdio.h>
#include <stdlib.h>


/*!
 * @brief synchronize is the main function for synchronization
 * It will build the lists (source and destination), then make a third list with differences, and apply differences to the destination
 * It must adapt to the parallel or not operation of the program.
 * @param the_config is a pointer to the configuration
 * @param p_context is a pointer to the processes context
 */
void synchronize(configuration_t *the_config, process_context_t *p_context) {
    if (p_context == NULL) {
        fprintf(stderr, "Invalid arguments to synchronize\n");
        exit(-1);
    }
    if (the_config->verbose || the_config->dry_run) {
        printf("Synchronizing %s and %s\n", the_config->source, the_config->destination);
    }
    files_list_t source = {NULL, NULL};
     files_list_t destination = {NULL, NULL};
    if (!the_config->is_parallel) {
        make_files_list(&source, the_config->source);
        make_files_list(&destination, the_config->destination);
    } else {
        make_files_lists_parallel(&source, &destination, the_config, p_context->message_queue_id);
    }
    if (the_config->verbose || the_config->dry_run) {
            printf("\nSource files:\n");
            display_files_list(&source);
            printf("\nDestination files:\n");
            display_files_list(&destination);
        }

    files_list_t difference = {NULL, NULL};
    files_list_entry_t *tmp = source.head;
    files_list_entry_t *result;
    while (tmp != NULL){
        size_t start_of_src = strlen(the_config->source) + 1;
        size_t start_of_dest = strlen(the_config->destination) + 1;
        if (the_config->verbose || the_config->dry_run) {
            printf("\nComparing %s and %s\n", tmp->path_and_name, destination.head->path_and_name);
        }
        result = find_entry_by_name(&destination, tmp->path_and_name, start_of_src, start_of_dest);
        if (result == NULL){
            if (the_config->verbose || the_config->dry_run) {
                printf("\nDifferent, adding %s to the list of files to copy\n", tmp->path_and_name + start_of_src);
            }
            files_list_entry_t *tmp_copy = malloc(sizeof(files_list_entry_t));
            if (tmp_copy == NULL) {
                fprintf(stderr, "Failed to allocate memory for tmp_copy\n");
                exit(-1);
            }
            memcpy(tmp_copy, tmp, sizeof(files_list_entry_t));
            tmp_copy->next = NULL;
            printf("tmp_copy : %s\n", tmp_copy->path_and_name);
            add_entry_to_tail(&difference, tmp_copy);
        } else {
            if (the_config->verbose || the_config->dry_run) {
                printf("\nSame %s\n", result->path_and_name + start_of_dest);
            }
            if (mismatch(tmp, result, the_config->uses_md5)) {
                if (the_config->verbose || the_config->dry_run) {
                    printf("\nFiles are different, adding %s to the list of files to copy\n", tmp->path_and_name + start_of_src);
                }
                files_list_entry_t *tmp_copy = malloc(sizeof(files_list_entry_t));
                if (tmp_copy == NULL) {
                    fprintf(stderr, "Failed to allocate memory for tmp_copy\n");
                    exit(-1);
                }
                memcpy(tmp_copy, tmp, sizeof(files_list_entry_t));
                tmp_copy->next = NULL;
                printf("tmp_copy : %s\n", tmp_copy->path_and_name);
                add_entry_to_tail(&difference, tmp_copy);
            }
        }
        tmp = tmp->next;
    }
    if (the_config->verbose || the_config->dry_run) {
        printf("\nFiles to be copied:\n");
        display_files_list(&difference);
    }
    files_list_entry_t *tmp_dif = difference.head;
    while (tmp_dif != NULL) {
        if (the_config->dry_run) {
            printf("\nWould copy %s\n", tmp_dif->path_and_name);
        } else {
            copy_entry_to_destination(tmp_dif, the_config);
        }
        tmp_dif = tmp_dif->next;   
    } 
    clear_files_list(&difference);
    clear_files_list(&source);
    clear_files_list(&destination);
}



/*!
 * @brief mismatch tests if two files with the same name (one in source, one in destination) are equal
 * @param lhd a files list entry from the source
 * @param rhd a files list entry from the destination
 * @has_md5 a value to enable or disable MD5 sum check
 * @return true if both files are not equal, false else
 */
bool mismatch(files_list_entry_t *lhd, files_list_entry_t *rhd, bool has_md5) {
    // printf("Comparing %s and %s\n", lhd->path_and_name, rhd->path_and_name); debug
    // Ici, EOF = end of file, caractèr marquant la fin d'un fichier
    if (lhd == NULL || rhd == NULL || lhd->path_and_name == NULL || rhd->path_and_name == NULL) {
        fprintf(stderr, "Invalid arguments to mismatch\n");
        exit(-1);
    }

    if (has_md5) {
        if (lhd->md5sum == NULL || rhd->md5sum == NULL) {
            fprintf(stderr, "MD5 sum not available\n");
            exit(-1);
        }
        for (int i = 0; i < 16; ++i) {
            if (lhd->md5sum[i] != rhd->md5sum[i]) {
                return true;
            }
        }
    }
    else{ // date and size only (no md5 : uses_md5 = false in config)
        if(lhd->mtime.tv_sec == rhd->mtime.tv_sec && lhd->mtime.tv_nsec == rhd->mtime.tv_nsec && lhd->size == rhd->size) {
            return true;
        }
    }
    return false;
}

/*!
 * @brief make_files_list buils a files list in no parallel mode
 * @param list is a pointer to the list that will be built
 * @param target_path is the path whose files to list
 */
void make_files_list(files_list_t *list, char *target_path) {
    // printf("Making files list for %s\n", target_path); debug
    if (list == NULL || target_path == NULL) {
        fprintf(stderr, "Invalid arguments to make_files_list\n");
        exit(-1);
    }
    make_list(list, target_path);

    files_list_entry_t *cursor = list->head;
    while (cursor != NULL) {
        get_file_stats(cursor); 
        cursor = cursor->next;
    }
}

/*!
 * @brief make_files_lists_parallel makes both (src and dest) files list with parallel processing
 * @param src_list is a pointer to the source list to build
 * @param dst_list is a pointer to the destination list to build
 * @param the_config is a pointer to the program configuration
 * @param msg_queue is the id of the MQ used for communication
 */
void make_files_lists_parallel(files_list_t *src_list, files_list_t *dst_list, configuration_t *the_config, int msg_queue) {
    if (src_list == NULL || dst_list == NULL || the_config == NULL) {
        fprintf(stderr, "Invalid arguments to make_files_lists_parallel\n");
        exit(-1);
    }
    printf("msg_queue %d\n", msg_queue);
    printf("Making files lists in parallel\n");
    send_analyze_dir_command(msg_queue, MSG_TYPE_TO_SOURCE_LISTER, the_config->source);
    send_analyze_dir_command(msg_queue, MSG_TYPE_TO_DESTINATION_LISTER, the_config->destination);
    printf("Sent analyze dir commands\n");
    bool source_loop = true;
    bool destination_loop = true;
    files_list_entry_transmit_t source_response;
    simple_command_t src_end; 
    files_list_entry_transmit_t destination_response;
    simple_command_t dst_end;
    while (source_loop || destination_loop){
        printf("Waiting for messages\n");
        if (msgrcv(msg_queue, &source_response, sizeof(analyze_file_command_t) - sizeof(long), MSG_TYPE_TO_MAIN_FROM_SOURCE_LISTER, 0) != -1){
            if (the_config->verbose || the_config->dry_run) {
                printf("Received source response\n");
            }
            files_list_entry_t *tmp_copy = malloc(sizeof(files_list_entry_t));
            if (tmp_copy == NULL) {
                fprintf(stderr, "Failed to allocate memory for tmp_copy\n");
                exit(-1);
            }
            memcpy(tmp_copy, &source_response.payload, sizeof(files_list_entry_t));
            add_entry_to_tail(src_list, tmp_copy);
        }
        if (msgrcv(msg_queue, &destination_response, sizeof(analyze_file_command_t) - sizeof(long), MSG_TYPE_TO_MAIN_FROM_DESTINATION_LISTER, 0) != -1){
            if (the_config->verbose || the_config->dry_run) {
                printf("Received destination response\n");
            }
            files_list_entry_t *tmp_copy = malloc(sizeof(files_list_entry_t));
            if (tmp_copy == NULL) {
                fprintf(stderr, "Failed to allocate memory for tmp_copy\n");
                exit(-1);
            }
            memcpy(tmp_copy, &destination_response.payload, sizeof(files_list_entry_t));
            add_entry_to_tail(dst_list, tmp_copy);
        }
        if (msgrcv(msg_queue, &src_end, sizeof(char), MSG_TYPE_TO_MAIN_FROM_END_SRC_LISTER, 0) != -1){
            if (the_config->verbose || the_config->dry_run) {
                printf("Received source end\n");
            }
            if (src_end.message == COMMAND_CODE_LIST_COMPLETE){
                printf("Source end\n");
                source_loop = false;
            }
        }
        if (msgrcv(msg_queue, &dst_end, sizeof(char), MSG_TYPE_TO_MAIN_FROM_END_DEST_LISTER, 0) != -1){
            if (the_config->verbose || the_config->dry_run) {
                printf("Received destination end\n");
            }
            if (src_end.message == COMMAND_CODE_LIST_COMPLETE){
                printf("Destination end\n");
                destination_loop = false;
            }
        }
    }
}

/*!
 * @brief copy_entry_to_destination copies a file from the source to the destination
 * It keeps access modes and mtime (@see utimensat)
 * Pay attention to the path so that the prefixes are not repeated from the source to the destination
 * Use sendfile to copy the file, mkdir to create the directory
 */
void copy_entry_to_destination(files_list_entry_t *source_entry, configuration_t *the_config) {
    if (the_config->verbose || the_config->dry_run) {
        printf("Copying %s to %s\n", source_entry->path_and_name, the_config->destination); 
    }
    if (source_entry == NULL || the_config == NULL) {
        fprintf(stderr, "Invalid arguments to copy_entry_to_destination\n");
        exit(-1);
    }
    char source[1024];
    strcpy(source, the_config->source);
    char destination[1024];
    strcpy(destination, the_config->destination);
    // printf("Source : %s\n", source); debug
    // printf("Destination : %s\n", destination); debug

    if (source_entry->entry_type == DOSSIER) {
        char path[PATH_SIZE];
        if (the_config->verbose || the_config->dry_run)
        printf("Creating directory %s\n", source_entry->path_and_name + strlen(the_config->source) + 1);
        concat_path(path, destination, source_entry->path_and_name + strlen(the_config->source) + 1);
        mkdir(path, source_entry->mode);
    } else {
        off_t offset = 0;
        char source_file[PATH_SIZE];
        char destination_file[PATH_SIZE];
        if (the_config->verbose || the_config->dry_run)
        printf("Copying file %s\n", source_entry->path_and_name + strlen(the_config->source) + 1); 
        concat_path(source_file, source, source_entry->path_and_name);
        concat_path(destination_file, destination, source_entry->path_and_name + strlen(the_config->source) + 1);

        int fd_source, fd_destination;
        fd_source = open(source_entry->path_and_name, O_RDONLY);
        fd_destination = open(destination_file, O_WRONLY | O_CREAT | O_TRUNC, source_entry->mode);
        sendfile(fd_destination, fd_source, &offset, source_entry->size);

        close(fd_source);
        close(fd_destination);
    }
    return;
}

/*!
 * @brief make_list lists files in a location (it recurses in directories)
 * It doesn't get files properties, only a list of paths
 * This function is used by make_files_list and make_files_list_parallel
 * @param list is a pointer to the list that will be built
 * @param target is the target dir whose content must be listed
 */
void make_list(files_list_t *list, char *target) {
    if (list == NULL || target == NULL) {
        fprintf(stderr, "Invalid arguments to make_list\n");
        exit(-1);
    }
    DIR *dir;
    if (!(dir = open_dir(target))) {
        return;
    }

    struct dirent *entry;
    while ((entry = get_next_entry(dir)) != NULL) {
        char full_path[PATH_SIZE];
        concat_path(full_path, target, entry->d_name);

        if (entry->d_type == DT_DIR) {
            add_file_entry(list, full_path);
            make_list(list, full_path);
        } else {
            add_file_entry(list, full_path);
        }
    }
    closedir(dir);
}

/*!
 * @brief open_dir opens a dir
 * @param path is the path to the dir
 * @return a pointer to a dir, NULL if it cannot be opened
 */
DIR *open_dir(char *path) {
    DIR *d = opendir(path);
    if (d == NULL) {
        perror("Failed to open directory");
    }
    return d;
}

/*!
 * @brief get_next_entry returns the next entry in an already opened dir
 * @param dir is a pointer to the dir (as a result of opendir, @see open_dir)
 * @return a struct dirent pointer to the next relevant entry, NULL if none found (use it to stop iterating)
 * Relevant entries are all regular files and dir, except . and ..
 */
struct dirent *get_next_entry(DIR *dir) {
    // printf("Getting next entry\n"); debug
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            if (entry->d_type == DT_DIR || entry->d_type == DT_REG) {
                return entry;
            }
            printf("No relevent entry found");
        }
    }
    return NULL; // No relevant entry found
}
