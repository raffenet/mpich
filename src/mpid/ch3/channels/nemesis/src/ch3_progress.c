/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpidi_ch3_impl.h"

/*#include "mpidpre.h"*/
#include "mpid_nem_impl.h"
#if defined (MPID_NEM_INLINE) && MPID_NEM_INLINE
#include "mpid_nem_inline.h"
#endif
#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

typedef struct vc_term_element
{
    struct vc_term_element *next;
    MPIDI_VC_t *vc;
    MPIR_Request *req;
} vc_term_element_t;

static struct { vc_term_element_t *head, *tail; } vc_term_queue;
#define TERMQ_EMPTY() GENERIC_Q_EMPTY(vc_term_queue)
#define TERMQ_HEAD() GENERIC_Q_HEAD(vc_term_queue)
#define TERMQ_ENQUEUE(ep) GENERIC_Q_ENQUEUE(&vc_term_queue, ep, next)
#define TERMQ_DEQUEUE(epp) GENERIC_Q_DEQUEUE(&vc_term_queue, epp, next)

#define PKTARRAY_SIZE (MPIDI_CH3_PKT_END_ALL+1)
static MPIDI_CH3_PktHandler_Fcn *pktArray[PKTARRAY_SIZE];

#ifndef MPIDI_POSTED_RECV_ENQUEUE_HOOK
#define MPIDI_POSTED_RECV_ENQUEUE_HOOK(x) do{}while(0)
#endif
#ifndef MPIDI_POSTED_RECV_DEQUEUE_HOOK
#define MPIDI_POSTED_RECV_DEQUEUE_HOOK(x) 0
#endif

#ifdef BY_PASS_PROGRESS
extern MPIR_Request ** const MPID_Recvq_posted_head_ptr;
extern MPIR_Request ** const MPID_Recvq_unexpected_head_ptr;
extern MPIR_Request ** const MPID_Recvq_posted_tail_ptr;
extern MPIR_Request ** const MPID_Recvq_unexpected_tail_ptr;
#endif

MPL_atomic_int_t MPIDI_CH3I_progress_completion_count = MPL_ATOMIC_INT_T_INITIALIZER(0);

/* NEMESIS MULTITHREADING: Extra Data Structures Added */
#ifdef MPICH_IS_THREADED
volatile int MPIDI_CH3I_progress_blocked = FALSE;
volatile int MPIDI_CH3I_progress_wakeup_signalled = FALSE;
#if (MPICH_THREAD_GRANULARITY == MPICH_THREAD_GRANULARITY__GLOBAL)
static MPID_Thread_cond_t MPIDI_CH3I_progress_completion_cond;
#endif
static int MPIDI_CH3I_Progress_delay(unsigned int completion_count);
static int MPIDI_CH3I_Progress_continue(unsigned int completion_count);
#endif /* MPICH_IS_THREADED */
/* NEMESIS MULTITHREADING - End block*/

#ifdef HAVE_SIGNAL
static void (*prev_sighandler) (int);
#endif
static volatile int sigusr1_count = 0;
static int my_sigusr1_count = 0;

MPIDI_CH3I_shm_sendq_t MPIDI_CH3I_shm_sendq = {NULL, NULL};
struct MPIR_Request *MPIDI_CH3I_shm_active_send = NULL;

static int pkt_NETMOD_handler(MPIDI_VC_t *vc, MPIDI_CH3_Pkt_t *pkt, void *data, intptr_t *buflen, MPIR_Request **rreqp);
static int shm_connection_terminated(MPIDI_VC_t * vc);
static int check_terminating_vcs(void);

int (*MPID_nem_local_lmt_progress)(void) = NULL;
int MPID_nem_local_lmt_pending = FALSE;

/* qn_ent and friends are used to keep a list of notification
   callbacks for posted and matched anysources */
typedef struct qn_ent
{
    struct qn_ent *next;
    void (*enqueue_fn)(MPIR_Request *rreq);
    int (*dequeue_fn)(MPIR_Request *rreq);
} qn_ent_t;

static qn_ent_t *qn_head = NULL;

#ifdef HAVE_SIGNAL
static void sigusr1_handler(int sig)
{
    ++sigusr1_count;
    /* poke the progress engine in case we're waiting in a blocking recv */
    MPIDI_CH3_Progress_signal_completion();
    if (prev_sighandler)
        prev_sighandler(sig);
}
#endif

static int check_terminating_vcs(void)
{
    int mpi_errno = MPI_SUCCESS;

    MPIR_FUNC_ENTER;

    while (!TERMQ_EMPTY() && MPIR_Request_is_complete(TERMQ_HEAD()->req)) {
        vc_term_element_t *ep;
        TERMQ_DEQUEUE(&ep);
        MPIR_Request_free(ep->req);
        mpi_errno = shm_connection_terminated(ep->vc);
        MPIR_ERR_CHECK(mpi_errno);
        MPL_free(ep);
    }
    
 fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}


/* MPIDI_CH3I_Shm_send_progress() this function makes progress sending
   queued messages on the shared memory queues.  This function is
   nonblocking and does not call netmod functions..*/
