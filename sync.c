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

/*!
 * @brief synchronize is the main function for synchronization
 * It will build the lists (source and destination), then make a third list with differences, and apply differences to the destination
 * It must adapt to the parallel or not operation of the program.
 * @param the_config is a pointer to the configuration
 * @param p_context is a pointer to the processes context
 */
void synchronize(configuration_t *the_config, process_context_t *p_context) {
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

        while ((strcpy(char1, fgetc(file1))) != EOF && (strcpy(char2,fgetc(file2))) != EOF) {
            if (strcmp(char1,char2) != 0) {
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
    DIR *dir;
    struct dirent *entry;

    if ((dir = opendir(target_path)) == NULL) {
        perror("Unable to open directory");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        char path[PATH_SIZE];
        if (concat_path(path, target_path, entry->d_name) == NULL) {
            perror("Unable to concatenate path");
            continue;
        }  

        files_list_entry_t *new_entry = malloc(sizeof(files_list_entry_t));
        if (new_entry == NULL) {
            perror("Unable to allocate memory for new file entry");
            continue;
        }

        if (add_file_entry(list, path) != 0) {
            perror("Unable to add file entry to list");
        }
    }

    closedir(dir);
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
        char path[PATH_SIZE];
        concat_path(path, target, entry->d_name);
        add_file_entry(list, path);
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
