/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 *  (C) 2017 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#ifndef CH4I_WORKQ_TYPES_H_INCLUDED
#define CH4I_WORKQ_TYPES_H_INCLUDED

/* Define the work queue implementation type */
#if defined(MPIDI_USE_NMQUEUE)
#include <queue/zm_nmqueue.h>
#define MPIDI_workq_t                   zm_nmqueue_t
#define MPIDI_workq_init(q, n)          zm_nmqueue_init(q)
#define MPIDI_workq_enqueue(q, d, i)    zm_nmqueue_enqueue(q, d)
#define MPIDI_workq_dequeue(q, d)       zm_nmqueue_dequeue(q, d)
#elif defined(MPIDI_USE_MSQUEUE)
#include <queue/zm_msqueue.h>
#define MPIDI_workq_t                   zm_msqueue_t
#define MPIDI_workq_init(q, n)          zm_msqueue_init(q)
#define MPIDI_workq_enqueue(q, d, i)    zm_msqueue_enqueue(q, d)
#define MPIDI_workq_dequeue(q, d)       zm_msqueue_dequeue(q, d)
#elif defined(MPIDI_USE_GLQUEUE)
#include <queue/zm_glqueue.h>
#define MPIDI_workq_t                   zm_glqueue_t
#define MPIDI_workq_init(q, n)          zm_glqueue_init(q)
#define MPIDI_workq_enqueue(q, d, i)    zm_glqueue_enqueue(q, d)
#define MPIDI_workq_dequeue(q, d)       zm_glqueue_dequeue(q, d)
#elif defined(MPIDI_USE_MPBQUEUE)
#include <queue/zm_mpbqueue.h>
#define MPIDI_workq_t                   struct zm_mpbqueue
#define MPIDI_workq_init(q, n)          zm_mpbqueue_init(q, n)
#define MPIDI_workq_enqueue(q, d, i)    zm_mpbqueue_enqueue(q, d, i)
#define MPIDI_workq_dequeue(q, d)       zm_mpbqueue_dequeue(q, d)

#else
/* Stub implementation to make it compile */
typedef void *MPIDI_workq_t;
MPL_STATIC_INLINE_PREFIX void MPIDI_workq_init(MPIDI_workq_t *q, int nbuckets) {}
MPL_STATIC_INLINE_PREFIX void MPIDI_workq_enqueue(MPIDI_workq_t *q, void *p, int bucket_idx) {}
MPL_STATIC_INLINE_PREFIX void MPIDI_workq_dequeue(MPIDI_workq_t *q, void **pp) {}
#endif

typedef enum MPIDI_workq_op MPIDI_workq_op_t;
enum MPIDI_workq_op {SEND, ISEND, RECV, IRECV, PUT};

typedef struct MPIDI_workq_elemt MPIDI_workq_elemt_t;
typedef struct MPIDI_workq_list  MPIDI_workq_list_t;

struct MPIDI_workq_elemt {
    MPIDI_workq_op_t op;
    union {
        /* Point-to-Point */
        struct {
            const void *send_buf;
            void *recv_buf;
            MPI_Aint count;
            MPI_Datatype datatype;
            int rank;
            int tag;
            MPIR_Comm *comm_ptr;
            int context_offset;
            MPI_Status *status;
            MPIR_Request *request;
        };
        /* RMA */
        struct {
            const void *origin_addr;
            int origin_count;
            MPI_Datatype origin_datatype;
            int target_rank;
            MPI_Aint target_disp;
            int target_count;
            MPI_Datatype target_datatype;
            MPIR_Win *win_ptr;
        };
    };
};

#endif /* CH4I_WORKQ_TYPES_H_INCLUDED */
