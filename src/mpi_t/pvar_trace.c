/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 *  (C) 2017 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#if defined (MPIR_PVAR_SAMPLE_TRACING)

#define MPIR_CVAR_TRACE_BUFFER_LENGTH 1000000
#define MPIR_CVAR_PVAR_TRACE_INTERVAL_DEFAULT 1000000
FILE *PVAR_TRACE_fd;

timer_data_t        PVAR_TRACE_timer;
long long           PVAR_TRACE_interval;
unsigned            PVAR_TRACE_index;
double              PVAR_TRACE_time_prev;

double MPIDI_pt2pt_enqueue_time;
double MPIDI_pt2pt_progress_time;
double MPIDI_pt2pt_issue_pend_time;
double MPIDI_rma_enqueue_time;
double MPIDI_rma_progress_time;
double MPIDI_rma_issue_pend_time;

struct PVAR_TRACE_entry {
    double             timestamp;
#if ENABLE_PVAR_P2PWORKQ
    double p2p_enqueue_time;
    double p2p_progress_time;
    double p2p_issue_pend_time;
#endif
#if ENABLE_PVAR_RMAWORKQ
    double rma_enqueue_time;
    double rma_progress_time;
    double rma_issue_pend_time;
#endif
};

struct PVAR_TRACE_entry old_entry = {0};
struct PVAR_TRACE_entry cur_entry;

struct PVAR_TRACE_entry *PVAR_TRACE_buffer;
FILE               *PVAR_TRACE_fd;

static void PVAR_TRACE_dump_trace() {
    unsigned i;
    for( i=0; i< PVAR_TRACE_index; i++) {
        fprintf(PVAR_TRACE_fd, "%.3f", 1e3*PVAR_TRACE_buffer[i].timestamp);
#if ENABLE_PVAR_P2PWORKQ
        fprintf(PVAR_TRACE_fd, ",%.3f",   PVAR_TRACE_buffer[i].p2p_enqueue_time*1e3);
        fprintf(PVAR_TRACE_fd, ",%.3f",   PVAR_TRACE_buffer[i].p2p_progress_time*1e3);
        fprintf(PVAR_TRACE_fd, ",%.3f",   PVAR_TRACE_buffer[i].p2p_issue_pend_time*1e3);
#endif
#if ENABLE_PVAR_RMAWORKQ
        fprintf(PVAR_TRACE_fd, ",%.3f",   PVAR_TRACE_buffer[i].rma_enqueue_time*1e3);
        fprintf(PVAR_TRACE_fd, ",%.3f",   PVAR_TRACE_buffer[i].rma_progress_time*1e3);
        fprintf(PVAR_TRACE_fd, ",%.3f",   PVAR_TRACE_buffer[i].rma_issue_pend_time;*1e3);
#endif
        fprintf(PVAR_TRACE_fd, "\n");
    }
    memset(PVAR_TRACE_buffer,            0.0, MPIR_CVAR_TRACE_BUFFER_LENGTH*sizeof(struct PVAR_TRACE_entry));
    PVAR_TRACE_index = 0;
}