int MPIDI_CH3I_Shm_send_progress(void)
{
    int mpi_errno = MPI_SUCCESS;
    struct iovec *iov;
    int n_iov;
    MPIR_Request *sreq;
    int again = 0;


    MPIR_FUNC_ENTER;

    sreq = MPIDI_CH3I_shm_active_send;
    MPL_DBG_STMT(MPIDI_CH3_DBG_CHANNEL, VERBOSE, {if (sreq) MPL_DBG_MSG (MPIDI_CH3_DBG_CHANNEL, VERBOSE, "Send: cont sreq");});
    if (sreq)
    {
        if (!sreq->ch.noncontig)
        {
            MPIR_Assert(sreq->dev.iov_count > 0 && sreq->dev.iov[sreq->dev.iov_offset].iov_len > 0);

            iov = &sreq->dev.iov[sreq->dev.iov_offset];
            n_iov = sreq->dev.iov_count;

            do
            {
                mpi_errno = MPID_nem_mpich_sendv(&iov, &n_iov, sreq->ch.vc, &again);
                if (mpi_errno) MPIR_ERR_POP (mpi_errno);
            }
            while (!again && n_iov > 0);

            if (again) /* not finished sending */
            {
                sreq->dev.iov_offset = iov - sreq->dev.iov;
                sreq->dev.iov_count = n_iov;
                goto fn_exit;
            }
            else
                sreq->dev.iov_offset = 0;
        }
        else
        {
            do
            {
                MPID_nem_mpich_send_seg(sreq->dev.user_buf, sreq->dev.user_count, sreq->dev.datatype,
                                        &sreq->dev.msg_offset, sreq->dev.msgsize,
                                         sreq->ch.vc, &again);
            }
            while (!again && sreq->dev.msg_offset < sreq->dev.msgsize);

            if (again) /* not finished sending */
                goto fn_exit;
        }
    }
    else
    {
        sreq = MPIDI_CH3I_Sendq_head(MPIDI_CH3I_shm_sendq);
        MPL_DBG_STMT (MPIDI_CH3_DBG_CHANNEL, VERBOSE, {if (sreq) MPL_DBG_MSG (MPIDI_CH3_DBG_CHANNEL, VERBOSE, "Send: new sreq ");});

        if (!sreq->ch.noncontig)
        {
            MPIR_Assert(sreq->dev.iov_count > 0 && sreq->dev.iov[sreq->dev.iov_offset].iov_len > 0);

            iov = &sreq->dev.iov[sreq->dev.iov_offset];
            n_iov = sreq->dev.iov_count;

            mpi_errno = MPID_nem_mpich_sendv_header(&iov, &n_iov, sreq->ch.vc, &again);
            if (mpi_errno) MPIR_ERR_POP (mpi_errno);
            if (!again)
            {
                MPIDI_CH3I_shm_active_send = sreq;
                while (!again && n_iov > 0)
                {
                    mpi_errno = MPID_nem_mpich_sendv(&iov, &n_iov, sreq->ch.vc, &again);
                    if (mpi_errno) MPIR_ERR_POP (mpi_errno);
                }
            }

            if (again) /* not finished sending */
            {
                sreq->dev.iov_offset = iov - sreq->dev.iov;
                sreq->dev.iov_count = n_iov;
                goto fn_exit;
            }
            else
                sreq->dev.iov_offset = 0;
        }
        else
        {
            MPID_nem_mpich_send_seg_header(sreq->dev.user_buf, sreq->dev.user_count, sreq->dev.datatype,
                                           &sreq->dev.msg_offset, sreq->dev.msgsize,
                                           &sreq->dev.pending_pkt, sreq->ch.header_sz, sreq->ch.vc, &again);
            if (!again)
            {
                MPIDI_CH3I_shm_active_send = sreq;
                while (!again && sreq->dev.msg_offset < sreq->dev.msgsize)
                {
                    MPID_nem_mpich_send_seg(sreq->dev.user_buf, sreq->dev.user_count, sreq->dev.datatype,
                                            &sreq->dev.msg_offset, sreq->dev.msgsize,
                                             sreq->ch.vc, &again);
                }
            }

            if (again) /* not finished sending */
                goto fn_exit;
        }
    }

    /* finished sending sreq */
    MPIR_Assert(!again);

    if (!sreq->dev.OnDataAvail)
    {
        MPIR_Assert(MPIDI_Request_get_type(sreq) != MPIDI_REQUEST_TYPE_GET_RESP);

        mpi_errno = MPID_Request_complete(sreq);
        MPIR_ERR_CHECK(mpi_errno);

        /* MT - clear the current active send before dequeuing/destroying the current request */
        MPIDI_CH3I_shm_active_send = NULL;
        MPIDI_CH3I_Sendq_dequeue(&MPIDI_CH3I_shm_sendq, &sreq);
        MPL_DBG_MSG(MPIDI_CH3_DBG_CHANNEL, VERBOSE, ".... complete");
        mpi_errno = check_terminating_vcs();
        MPIR_ERR_CHECK(mpi_errno);
    }
    else
    {
        int complete = 0;
        mpi_errno = sreq->dev.OnDataAvail(sreq->ch.vc, sreq, &complete);
        MPIR_ERR_CHECK(mpi_errno);

        if (complete)
        {
            MPIDI_CH3I_shm_active_send = NULL;
            MPIDI_CH3I_Sendq_dequeue(&MPIDI_CH3I_shm_sendq, &sreq);
            MPL_DBG_MSG(MPIDI_CH3_DBG_CHANNEL, VERBOSE, ".... complete");
            mpi_errno = check_terminating_vcs();
            MPIR_ERR_CHECK(mpi_errno);
        }
    }
        
 fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}

/* NOTE: it appears that this function is sometimes (inadvertently?) recursive.
 * Some packet handlers, such as MPIDI_CH3_PktHandler_Close, call iStartMsg,
 * which calls MPID_Progress_test. */
