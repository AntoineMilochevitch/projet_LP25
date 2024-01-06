#include "processes.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/msg.h>
#include <stdio.h>
#include "messages.h"
#include "file-properties.h"
#include "sync.h"
#include <string.h>
#include <errno.h>

/*!
 * @brief prepare prepares (only when parallel is enabled) the processes used for the synchronization.
 * @param the_config is a pointer to the program configuration
 * @param p_context is a pointer to the program processes context
 * @return 0 if all went good, -1 else
 */
int prepare(configuration_t *the_config, process_context_t *p_context) {
    //The goal here is to modify p_context in order to use it in the synchronize function
    //If the parallel is disabled, the process context is unecessary

    //Process count 
    p_context->processes_count = the_config->processes_count;

    if (!the_config->is_parallel){
        printf("La configuration parallèle est désactivée.\n");
        return 0;
    } else {
        //Create lister for both dest & src 
        lister_configuration_t *lister_config_dest, *lister_config_src; 
        p_context->source_lister_pid = make_process(p_context, lister_process_loop, lister_config_src);
        p_context->destination_lister_pid = make_process(p_context, lister_process_loop, lister_config_dest);
        if (p_context->source_lister_pid == -1 || p_context->destination_lister_pid == -1) { // PID -1 = FAIL
            return -1;
        }

        // Create analyzer processes
        p_context->source_analyzers_pids = malloc(sizeof(pid_t) * the_config->processes_count);
        p_context->destination_analyzers_pids = malloc(sizeof(pid_t) * the_config->processes_count);
        for(int i = 0; i < the_config->processes_count; i++){
            analyzer_configuration_t *analyser_config_dest, *analyser_config_src; 
            p_context->source_analyzers_pids[i] = make_process(p_context, analyzer_process_loop, analyser_config_src);
            p_context->destination_analyzers_pids[i] = make_process(p_context, analyzer_process_loop, analyser_config_dest); 
            if(p_context->destination_analyzers_pids[i] == -1 || p_context->source_analyzers_pids[i] == -1){
                //Terminate both listers process
                kill(p_context->source_lister_pid, 0);
                kill(p_context->destination_lister_pid, 0);
                return -1;
            }
        }

        /* Ta fait quoi ici Mathis ?
        // Wait for both processes to initialize
        if (wait_for_processes(p_context) == -1) {
            // Terminate lister and analyzer processes
            kill(p_context->source_lister_pid, p_context);
            kill(p_context->source_analyzers_pids, p_context);
            return -1; // Failed to wait for processes
        }*/
    }
    return 0;
}

/*!
 * @brief make_process creates a process and returns its PID to the parent
 * @param p_context is a pointer to the processes context
 * @param func is the function executed by the new process
 * @param parameters is a pointer to the parameters of func
 * @return the PID of the child process (it never returns in the child process)
 */
int make_process(process_context_t *p_context, process_loop_t func, void *parameters) {
    pid_t pid = fork(); // Create a new process

    if (pid < 0) { // If fork() failed
        return -1;
    } else if (pid == 0) { // Child process
        func(parameters);
        exit(0);
    } else { // Parent process
        return pid;
    }
}

/*!
 * @brief lister_process_loop is the lister process function (@see make_process)
 * @param parameters is a pointer to its parameters, to be cast to a lister_configuration_t
 */
void lister_process_loop(lister_configuration_t *parameters) {
    int msg_q_id = msgget(parameters->mq_key, 0666 | IPC_CREAT);
    //je crois qu'il faut faire ça : 
    /*
    - récupérer noms des fichiers via les messages ?
    - les mettre dans une liste avec les commandes file-list.c ?
    - Envoyer le tout à l'analyser_process_loop via message ?
    - Une fois tout ça finit l'envoyer au main ?
    */
    //ne pas oublier de fermer les files de messages
    files_list_entry_transmit_t message; 
    //Send message to analyser via MQ ??
    msgsnd(msg_q_id, &message, sizeof(message) - sizeof(long), 0);
    

    //Create file list
    files_list_t l;
    l.head = NULL; 
    l.tail = NULL;




    //Once the list is created, send it to 
}

/*!
 * @brief analyzer_process_loop is the analyzer process function
 * @param parameters is a pointer to its parameters, to be cast to an analyzer_configuration_t
 */
void analyzer_process_loop(analyzer_configuration_t *parameters) {
    files_list_entry_transmit_t message;
    simple_command_t message_end;
    int msg_id = msgget(parameters->mq_key, 0666);
    int loop = 1;
    while (loop){
        if (msgrcv(msg_id, &message, sizeof(message) - sizeof(long), parameters->my_receiver_id, 0) != -1){
            if (message.op_code == COMMAND_CODE_ANALYZE_DIR){
                get_file_stats(&message.payload);
            }
            if (message.op_code == COMMAND_CODE_ANALYZE_FILE){
                get_file_stats(&message.payload);
                if (parameters->use_md5){
                    compute_file_md5(&message.payload);
                }
            }
            send_analyze_file_response(msg_id, parameters->my_recipient_id, &message.payload);
        }
        if (msgrcv(, &message_end, sizeof(message_end.message), MSG_TYPE_TO_SOURCE_ANALYZERS, 0) != -1){ // je sais pas quoi mettre dans le premier et le 4 destination ou source ?
            if (message_end.message == COMMAND_CODE_TERMINATE){
                loop = 0;
            }
        }
    }
}

/*!
 * @brief clean_processes cleans the processes by sending them a terminate command and waiting to the confirmation
 * @param the_config is a pointer to the program configuration
 * @param p_context is a pointer to the processes context
 */
void clean_processes(configuration_t *the_config, process_context_t *p_context) {
    // Do nothing if not parallel
    if (the_config->is_parallel){
        int loop_dl = 1;
        int loop_sl = 1;
        int loop_da = 1;
        int loop_sa = 1;
        simple_command_t message_end;

        // Send terminate
        send_terminate_command(p_context->message_queue_id, MSG_TYPE_TO_DESTINATION_LISTER); 
        send_terminate_command(p_context->message_queue_id, MSG_TYPE_TO_SOURCE_LISTER);
        send_terminate_command(p_context->message_queue_id, MSG_TYPE_TO_DESTINATION_ANALYZERS);
        send_terminate_command(p_context->message_queue_id, MSG_TYPE_TO_SOURCE_ANALYZERS);

        // Wait for responses
        while (loop_dl && loop_sl && loop_sa && loop_da){ 
            if (msgrcv(p_context->message_queue_id, &message_end, sizeof(message_end.message), MSG_TYPE_TO_DESTINATION_LISTER, 0) != -1){ 
                loop_dl = 0;
            }
            if (msgrcv(p_context->message_queue_id, &message_end, sizeof(message_end.message), MSG_TYPE_TO_SOURCE_LISTER, 0) != -1){ 
                loop_sl = 0;
            }
            if (msgrcv(p_context->message_queue_id, &message_end, sizeof(message_end.message), MSG_TYPE_TO_DESTINATION_ANALYZERS, 0) != -1){ 
                loop_da = 0;
            }
            if (msgrcv(p_context->message_queue_id, &message_end, sizeof(message_end.message), MSG_TYPE_TO_SOURCE_ANALYZERS, 0) != -1){ 
                loop_sa = 0;
            }
        }

        // Free allocated memory 
        free(p_context->source_analyzers_pids);
        free(p_context->destination_analyzers_pids);

        // Free the MQ
        msgctl(p_context->message_queue_id, IPC_RMID, NULL);
    }
}