void PVAR_TRACE_timer_handler(int sig, siginfo_t *si, void *uc) {

    double timestamp = PMPI_Wtime();
    double duration = timestamp - PVAR_TRACE_time_prev;
    PVAR_TRACE_time_prev = timestamp;

    if(PVAR_TRACE_index + 1 < MPIR_CVAR_TRACE_BUFFER_LENGTH) {
        PVAR_TRACE_buffer[PVAR_TRACE_index].timestamp = duration;
#if ENABLE_PVAR_P2PWORKQ
        cur_entry.p2p_enqueue_time                                = MPIDI_pt2pt_enqueue_time;
        cur_entry.p2p_progress_time                               = MPIDI_pt2pt_progress_time;
        cur_entry.p2p_issue_pend_time                             = MPIDI_pt2pt_issue_pend_time;
        PVAR_TRACE_buffer[PVAR_TRACE_index].p2p_enqueue_time      = cur_entry.p2p_enqueue_time    - old_entry.p2p_enqueue_time;
        PVAR_TRACE_buffer[PVAR_TRACE_index].p2p_progress_time     = cur_entry.p2p_progress_time   - old_entry.p2p_progress_time;  
        PVAR_TRACE_buffer[PVAR_TRACE_index].p2p_issue_pend_time   = cur_entry.p2p_issue_pend_time - old_entry.p2p_issue_pend_time;
#endif
#if ENABLE_PVAR_RMAWORKQ
        cur_entry.rma_enqueue_time                                = MPIDI_rma_enqueue_time;
        cur_entry.rma_progress_time                               = MPIDI_rma_progress_time;
        cur_entry.rma_issue_pend_time                             = MPIDI_rma_issue_pend_time;
        PVAR_TRACE_buffer[PVAR_TRACE_index].rma_enqueue_time      = cur_entry.rma_enqueue_time    - old_entry.rma_enqueue_time;
        PVAR_TRACE_buffer[PVAR_TRACE_index].rma_progress_time     = cur_entry.rma_progress_time   - old_entry.rma_progress_time;  
        PVAR_TRACE_buffer[PVAR_TRACE_index].rma_issue_pend_time   = cur_entry.rma_issue_pend_time - old_entry.rma_issue_pend_time;
#endif
        memcpy(&old_entry, &cur_entry, sizeof(struct PVAR_TRACE_entry));
        PVAR_TRACE_index++;
    }
    else {
        /* Dump current trace and reset everything */
        PVAR_TRACE_dump_trace();
    }

}

void MPIR_T_init_trace() {
    char *s;
    int ret;
    s = getenv("MPIR_CVAR_PVAR_TRACE_INTERVAL");
    if(s) PVAR_TRACE_interval = atoll(s);
    else  PVAR_TRACE_interval = MPIR_CVAR_PVAR_TRACE_INTERVAL_DEFAULT;
    PVAR_TRACE_buffer     = (struct PVAR_TRACE_entry*) calloc(MPIR_CVAR_TRACE_BUFFER_LENGTH, sizeof(struct PVAR_TRACE_entry));
    PVAR_TRACE_index = 0;
    int my_rank = MPIR_Process.comm_world->rank;
    int my_pid = getpid();
    char filename[30];
    sprintf(filename, "%d_%d.csv", my_rank, my_pid);
    PVAR_TRACE_fd = fopen(filename, "w");
    assert(PVAR_TRACE_fd != NULL);
    ret = fprintf(PVAR_TRACE_fd, "time");
    assert(ret >= 0);
    if(ENABLE_PVAR_P2PWORKQ)
        fprintf(PVAR_TRACE_fd, ",p2p_enq_time,p2p_progress_time,p2p_issue_pend_time");
    if(ENABLE_PVAR_RMAWORKQ)
        fprintf(PVAR_TRACE_fd, ",rma_enq_time,rma_progress_time,rma_issue_pend_time");
    fprintf(PVAR_TRACE_fd, "\n");
    fflush(PVAR_TRACE_fd);
    PVAR_TRACE_time_prev = PMPI_Wtime() ;
    start_sampling(&PVAR_TRACE_timer, PVAR_TRACE_interval, PVAR_TRACE_timer_handler);
}

void PVAR_TRACE_finalize() {
    int ret;
    PVAR_TRACE_dump_trace();
    free(PVAR_TRACE_buffer);
    ret = fclose(PVAR_TRACE_fd);
    if (ret != 0) {
        if (ret == EBADF) {
            printf("fclose: %p is not a valid file descriptor\n", PVAR_TRACE_fd);
        } else {
            printf("fclose returned %s\n", strerror(errno));
        }
    }
}

void MPIR_T_stop_trace() {
      stop_sampling(&PVAR_TRACE_timer, PVAR_TRACE_finalize);
}

#else

void MPIR_T_init_trace() {
    return;
}

void MPIR_T_stop_trace() {
    return;
}

#endif