int MPIDI_CH3I_Progress (MPID_Progress_state *progress_state, int is_blocking)
{
    int mpi_errno = MPI_SUCCESS;
    int made_progress = FALSE;

    MPIR_FUNC_ENTER;

    /* sanity: if this doesn't hold, we can't track our local view of completion safely */
    if (is_blocking) {
        MPIR_Assert(progress_state != NULL);
    }

    if (sigusr1_count > my_sigusr1_count) {
        my_sigusr1_count = sigusr1_count;
        mpi_errno = MPIDI_CH3U_Check_for_failed_procs();
        MPIR_ERR_CHECK(mpi_errno);
    }
    
#ifdef ENABLE_CHECKPOINTING
    if (MPIR_CVAR_NEMESIS_ENABLE_CKPOINT) {
        if (MPIDI_nem_ckpt_start_checkpoint) {
            MPIDI_nem_ckpt_start_checkpoint = FALSE;
            mpi_errno = MPIDI_nem_ckpt_start();
            MPIR_ERR_CHECK(mpi_errno);
        }
        if (MPIDI_nem_ckpt_finish_checkpoint) {
            MPIDI_nem_ckpt_finish_checkpoint = FALSE;
            mpi_errno = MPIDI_nem_ckpt_finish();
            MPIR_ERR_CHECK(mpi_errno);
        }
    }
#endif

/* For threaded mode, if another thread is in the progress engine, we
 * don't enter the progress engine */
#ifdef MPICH_IS_THREADED
    MPIR_THREAD_CHECK_BEGIN;
    {
        while (MPIDI_CH3I_progress_blocked == TRUE)
        {
            /* if this is a nonblocking call, and some other thread is
             * going to poke progress, our job is done and we can
             * return */
            if (!is_blocking)
                goto fn_exit;

            /* if it's a blocking call, and some other thread is going
             * to poke progress, our job might also be done.  But
             * there's no point returning from this call to see if the
             * work is done and coming back in again if it's not done.
             * We might as well wait for the other thread to be done
             * before doing that. */
            if (progress_state->ch.completion_count == MPL_atomic_relaxed_load_int(&MPIDI_CH3I_progress_completion_count))
                MPIDI_CH3I_Progress_delay(progress_state->ch.completion_count);
            else {
                /* if the completion count of our progress state is
                 * different from the current completion count, some
                 * progress happened.  We reset the value for the next
                 * iteration and return from the progress engine. */
                progress_state->ch.completion_count = MPL_atomic_relaxed_load_int(&MPIDI_CH3I_progress_completion_count);
                goto fn_exit;
            }
        }
    }
    MPIR_THREAD_CHECK_END;
#endif

    do
    {
	MPIR_Request        *rreq;
	MPID_nem_cell_ptr_t  cell;
	int                  in_fbox = 0;
	MPIDI_VC_t          *vc;

        do /* receive progress */
        {
            /* make progress receiving */
            /* check queue */
            if (MPID_nem_safe_to_block_recv() && is_blocking && !MPIR_IS_THREADED)
            {
                mpi_errno = MPID_nem_mpich_blocking_recv(&cell, &in_fbox, progress_state->ch.completion_count);
            }
            else
            {
                mpi_errno = MPID_nem_mpich_test_recv(&cell, &in_fbox, is_blocking);
            }
            if (mpi_errno) MPIR_ERR_POP (mpi_errno);

            if (cell)
            {
                char            *cell_buf    = (char *)cell->payload;
                intptr_t   payload_len = cell->header.datalen;
                MPIDI_CH3_Pkt_t *pkt         = (MPIDI_CH3_Pkt_t *)cell_buf;

                /* Empty packets are not allowed */
                MPIR_Assert(payload_len >= 0);

                if (in_fbox)
                {
                    MPIDI_CH3I_VC *vc_ch;
                    intptr_t buflen;

                    /* This packet must be the first packet of a new message */
                    MPL_DBG_MSG(MPIDI_CH3_DBG_CHANNEL, VERBOSE, "Recv pkt from fbox");
                    MPIR_Assert(payload_len >= sizeof (MPIDI_CH3_Pkt_t));

                    /* deduct packet header */
                    payload_len -= sizeof(MPIDI_CH3_Pkt_t);
                    cell_buf += sizeof(MPIDI_CH3_Pkt_t);
                    buflen = payload_len;

                    MPIDI_PG_Get_vc_set_active(MPIDI_Process.my_pg, MPID_NEM_FBOX_SOURCE(cell), &vc);
		   
		    MPIR_Assert(vc->ch.recv_active == NULL &&
                                vc->ch.pending_pkt_len == 0);
                    vc_ch = &vc->ch;

                    /* invalid pkt data will result in unpredictable behavior */
                    MPIR_Assert(pkt->type >= 0 && pkt->type < MPIDI_CH3_PKT_END_ALL);

                    mpi_errno = pktArray[pkt->type](vc, pkt, cell_buf, &buflen, &rreq);
                    MPIR_ERR_CHECK(mpi_errno);

                    if (!rreq)
                    {
                        MPID_nem_mpich_release_fbox(cell);
                        break; /* break out of recv progress block */
                    }

                    /* we received a truncated packet, handle it with handle_pkt */
                    vc_ch->recv_active = rreq;
                    cell_buf    += buflen;
                    payload_len -= buflen;

                    MPIR_Assert(payload_len >= 0);

                    mpi_errno = MPID_nem_handle_pkt(vc, cell_buf, payload_len);
                    MPIR_ERR_CHECK(mpi_errno);
                    MPID_nem_mpich_release_fbox(cell);

                    /* the whole message should have been handled */
                    MPIR_Assert(!vc_ch->recv_active);

                    break; /* break out of recv progress block */
                }


                MPL_DBG_MSG(MPIDI_CH3_DBG_CHANNEL, VERBOSE, "Recv pkt from queue");

                MPIDI_PG_Get_vc_set_active(MPIDI_Process.my_pg, MPID_NEM_CELL_SOURCE(cell), &vc);

                mpi_errno = MPID_nem_handle_pkt(vc, cell_buf, payload_len);
                MPIR_ERR_CHECK(mpi_errno);
                MPID_nem_mpich_release_cell(cell, vc);

                break; /* break out of recv progress block */

            }
        }
        while(0);  /* do the loop exactly once.  Used so we can jump out of recv progress using break. */


	/* make progress sending */
        if (MPIDI_CH3I_shm_active_send || MPIDI_CH3I_Sendq_head(MPIDI_CH3I_shm_sendq)) {
            mpi_errno = MPIDI_CH3I_Shm_send_progress();
            MPIR_ERR_CHECK(mpi_errno);
        }
        
        /* make progress on LMTs */
        if (MPID_nem_local_lmt_pending)
        {
            mpi_errno = MPID_nem_local_lmt_progress();
            MPIR_ERR_CHECK(mpi_errno);
        }

        mpi_errno = MPIR_Progress_hook_exec_all(-1, &made_progress);
        MPIR_ERR_CHECK(mpi_errno);

        if (made_progress)
            MPIDI_CH3_Progress_signal_completion();

        /* in the case of progress_wait, bail out if anything completed (CC-1) */
        if (is_blocking) {
            int completion_count = MPL_atomic_relaxed_load_int(&MPIDI_CH3I_progress_completion_count);
            if (progress_state->ch.completion_count != completion_count) {
                /* Read barrier to make sure no reads get values before the
                   completion counter was incremented  */
                MPL_atomic_read_barrier();
                /* reset for the next iteration */
                progress_state->ch.completion_count = completion_count;
                break;
            }
        }

#ifdef MPICH_IS_THREADED
        MPIR_THREAD_CHECK_BEGIN;
        {
            if (is_blocking) {
                MPIDI_CH3I_progress_blocked = TRUE;
                MPID_THREAD_CS_YIELD(GLOBAL, MPIR_THREAD_GLOBAL_ALLFUNC_MUTEX);
                /* MPIDCOMM yield is needed because at least the send
                 * functions acquire MPIDCOMM to put things into the send
                 * queues.  Failure to yield could result in a deadlock.
                 * This thread needs the send from another thread to be
                 * posted, but the other thread can't post it while this
                 * CS is held. */
                /* assertion: we currently do not hold any other critical
                 * sections besides the MPIDCOMM CS at this point.
                 * Violating this will probably lead to lock-ordering
                 * deadlocks. */
                MPIDI_CH3I_progress_blocked = FALSE;
                MPIDI_CH3I_progress_wakeup_signalled = FALSE;
            }
        }
        MPIR_THREAD_CHECK_END;
#else
        MPIU_Busy_wait();
#endif
    }
    while (is_blocking);

    
#ifdef MPICH_IS_THREADED
    MPIR_THREAD_CHECK_BEGIN;
    {
        if (is_blocking)
        {
            MPIDI_CH3I_Progress_continue(0/*unused*/);
        }
    }
    MPIR_THREAD_CHECK_END;
#endif

 fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}
