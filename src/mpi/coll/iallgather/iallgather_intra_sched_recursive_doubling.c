/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpiimpl.h"

/* helper callbacks and associated state structures */
struct shared_state {
    int recvtype;
    int recvcount;
    MPI_Aint curr_count;
    MPI_Aint last_recv_count;
    MPI_Status status;
};
static int get_count(MPIR_Comm * comm, int tag, void *state)
{
    struct shared_state *ss = state;
    MPI_Aint recv_count;
    MPIR_Get_count_impl(&ss->status, ss->recvtype, &recv_count);
    ss->last_recv_count = recv_count;
    ss->curr_count += ss->last_recv_count;
    return MPI_SUCCESS;
}

static int reset_shared_state(MPIR_Comm * comm, int tag, void *state)
{
    struct shared_state *ss = state;
    ss->curr_count = ss->recvcount;

    return MPI_SUCCESS;
}

/*
 * Recursive Doubling Algorithm:
 *
 * Restrictions: power-of-two no. of processes
 *
 * Cost = lgp.alpha + n.((p-1)/p).beta
 *
 * TODO: On TCP, we may want to use recursive doubling instead of the
 * Bruck's algorithm in all cases because of the pairwise-exchange
 * property of recursive doubling (see Benson et al paper in Euro
 * PVM/MPI 2003).
 */
