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
    if (!the_config->is_parallel){
        files_list_t *source = (files_list_t *) malloc(sizeof(files_list_t));
        source->head = NULL;
        source->tail = NULL;
        make_files_list(source, the_config->source);

        files_list_t *destination = (files_list_t *) malloc(sizeof(files_list_t));
        destination->head = NULL;
        destination->tail = NULL;
        make_files_list(destination, the_config->destination);

        files_list_t *difference = (files_list_t *) malloc(sizeof(files_list_t));
        difference->head = NULL;
        difference->tail = NULL;

        files_list_entry_t *tmp = source->head;
        files_list_entry_t *result;
        while (tmp != NULL){
            result = find_entry_by_name(destination, tmp->path_and_name, strlen(the_config->source), strlen(the_config->destination));
            if (tmp == NULL){
                add_entry_to_tail(difference, tmp);
            }
            else{
                if (mismatch(tmp, result, true)){
                    add_entry_to_tail(difference, tmp);
                }
            }
            tmp = tmp->next;
        }

        files_list_entry_t *tmp_dif = difference->head;
        while (tmp_dif != NULL){
            add_entry_to_tail(destination, tmp_dif);
            tmp_dif = tmp_dif->next;
        }
    }
}

/*!
 * @brief mismatch tests if two files with the same name (one in source, one in destination) are equal
 * @param lhd a files list entry from the source
 * @param rhd a files list entry from the destination
 * @has_md5 a value to enable or disable MD5 sum check
 * @return true if both files are not equal, false else
 */
bool mismatch(files_list_entry_t *lhd, files_list_entry_t *rhd, bool has_md5) {
    // Ici, EOF = end of file, caractèr marquant la fin d'un fichier
    if (has_md5) {
        for (int i = 0; i < 16; ++i) {
            if (lhd->md5sum[i] != rhd->md5sum[i]) {
                return true;
            }
        }
    } else {
        FILE *file1 = fopen(lhd->path_and_name, "rb");
        FILE *file2 = fopen(rhd->path_and_name, "rb");

        if (file1 == NULL || file2 == NULL) {
            if (file1) fclose(file1);
            if (file2) fclose(file2);
            fprintf(stderr, "[MISMATCH TEST] : un des 2 fichier n'a pas pu être ouvert\n");
            exit(-1);
        }
        char char1, char2;

        while ((char1 = fgetc(file1)) != EOF && (char2 = fgetc(file2)) != EOF) {
            if (char1 != char2) {
                return true;
                break; // sort de la fonction lors de la détection d'une différence
            }
        }
        // Si la longueur des fichiers est différente
        if ((char1 == EOF && char2 != EOF) || (char1 != EOF && char2 == EOF)) {
            return true;
        }
        fclose(file1);
        fclose(file2);
    }
    return false;
}

/*!
 * @brief make_files_list buils a files list in no parallel mode
 * @param list is a pointer to the list that will be built
 * @param target_path is the path whose files to list
 */
void make_files_list(files_list_t *list, char *target_path) {

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
}

/*!
 * @brief copy_entry_to_destination copies a file from the source to the destination
 * It keeps access modes and mtime (@see utimensat)
 * Pay attention to the path so that the prefixes are not repeated from the source to the destination
 * Use sendfile to copy the file, mkdir to create the directory
 */
void copy_entry_to_destination(files_list_entry_t *source_entry, configuration_t *the_config) {
    char source[1024];
    strcpy(source, the_config->source);
    char destination[1024];
    strcpy(source, the_config->destination);

    if (source_entry->entry_type == DOSSIER){
        char path[PATH_SIZE];
        concat_path(path, destination, source_entry->path_and_name + strlen(the_config->source));
        mkdir(path, source_entry->mode);
    }

    else{
        off_t offset = 0;
        char source_file[PATH_SIZE];
        char destination_file[PATH_SIZE];
        concat_path(source_file, source, source_entry->path_and_name);
        concat_path(destination_file, destination, source_entry->path_and_name + strlen(the_config->source));

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
    DIR *directory = open_dir(target);
    if (directory == NULL){
        return;
    }
    struct dirent *entry = get_next_entry(directory);
    while (entry != NULL){
        char full_path[PATH_SIZE];
        concat_path(full_path, target, entry->d_name);
        add_file_entry(list, full_path);
        entry = get_next_entry(directory);
    } 
    closedir(directory);
    return;
}

/*!
 * @brief open_dir opens a dir
 * @param path is the path to the dir
 * @return a pointer to a dir, NULL if it cannot be opened
 */
DIR *open_dir(char *path) {
    DIR *d = opendir(path);
    return d;
}

/*!
 * @brief get_next_entry returns the next entry in an already opened dir
 * @param dir is a pointer to the dir (as a result of opendir, @see open_dir)
 * @return a struct dirent pointer to the next relevant entry, NULL if none found (use it to stop iterating)
 * Relevant entries are all regular files and dir, except . and ..
 */
struct dirent *get_next_entry(DIR *dir) {
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            if (entry->d_type == 4 || entry->d_type == 8) {
                return entry;
            }
            printf("No relevent entry found");
        }
    }
    return NULL; // No relevant entry found
}