#ifdef MPICH_IS_THREADED

/* Note that this routine is only called if threads are enabled;
   it does not need to check whether runtime threads are enabled */
static int MPIDI_CH3I_Progress_delay(unsigned int completion_count)
{
    int mpi_errno = MPI_SUCCESS, err;

    MPIR_FUNC_ENTER;
    /* FIXME should be appropriately abstracted somehow */
#   if defined(MPICH_IS_THREADED) && (MPICH_THREAD_GRANULARITY == MPICH_THREAD_GRANULARITY__GLOBAL)
    {
        MPID_Thread_cond_wait(&MPIDI_CH3I_progress_completion_cond, &MPIR_THREAD_GLOBAL_ALLFUNC_MUTEX/*MPIDCOMM*/, &err);
    }
#   endif

    MPIR_FUNC_EXIT;
    return mpi_errno;
}
/* end MPIDI_CH3I_Progress_delay() */


static int MPIDI_CH3I_Progress_continue(unsigned int completion_count/*unused*/)
{
    int mpi_errno = MPI_SUCCESS,err;

    MPIR_FUNC_ENTER;
    /* FIXME should be appropriately abstracted somehow */
#   if defined(MPICH_IS_THREADED) && (MPICH_THREAD_GRANULARITY == MPICH_THREAD_GRANULARITY__GLOBAL)
    {
        /* we currently hold the MPIDCOMM CS */
        MPID_Thread_cond_broadcast(&MPIDI_CH3I_progress_completion_cond, &err);
    }
#   endif

    MPIR_FUNC_EXIT;
    return mpi_errno;
}
/* end MPIDI_CH3I_Progress_continue() */


void MPIDI_CH3I_Progress_wakeup(void)
{

    MPIR_FUNC_ENTER;

    /* no processes sleep in nemesis progress */
    MPIR_FUNC_EXIT;
    return;
}
#endif /* MPICH_IS_THREADED */

