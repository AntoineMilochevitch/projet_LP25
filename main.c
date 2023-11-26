#include <stdio.h>
#include <assert.h>
//#include <sync.h>
//#include <configuration.h>
#include "file-properties.h"
//#include <processes.h>
//#include <unistd.h>

/*!
 * @brief main function, calling all the mechanics of the program
 * @param argc its number of arguments, including its own name
 * @param argv the array of arguments
 * @return 0 in case of success, -1 else
 * Function is already provided with full implementation, you **shall not** modify it.
 */
int main(int argc, char *argv[]) {

    // Test de la somme md5 (fonctionne)
    /*files_list_entry_t file_entry = {
        .path_and_name = "../reponses",
        .mtime = { .tv_sec = 0, .tv_nsec = 0 },  // Replace with your modification time
        .size = 1024,  // Replace with the size of the file
        // .md5sum can be initialized as needed
        .entry_type = FICHIER,  // Replace with the appropriate file type
        .mode = 0644,  // Replace with the appropriate file permissions
        .next = NULL,
        .prev = NULL
    };

    printf("Path and Name: %s\n", file_entry.path_and_name);

    compute_file_md5(&file_entry);

    printf("MD5 Sum: ");
    for (int i = 0; i < 16; i++) {
        printf("%02x", file_entry.md5sum[i]);
    }
    printf("\n");*/

    // Check parameters:
    // - source and destination are provided
    // - source exists and can be read
    // - destination exists and can be written OR doesn't exist but can be created
    // - other options with getopt (see instructions)

    configuration_t my_config;
    init_configuration(&my_config);
    if (set_configuration(&my_config, argc, argv) == -1) {
        return -1;
    }

    // Check directories
    if (!directory_exists(my_config.source) || !directory_exists(my_config.destination)) {
        printf("Either source or destination directory do not exist\nAborting\n");
        return -1;
    }
    // Is destination writable?
    if (!is_directory_writable(my_config.destination)) {
        printf("Destination directory %s is not writable\n", my_config.destination);
        return -1;
    }

    // Prepare (fork, MQ) if parallel
    process_context_t processes_context;
    prepare(&my_config, &processes_context);

    // Run synchronize:
    synchronize(&my_config, &processes_context);
    
    // Clean resources
    clean_processes(&my_config, &processes_context);
 
    return 0;
}
