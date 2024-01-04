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
}

/*!
 * @brief make_process creates a process and returns its PID to the parent
 * @param p_context is a pointer to the processes context
 * @param func is the function executed by the new process
 * @param parameters is a pointer to the parameters of func
 * @return the PID of the child process (it never returns in the child process)
 */
int make_process(process_context_t *p_context, process_loop_t func, void *parameters) {
}

/*!
 * @brief lister_process_loop is the lister process function (@see make_process)
 * @param parameters is a pointer to its parameters, to be cast to a lister_configuration_t
 */
void lister_process_loop(void *parameters) {
}

/*!
 * @brief analyzer_process_loop is the analyzer process function
 * @param parameters is a pointer to its parameters, to be cast to an analyzer_configuration_t
 */
void analyzer_process_loop(void *parameters) {
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