int MPID_nem_handle_pkt(MPIDI_VC_t *vc, char *buf, intptr_t buflen)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_Request *rreq = NULL;
    int complete;
    MPIDI_CH3I_VC *vc_ch = &vc->ch;

    MPIR_FUNC_ENTER;

    do
    {
        if (!vc_ch->recv_active && vc_ch->pending_pkt_len == 0 && buflen >= sizeof(MPIDI_CH3_Pkt_t))
        {
            /* handle fast-path first: received a new whole message */
            do
            {
                intptr_t len;
                MPIDI_CH3_Pkt_t *pkt = (MPIDI_CH3_Pkt_t *)buf;
#ifdef NEEDS_STRICT_ALIGNMENT
                MPIDI_CH3_Pkt_t aligned_buf;
#endif

                MPL_DBG_MSG(MPIDI_CH3_DBG_CHANNEL, VERBOSE, "received new message");

#ifdef NEEDS_STRICT_ALIGNMENT
                /* Copy packet header to temporary buffer to ensure aligned access.
                 * Note that we intentionally perform the extra copy on all platforms
                 * that require strict alignment without checking whether the original
                 * buffer is aligned. This is because we cannot find a good way to
                 * get correct alignment for general structure on all platforms. */
                MPIR_Memcpy(&aligned_buf, buf, sizeof(MPIDI_CH3_Pkt_t));
                pkt = &aligned_buf;
#endif
                /* deduct packet header */
                buflen -= sizeof(MPIDI_CH3_Pkt_t);
                buf += sizeof(MPIDI_CH3_Pkt_t);
                len = buflen;

                /* invalid pkt data will result in unpredictable behavior */
                MPIR_Assert(pkt->type >= 0 && pkt->type < MPIDI_CH3_PKT_END_ALL);

                mpi_errno = pktArray[pkt->type](vc, pkt, buf, &len, &rreq);
                MPIR_ERR_CHECK(mpi_errno);
                buflen -= len;
                buf    += len;

                MPIR_Assert(buflen >= 0);

                MPL_DBG_STMT(MPIDI_CH3_DBG_CHANNEL, VERBOSE, if (!rreq) MPL_DBG_MSG(MPIDI_CH3_DBG_CHANNEL, VERBOSE, "...completed immediately"));
            }
            while (!rreq && buflen >= sizeof(MPIDI_CH3_Pkt_t));

            if (!rreq)
                continue;

            /* Channel fields don't get initialized on request creation, init them here */
            if (rreq)
                rreq->dev.iov_offset = 0;
        }
        else if (vc_ch->recv_active)
        {
            MPIR_Assert(vc_ch->pending_pkt_len == 0);
            MPL_DBG_MSG(MPIDI_CH3_DBG_CHANNEL, VERBOSE, "continuing recv");
            rreq = vc_ch->recv_active;
        }
        else
        {
            /* collect header fragments in vc's pending_pkt */
            intptr_t copylen;
            intptr_t datalen = 0;
            MPIDI_CH3_Pkt_t *pkt = (MPIDI_CH3_Pkt_t *)vc_ch->pending_pkt;

            /* pending_pkt is allocated by malloc which guarantees the allocated
             * space is suitably aligned for storage of any type of object. */

            MPL_DBG_MSG(MPIDI_CH3_DBG_CHANNEL, VERBOSE, "received header fragment");

            copylen = ((vc_ch->pending_pkt_len + buflen <= sizeof(MPIDI_CH3_Pkt_t))
                       ? buflen
                       : sizeof(MPIDI_CH3_Pkt_t) - vc_ch->pending_pkt_len);
            MPIR_Memcpy((char *)vc_ch->pending_pkt + vc_ch->pending_pkt_len, buf, copylen);
            vc_ch->pending_pkt_len += copylen;
            if (vc_ch->pending_pkt_len < sizeof(MPIDI_CH3_Pkt_t))
                goto fn_exit;

            /* we have a whole header */
            MPL_DBG_MSG(MPIDI_CH3_DBG_CHANNEL, VERBOSE, "    completed header");
            MPIR_Assert(vc_ch->pending_pkt_len == sizeof(MPIDI_CH3_Pkt_t));

            buflen -= copylen;
            buf    += copylen;

            /* invalid pkt data will result in unpredictable behavior */
            MPIR_Assert(pkt->type >= 0 && pkt->type < MPIDI_CH3_PKT_END_ALL);

            mpi_errno = pktArray[pkt->type](vc, pkt, NULL, &datalen, &rreq);
            MPIR_ERR_CHECK(mpi_errno);
            MPIR_Assert(datalen == 0);

            vc_ch->pending_pkt_len = 0;

            if (!rreq)
            {
                MPL_DBG_MSG(MPIDI_CH3_DBG_CHANNEL, VERBOSE, "...completed immediately");
                continue;
            }
            /* Channel fields don't get initialized on request creation, init them here */
            rreq->dev.iov_offset = 0;
        }

        /* copy data into user buffer described by iov in rreq */
        MPIR_Assert(rreq);
        MPIR_Assert(rreq->dev.iov_count > 0 && rreq->dev.iov[rreq->dev.iov_offset].iov_len > 0);
        MPIR_Assert(buflen >= 0);

        MPL_DBG_MSG(MPIDI_CH3_DBG_CHANNEL, VERBOSE, "    copying into user buffer from IOV");

        if (buflen == 0)
        {
            vc_ch->recv_active = rreq;
            goto fn_exit;
        }

        complete = 0;

        while (buflen && !complete)
        {
            struct iovec *iov;
            int n_iov;

            iov = &rreq->dev.iov[rreq->dev.iov_offset];
            n_iov = rreq->dev.iov_count;
		
            while (n_iov && buflen >= iov->iov_len)
            {
                size_t iov_len = iov->iov_len;
		MPL_DBG_MSG_D(MPIDI_CH3_DBG_CHANNEL, VERBOSE, "        %d", (int)iov_len);
                if (rreq->dev.drop_data == FALSE) {
                    MPIR_Memcpy (iov->iov_base, buf, iov_len);
                }

                buflen -= iov_len;
                buf    += iov_len;
                --n_iov;
                ++iov;
            }
		
            if (n_iov)
            {
                if (buflen > 0)
                {
		    MPL_DBG_MSG_D(MPIDI_CH3_DBG_CHANNEL, VERBOSE, "        %" PRIdPTR, buflen);
                    if (rreq->dev.drop_data == FALSE) {
                        MPIR_Memcpy (iov->iov_base, buf, buflen);
                    }
                    iov->iov_base = (void *)((char *)iov->iov_base + buflen);
                    iov->iov_len -= buflen;
                    buflen = 0;
                }

                rreq->dev.iov_offset = iov - rreq->dev.iov;
                rreq->dev.iov_count = n_iov;
                vc_ch->recv_active = rreq;
		MPL_DBG_MSG_FMT(MPIDI_CH3_DBG_CHANNEL, VERBOSE, (MPL_DBG_FDEST, "        remaining: %" PRIdPTR " bytes + %d iov entries", iov->iov_len, n_iov));
            }
            else
            {
                int (*reqFn)(MPIDI_VC_t *, MPIR_Request *, int *);

                reqFn = rreq->dev.OnDataAvail;
                if (!reqFn)
                {
                    MPIR_Assert(MPIDI_Request_get_type(rreq) != MPIDI_REQUEST_TYPE_GET_RESP);
                    mpi_errno = MPID_Request_complete(rreq);
                    MPIR_ERR_CHECK(mpi_errno);

                    complete = TRUE;
                }
                else
                {
                    mpi_errno = reqFn(vc, rreq, &complete);
                    MPIR_ERR_CHECK(mpi_errno);
                }

                if (!complete)
                {
                    rreq->dev.iov_offset = 0;
                    MPIR_Assert(rreq->dev.iov_count > 0 && rreq->dev.iov[rreq->dev.iov_offset].iov_len > 0);
                    vc_ch->recv_active = rreq;
                    MPL_DBG_MSG(MPIDI_CH3_DBG_CHANNEL, VERBOSE, "...not complete");
                }
                else
                {
                    MPL_DBG_MSG(MPIDI_CH3_DBG_CHANNEL, VERBOSE, "...complete");
                    vc_ch->recv_active = NULL;
                }
            }
        }
    }
    while (buflen);

 fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}


