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
#include <signal.h>

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

    if (!the_config->is_parallel) {
        printf("La configuration parallèle est désactivée.\n");
        return 0;
    } else {
        //Create lister for both dest & src 
        p_context->shared_key = ftok("/bin", 25);
        if (p_context->shared_key == -1) {
            fprintf(stderr, "Erreur lors de la création de la clé partagée : %s\n", strerror(errno));
            return -1;
        }
        p_context->message_queue_id = msgget(p_context->shared_key, IPC_CREAT | 0666);
        lister_configuration_t lister_config_dest;
        lister_configuration_t lister_config_src;
        lister_config_src.analyzers_count = the_config->processes_count;
        lister_config_src.my_receiver_id = MSG_TYPE_TO_MAIN_FROM_SOURCE_LISTER;
        lister_config_src.my_recipient_id = MSG_TYPE_TO_SOURCE_ANALYZERS;
        lister_config_src.mq_key = p_context->shared_key;
        lister_config_dest.analyzers_count = the_config->processes_count;
        lister_config_dest.my_receiver_id = MSG_TYPE_TO_MAIN_FROM_DESTINATION_LISTER;
        lister_config_dest.my_recipient_id = MSG_TYPE_TO_DESTINATION_ANALYZERS;
        lister_config_dest.mq_key = p_context->shared_key; 
        p_context->source_lister_pid = make_process(p_context, lister_process_loop, &lister_config_src);
        p_context->destination_lister_pid = make_process(p_context, lister_process_loop, &lister_config_dest);
        if (p_context->source_lister_pid == -1 || p_context->destination_lister_pid == -1) { // PID -1 = FAIL
            clean_processes(the_config, p_context);
            return -1;
        }

        // Create analyzer processes (it is an array since there are many of them)
        p_context->source_analyzers_pids = malloc(the_config->processes_count * sizeof(int));
        p_context->destination_analyzers_pids = malloc(the_config->processes_count * sizeof(int));
        for (int i = 0; i < the_config->processes_count; i++) {
            analyzer_configuration_t analyser_config_dest;
            analyser_config_dest.my_receiver_id = MSG_TYPE_TO_LISTER_FROM_DESTINATION_ANALYZERS;
            analyser_config_dest.my_recipient_id = MSG_TYPE_TO_DESTINATION_LISTER;
            analyser_config_dest.mq_key = p_context->shared_key;
            analyzer_configuration_t analyser_config_src;
            analyser_config_src.my_receiver_id = MSG_TYPE_TO_LISTER_FROM_SOURCE_ANALYZERS;
            analyser_config_src.my_recipient_id = MSG_TYPE_TO_SOURCE_LISTER;
            analyser_config_src.mq_key = p_context->shared_key; 
            p_context->source_analyzers_pids[i] = make_process(p_context, analyzer_process_loop, &analyser_config_src);
            p_context->destination_analyzers_pids[i] = make_process(p_context, analyzer_process_loop, &analyser_config_dest); 
            if (p_context->destination_analyzers_pids[i] == -1 || p_context->source_analyzers_pids[i] == -1) {
                //Terminate both listers process
                kill(p_context->source_lister_pid, 0);
                kill(p_context->destination_lister_pid, 0);
                return -1;
            }
        }
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
    //Wait for an analyse_dir_command_t to be send
    //When sent, build a list with only the path of the files 
    lister_configuration_t *config = (lister_configuration_t *)parameters;
    analyze_dir_command_t message; 
    simple_command_t message_end; 
    int msg_q_id = msgget(parameters->mq_key, 0666);
    bool loop = true;
    while(loop){
        printf("Waiting for a message lister proccess loop\n");
        if(msgrcv(msg_q_id, &message, sizeof(message) - sizeof(long), parameters->my_receiver_id, 0) != -1){
            loop = false;
        }
    }
    printf("Message received\n");
    if (message.op_code == COMMAND_CODE_ANALYZE_DIR) {
        //The process is asked to make a list out of this directory
        //Build the list 
        files_list_t l; 
        l.head = NULL; 
        l.tail = NULL; 
        make_list(&l, message.target); 
        //Send the n-th first element of the list to the n analyzers 
        files_list_entry_t *p = l.head;
        int sent_requests = parameters->analyzers_count;
        int answer_received = 0; 
        analyze_file_command_t response; 
        while(p != NULL || answer_received != sent_requests){
            if (msgrcv(msg_q_id, &response, sizeof(response.op_code), parameters->my_receiver_id, 0) != -1) {
                //The lister got an answer from one of the n analyzers 
                if(response.op_code == COMMAND_CODE_FILE_ANALYZED){
                    //The analyzer finished its work
                    send_files_list_element(msg_q_id, parameters->my_receiver_id, &response.payload);
                    answer_received++; 
                    sent_requests--; 
                }
                //If one of the analyzer is unoccupied, send it another element of the list
                if (sent_requests != parameters->analyzers_count && p != NULL) {
                    send_analyze_file_command(msg_q_id, parameters->my_recipient_id, p);
                    p = p->next; 
                    answer_received--;
                }
            }
        }
        //Send the freshly received datas to the main
        if (parameters->my_receiver_id == MSG_TYPE_TO_MAIN_FROM_DESTINATION_LISTER){
            send_list_end(msg_q_id, MSG_TYPE_TO_MAIN_FROM_END_DEST_LISTER); 
        }
        else if (parameters->my_receiver_id == MSG_TYPE_TO_MAIN_FROM_SOURCE_LISTER){
            send_list_end(msg_q_id, MSG_TYPE_TO_MAIN_FROM_END_SRC_LISTER); 
        }
    } 

    if (msgrcv(msg_q_id, &message_end, sizeof(message_end.message), parameters->my_receiver_id, 0) != -1) {
        if (message_end.message == COMMAND_CODE_TERMINATE) {
            send_terminate_confirm(msg_q_id, parameters->my_recipient_id);
        }
    }
}

/*!
 * @brief analyzer_process_loop is the analyzer process function
 * @param parameters is a pointer to its parameters, to be cast to an analyzer_configuration_t
 */
void analyzer_process_loop(analyzer_configuration_t *parameters) {
    //The analyzer puts himself in a waiting state
    analyzer_configuration_t *config = (analyzer_configuration_t *)parameters;
    analyze_file_command_t message;
    simple_command_t message_end;
    int msg_id = msgget(parameters->mq_key, 0666);
    int loop = 1;
    while (loop) {
        if (msgrcv(msg_id, &message, sizeof(message) - sizeof(long), parameters->my_receiver_id, 0) != -1) {
            get_file_stats(&message.payload);
            if (message.op_code == COMMAND_CODE_ANALYZE_FILE) {
                
                if (parameters->use_md5) {
                    compute_file_md5(&message.payload);
                }
            }
            send_analyze_file_response(msg_id, parameters->my_recipient_id, &message.payload);
        }
        //When a terminaison message is received, it stops the process
        if (msgrcv(msg_id, &message_end, sizeof(message_end.message), MSG_TYPE_TO_SOURCE_ANALYZERS, 0) != -1) {
            if (message_end.message == COMMAND_CODE_TERMINATE) {
                loop = 0;
                send_terminate_confirm(msg_id, parameters->my_recipient_id); 
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
    if (the_config->is_parallel) {
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
        while (loop_dl && loop_sl && loop_sa && loop_da) { 
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