int MPIR_Iallgather_intra_sched_recursive_doubling(const void *sendbuf, MPI_Aint sendcount,
                                                   MPI_Datatype sendtype, void *recvbuf,
                                                   MPI_Aint recvcount, MPI_Datatype recvtype,
                                                   MPIR_Comm * comm_ptr, MPIR_Sched_t s)
{
    int mpi_errno = MPI_SUCCESS;
    struct shared_state *ss = NULL;
    int comm_size, rank;
    int i, j, k;
    int mask, tmp_mask, dst;
    int dst_tree_root, my_tree_root, tree_root;
    MPI_Aint recvtype_extent;

    MPIR_COMM_RANK_SIZE(comm_ptr, rank, comm_size);

#ifdef HAVE_ERROR_CHECKING
    /* Currently this algorithm can only handle power-of-2 comm_size.
     * Non power-of-2 comm_size is still experimental */
    MPIR_Assert(!(comm_size & (comm_size - 1)));
#endif /* HAVE_ERROR_CHECKING */

    MPIR_Datatype_get_extent_macro(recvtype, recvtype_extent);

    /*  copy local data into recvbuf */
    if (sendbuf != MPI_IN_PLACE) {
        mpi_errno = MPIR_Sched_copy(sendbuf, sendcount, sendtype,
                                    ((char *) recvbuf + rank * recvcount * recvtype_extent),
                                    recvcount, recvtype, s);
        MPIR_ERR_CHECK(mpi_errno);
        MPIR_SCHED_BARRIER(s);
    }

    ss = MPIR_Sched_alloc_state(s, sizeof(struct shared_state));
    MPIR_ERR_CHKANDJUMP(!ss, mpi_errno, MPI_ERR_OTHER, "**nomem");
    ss->curr_count = recvcount;
    ss->recvtype = recvtype;
    ss->recvcount = recvcount;

    mask = 0x1;
    i = 0;
    while (mask < comm_size) {
        dst = rank ^ mask;

        /* find offset into send and recv buffers. zero out
         * the least significant "i" bits of rank and dst to
         * find root of src and dst subtrees. Use ranks of
         * roots as index to send from and recv into buffer */

        dst_tree_root = dst >> i;
        dst_tree_root <<= i;

        my_tree_root = rank >> i;
        my_tree_root <<= i;

        MPI_Aint send_offset, recv_offset;
        send_offset = my_tree_root * recvcount * recvtype_extent;
        recv_offset = dst_tree_root * recvcount * recvtype_extent;

        if (dst < comm_size) {
            mpi_errno = MPIR_Sched_send_defer(((char *) recvbuf + send_offset),
                                              &ss->curr_count, recvtype, dst, comm_ptr, s);
            MPIR_ERR_CHECK(mpi_errno);
            /* send-recv, no sched barrier here */
            mpi_errno = MPIR_Sched_recv_status(((char *) recvbuf + recv_offset),
                                               ((comm_size - dst_tree_root) * recvcount),
                                               recvtype, dst, comm_ptr, &ss->status, s);
            MPIR_ERR_CHECK(mpi_errno);
            MPIR_SCHED_BARRIER(s);

            mpi_errno = MPIR_Sched_cb(&get_count, ss, s);
            MPIR_ERR_CHECK(mpi_errno);
            MPIR_SCHED_BARRIER(s);
        }

        /* if some processes in this process's subtree in this step
         * did not have any destination process to communicate with
         * because of non-power-of-two, we need to send them the
         * data that they would normally have received from those
         * processes. That is, the haves in this subtree must send to
         * the havenots. We use a logarithmic recursive-halfing algorithm
         * for this. */

        /* This part of the code will not currently be
         * executed because we are not using recursive
         * doubling for non power of two. Mark it as experimental
         * so that it doesn't show up as red in the coverage
         * tests. */
        /* --BEGIN EXPERIMENTAL-- */
        if (dst_tree_root + mask > comm_size) {
            int nprocs_completed = comm_size - my_tree_root - mask;
            /* nprocs_completed is the number of processes in this
             * subtree that have all the data. Send data to others
             * in a tree fashion. First find root of current tree
             * that is being divided into two. k is the number of
             * least-significant bits in this process's rank that
             * must be zeroed out to find the rank of the root */
            j = mask;
            k = 0;
            while (j) {
                j >>= 1;
                k++;
            }
            k--;

            MPI_Aint offset;
            offset = recvcount * (my_tree_root + mask) * recvtype_extent;
            tmp_mask = mask >> 1;

            while (tmp_mask) {
                dst = rank ^ tmp_mask;

                tree_root = rank >> k;
                tree_root <<= k;

                /* send only if this proc has data and destination
                 * doesn't have data. at any step, multiple processes
                 * can send if they have the data */
                if ((dst > rank) && (rank < tree_root + nprocs_completed)
                    && (dst >= tree_root + nprocs_completed)) {
                    /* last_recv_count was set in the previous
                     * receive. that's the amount of data to be
                     * sent now. */
                    mpi_errno = MPIR_Sched_send_defer(((char *) recvbuf + offset),
                                                      &ss->last_recv_count,
                                                      recvtype, dst, comm_ptr, s);
                    MPIR_ERR_CHECK(mpi_errno);
                    MPIR_SCHED_BARRIER(s);
                }
                /* recv only if this proc. doesn't have data and sender
                 * has data */
                else if ((dst < rank) &&
                         (dst < tree_root + nprocs_completed) &&
                         (rank >= tree_root + nprocs_completed)) {
                    /* nprocs_completed is also equal to the
                     * no. of processes whose data we don't have */
                    mpi_errno = MPIR_Sched_recv_status(((char *) recvbuf + offset),
                                                       ((comm_size -
                                                         (my_tree_root + mask)) * recvcount),
                                                       recvtype, dst, comm_ptr, &ss->status, s);
                    MPIR_SCHED_BARRIER(s);
                    mpi_errno = MPIR_Sched_cb(&get_count, ss, s);
                    MPIR_ERR_CHECK(mpi_errno);
                    MPIR_SCHED_BARRIER(s);
                }

                tmp_mask >>= 1;
                k--;
            }
        }
        /* --END EXPERIMENTAL-- */

        mask <<= 1;
        i++;
    }

    mpi_errno = MPIR_Sched_cb(reset_shared_state, ss, s);
    MPIR_ERR_CHECK(mpi_errno);

  fn_exit:
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}