#define set_request_info(rreq_, pkt_, msg_type_)                \
{                                                               \
    (rreq_)->status.MPI_SOURCE = (pkt_)->match.rank;            \
    (rreq_)->status.MPI_TAG = (pkt_)->match.tag;                \
    MPIR_STATUS_SET_COUNT((rreq_)->status, (pkt_)->data_sz);		\
    (rreq_)->dev.sender_req_id = (pkt_)->sender_req_id;         \
    (rreq_)->dev.recv_data_sz = (pkt_)->data_sz;                \
    MPIDI_Request_set_seqnum((rreq_), (pkt_)->seqnum);          \
    MPIDI_Request_set_msg_type((rreq_), (msg_type_));           \
}

int MPIDI_CH3I_Progress_init(void)
{
    int mpi_errno = MPI_SUCCESS;
#ifdef HAVE_ERROR_CHECKING
    char strerrbuf[MPIR_STRERROR_BUF_SIZE];
#endif

    MPIR_FUNC_ENTER;

    MPIR_THREAD_CHECK_BEGIN;
    /* FIXME should be appropriately abstracted somehow */
#   if defined(MPICH_IS_THREADED) && (MPICH_THREAD_GRANULARITY == MPICH_THREAD_GRANULARITY__GLOBAL)
    {
        int err;
	MPID_Thread_cond_create(&MPIDI_CH3I_progress_completion_cond, &err);
        MPIR_Assert(err == 0);
    }
#   endif
    MPIR_THREAD_CHECK_END;

    MPIDI_CH3I_shm_sendq.head = NULL;
    MPIDI_CH3I_shm_sendq.tail = NULL;
    MPIDI_CH3I_shm_active_send = NULL;
    
    /* Initialize the code to handle incoming packets */
    if (PKTARRAY_SIZE <= MPIDI_CH3_PKT_END_ALL) {
        MPIR_ERR_SETFATALANDJUMP(mpi_errno, MPI_ERR_INTERN, "**ch3|pktarraytoosmall");
    }
    /* pkt handlers from CH3 */
    mpi_errno = MPIDI_CH3_PktHandler_Init(pktArray, PKTARRAY_SIZE);
    MPIR_ERR_CHECK(mpi_errno);

    /* pkt handlers for LMT */
    mpi_errno = MPID_nem_lmt_pkthandler_init(pktArray, PKTARRAY_SIZE);
    MPIR_ERR_CHECK(mpi_errno);
    
#ifdef ENABLE_CHECKPOINTING
    mpi_errno = MPIDI_nem_ckpt_pkthandler_init(pktArray, PKTARRAY_SIZE);
    MPIR_ERR_CHECK(mpi_errno);
#endif

    /* other pkt handlers */
    pktArray[MPIDI_NEM_PKT_NETMOD] = pkt_NETMOD_handler;
   
#ifdef HAVE_SIGNAL
    /* install signal handler for process failure notifications from hydra */
    prev_sighandler = signal(SIGUSR1, sigusr1_handler);
    MPIR_ERR_CHKANDJUMP1(prev_sighandler == SIG_ERR, mpi_errno, MPI_ERR_OTHER, "**signal", "**signal %s",
                         MPIR_Strerror(errno, strerrbuf, MPIR_STRERROR_BUF_SIZE));
    if (prev_sighandler == SIG_IGN || prev_sighandler == SIG_DFL ||
        prev_sighandler == sigusr1_handler) {
        prev_sighandler = NULL;
    }
#endif

 fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}

