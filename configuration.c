#include "configuration.h"
#include <stddef.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>

typedef enum {DATE_SIZE_ONLY, NO_PARALLEL} long_opt_values;

/*!
 * @brief function display_help displays a brief manual for the program usage
 * @param my_name is the name of the binary file
 * This function is provided with its code, you don't have to implement nor modify it.
 */
void display_help(char *my_name) {
    printf("%s [options] source_dir destination_dir\n", my_name);
    printf("Options: \t-n <processes count>\tnumber of processes for file calculations\n");
    printf("         \t-h display help (this text)\n");
    printf("         \t--date_size_only disables MD5 calculation for files\n");
    printf("         \t--no-parallel disables parallel computing (cancels values of option -n)\n");
}

/*!
 * @brief init_configuration initializes the configuration with default values
 * @param the_config is a pointer to the configuration to be initialized
 */
void init_configuration(configuration_t *the_config) {
    if (the_config == NULL) {
        return;
    }
    the_config->source[0] = '\0';
    the_config->destination[0] = '\0'; 
    the_config->processes_count = 1;
    the_config->is_parallel = true;
    the_config->uses_md5 = true;
    the_config->verbose = false;
    the_config->dry_run = false;
}

/*!
 * @brief set_configuration updates a configuration based on options and parameters passed to the program CLI
 * @param the_config is a pointer to the configuration to update
 * @param argc is the number of arguments to be processed
 * @param argv is an array of strings with the program parameters
 * @return -1 if configuration cannot succeed, 0 when ok
 */
int set_configuration(configuration_t *the_config, int argc, char *argv[]) {
    printf("Setting configuration\n");
    int opt;
    struct option long_options[] = {
        {"date-size-only", no_argument,       0, 'd'},
        {"no-parallel",    no_argument,       0, 'p'},
        {"dry-run",        no_argument,       0, 'r'},
        {"verbose",        no_argument,       0, 'v'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "dpvrn:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'd':
                the_config->uses_md5 = false;
                break;
            case 'p':
                the_config->is_parallel = false;
                break;
            case 'r':
                the_config->dry_run = true;
                break;
            case 'v':
                the_config->verbose = true;
                break;
            case 'n':
                the_config->processes_count = atoi(optarg);
                break;
            default:
                return -1;
        }
    }

    // Copy remaining arguments to source and destination
    if (optind < argc) {
        strncpy(the_config->source, argv[optind++], sizeof(the_config->source));
        if (optind < argc) {
            strncpy(the_config->destination, argv[optind++], sizeof(the_config->destination));
        }
    }
    return 0;
}