int MPIDI_CH3I_Progress_finalize(void)
{
    int mpi_errno = MPI_SUCCESS;
    qn_ent_t *ent;

    MPIR_FUNC_ENTER;

    while(qn_head) {
        ent = qn_head->next;
        MPL_free(qn_head);
        qn_head = ent;
    }

    MPIR_FUNC_EXIT;
    return mpi_errno;
}

static int shm_connection_terminated(MPIDI_VC_t * vc)
{
    /* This function is called after all sends have completed */
    int mpi_errno = MPI_SUCCESS;

    MPIR_FUNC_ENTER;

    if (vc->ch.lmt_vc_terminated) {
        mpi_errno = vc->ch.lmt_vc_terminated(vc);
        MPIR_ERR_CHECK(mpi_errno);
    }
    
    mpi_errno = MPL_shm_hnd_finalize(&(vc->ch.lmt_copy_buf_handle));
    if(mpi_errno != MPI_SUCCESS) { MPIR_ERR_POP(mpi_errno); }
    mpi_errno = MPL_shm_hnd_finalize(&(vc->ch.lmt_recv_copy_buf_handle));
    if(mpi_errno != MPI_SUCCESS) { MPIR_ERR_POP(mpi_errno); }
    
    mpi_errno = MPIDI_CH3U_Handle_connection(vc, MPIDI_VC_EVENT_TERMINATED);
    MPIR_ERR_CHECK(mpi_errno);

    MPL_DBG_MSG_D(MPIDI_CH3_DBG_DISCONNECT, TYPICAL, "Terminated VC %d", vc->pg_rank);
 fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}


int MPIDI_CH3_Connection_terminate(MPIDI_VC_t * vc)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_CHKPMEM_DECL();

    MPIR_FUNC_ENTER;

    MPL_DBG_MSG_D(MPIDI_CH3_DBG_DISCONNECT, TYPICAL, "Terminating VC %d", vc->pg_rank);

    /* if this is already closed, exit */
    if (vc->state == MPIDI_VC_STATE_MORIBUND ||
        vc->state == MPIDI_VC_STATE_INACTIVE_CLOSED)
        goto fn_exit;

    if (vc->ch.is_local) {
        MPL_DBG_MSG(MPIDI_CH3_DBG_DISCONNECT, TYPICAL, "VC is local");

        if (vc->state != MPIDI_VC_STATE_CLOSED) {
            /* VC is terminated as a result of a fault.  Complete
               outstanding sends with an error and terminate
               connection immediately. */
            MPL_DBG_MSG(MPIDI_CH3_DBG_DISCONNECT, TYPICAL, "VC terminated due to fault");
            mpi_errno = MPIDI_CH3I_Complete_sendq_with_error(vc);
            MPIR_ERR_CHECK(mpi_errno);

            mpi_errno = shm_connection_terminated(vc);
            MPIR_ERR_CHECK(mpi_errno);
        } else {
            /* VC is terminated as a result of the close protocol.
               Wait for sends to complete, then terminate. */

            if (MPIDI_CH3I_Sendq_empty(MPIDI_CH3I_shm_sendq)) {
                /* The sendq is empty, so we can immediately terminate
                   the connection. */
                MPL_DBG_MSG(MPIDI_CH3_DBG_DISCONNECT, TYPICAL, "Shm send queue empty, terminating immediately");
                mpi_errno = shm_connection_terminated(vc);
                MPIR_ERR_CHECK(mpi_errno);
            } else {
                /* There may be sends from this VC on the send queue.
                   Since there is one send queue, we don't want to
                   search the queue to find the last send from this
                   VC.  Instead, we use the last send in the queue,
                   regardless of which VC it's from.  When that send
                   completes, (since no new messages are sent on this
                   VC anymore) we know that all sends on this VC must
                   have completed.  */
                vc_term_element_t *ep;
                MPL_DBG_MSG(MPIDI_CH3_DBG_DISCONNECT, TYPICAL, "Shm send queue not empty, waiting to terminate");
                MPIR_CHKPMEM_MALLOC(ep, sizeof(vc_term_element_t), MPL_MEM_ADDRESS);
                ep->vc = vc;
                ep->req = MPIDI_CH3I_shm_sendq.tail;
                MPIR_Request_add_ref(ep->req); /* make sure this doesn't get released before we can check it */
                TERMQ_ENQUEUE(ep);
            }
        }
    
    } else {
        MPL_DBG_MSG(MPIDI_CH3_DBG_DISCONNECT, TYPICAL, "VC is remote");
        mpi_errno = MPID_nem_netmod_func->vc_terminate(vc);
        MPIR_ERR_CHECK(mpi_errno);
    }
    
 fn_exit:
    MPIR_CHKPMEM_COMMIT();
    MPIR_FUNC_EXIT;
    return mpi_errno;
 fn_fail:
    MPIR_CHKPMEM_REAP();
    goto fn_exit;
}
/* end MPIDI_CH3_Connection_terminate() */

int MPIDI_CH3I_Complete_sendq_with_error(MPIDI_VC_t * vc)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_Request *req, *prev;

    MPIR_FUNC_ENTER;

    req = MPIDI_CH3I_shm_sendq.head;
    prev = NULL;
    while (req) {
        if (req->ch.vc == vc) {
            MPIR_Request *next = req->next;
            if (prev)
                prev->next = next;
            else
                MPIDI_CH3I_shm_sendq.head = next;
            if (MPIDI_CH3I_shm_sendq.tail == req)
                MPIDI_CH3I_shm_sendq.tail = prev;

            req->status.MPI_ERROR = MPI_SUCCESS;
            MPIR_ERR_SET1(req->status.MPI_ERROR, MPIX_ERR_PROC_FAILED, "**comm_fail", "**comm_fail %d", vc->pg_rank);
            
            MPIR_Request_free(req); /* ref count was incremented when added to queue */
            mpi_errno = MPID_Request_complete(req);
            MPIR_ERR_CHECK(mpi_errno);
            req = next;
        } else {
            prev = req;
            req = req->next;
        }
    }

 fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}



static int pkt_NETMOD_handler(MPIDI_VC_t *vc, MPIDI_CH3_Pkt_t *pkt, void *data, intptr_t *buflen, MPIR_Request **rreqp)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_nem_pkt_netmod_t * const netmod_pkt = (MPID_nem_pkt_netmod_t *)pkt;
    MPIDI_CH3I_VC *vc_ch = &vc->ch;

    MPIR_FUNC_ENTER;

    MPIR_Assert_fmt_msg(vc_ch->pkt_handler && netmod_pkt->subtype < vc_ch->num_pkt_handlers, ("no handler defined for netmod-local packet"));

    mpi_errno = vc_ch->pkt_handler[netmod_pkt->subtype](vc, pkt, data, buflen, rreqp);

    MPIR_FUNC_EXIT;
    return mpi_errno;
}


int MPIDI_CH3I_Register_anysource_notification(void (*enqueue_fn)(MPIR_Request *rreq), int (*dequeue_fn)(MPIR_Request *rreq))
{
    int mpi_errno = MPI_SUCCESS;
    qn_ent_t *ent;
    MPIR_CHKPMEM_DECL();

    MPIR_CHKPMEM_MALLOC(ent, sizeof(qn_ent_t), MPL_MEM_BUFFER);

    ent->enqueue_fn = enqueue_fn;
    ent->dequeue_fn = dequeue_fn;
    ent->next = qn_head;
    qn_head = ent;

 fn_exit:
    MPIR_CHKPMEM_COMMIT();
    return mpi_errno;
 fn_fail:
    MPIR_CHKPMEM_REAP();
    goto fn_exit;
}

static void anysource_posted(MPIR_Request *rreq)
{
    qn_ent_t *ent = qn_head;

    /* call all of the registered handlers */
    while (ent)
    {
        if (ent->enqueue_fn)
        {
            ent->enqueue_fn(rreq);
        }
        ent = ent->next;
    }
}

static int anysource_matched(MPIR_Request *rreq)
{
    int matched = FALSE;
    qn_ent_t *ent = qn_head;

    /* call all of the registered handlers */
    while(ent) {
        if (ent->dequeue_fn)
        {
            int m;
            
            m = ent->dequeue_fn(rreq);
            
            /* this is a crude check to check if the req has been
               matched by more than one netmod.  When MPIR_Assert() is
               defined to empty, the extra matched=m is optimized
               away. */
            MPIR_Assert(!m || !matched);
            matched = m;
        }
        ent = ent->next;
    }

    return matched;
}

void MPIDI_CH3I_Posted_recv_enqueued(MPIR_Request *rreq)
{

    MPIR_FUNC_ENTER;

    /* MT FIXME acquiring MPIDCOMM here violates lock ordering rules,
     * easily causes deadlock */

    if ((rreq)->dev.match.parts.rank == MPI_ANY_SOURCE)
        /* call anysource handler */
	anysource_posted(rreq);
    else
    {
        int local_rank = -1;
	MPIDI_VC_t *vc;

        /* MT FIXME does this macro need some sort of synchronization too? */
	MPIDI_Comm_get_vc((rreq)->comm, (rreq)->dev.match.parts.rank, &vc);

#ifdef ENABLE_COMM_OVERRIDES
        /* call vc-specific handler */
	if (vc->comm_ops && vc->comm_ops->recv_posted)
            vc->comm_ops->recv_posted(vc, rreq);
#endif

        /* enqueue fastbox */

        /* don't enqueue a fastbox for yourself */
        MPIR_Assert(rreq->comm != NULL);
        if (rreq->dev.match.parts.rank == rreq->comm->rank)
            goto fn_exit;

        /* don't enqueue non-local processes */
        if (!vc->ch.is_local)
            goto fn_exit;

        /* Translate the communicator rank to a local rank.  Note that there is an
           implicit assumption here that because is_local is true above, that these
           processes are in the same PG. */
        local_rank = MPID_NEM_LOCAL_RANK(vc->pg_rank);

        MPID_nem_mpich_enqueue_fastbox(local_rank);
    }

 fn_exit:
    MPIR_FUNC_EXIT;
}

/* returns non-zero when req has been matched by channel */
int MPIDI_CH3I_Posted_recv_dequeued(MPIR_Request *rreq)
{
    int local_rank = -1;
    MPIDI_VC_t *vc;
    int matched = FALSE;

    MPIR_FUNC_ENTER;

    if (rreq->dev.match.parts.rank == MPI_ANY_SOURCE)
    {
	matched = anysource_matched(rreq);
    }
    else
    {
        if (rreq->dev.match.parts.rank == rreq->comm->rank)
            goto fn_exit;

        /* don't use MPID_NEM_IS_LOCAL, it doesn't handle dynamic processes */
        MPIDI_Comm_get_vc(rreq->comm, rreq->dev.match.parts.rank, &vc);
        MPIR_Assert(vc != NULL);
        if (!vc->ch.is_local)
            goto fn_exit;

        /* Translate the communicator rank to a local rank.  Note that there is an
           implicit assumption here that because is_local is true above, that these
           processes are in the same PG. */
        local_rank = MPID_NEM_LOCAL_RANK(vc->pg_rank);

        MPID_nem_mpich_dequeue_fastbox(local_rank);
    }

 fn_exit:
    MPIR_FUNC_EXIT;
    return matched;
}

