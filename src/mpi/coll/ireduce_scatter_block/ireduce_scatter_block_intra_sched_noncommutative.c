/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpiimpl.h"

/* Implements the reduce-scatter butterfly algorithm described in J. L. Traff's
 * "An Improved Algorithm for (Non-commutative) Reduce-Scatter with an Application"
 * from EuroPVM/MPI 2005.  This function currently only implements support for
 * the power-of-2 case. */
int MPIR_Ireduce_scatter_block_intra_sched_noncommutative(const void *sendbuf, void *recvbuf,
                                                          MPI_Aint recvcount, MPI_Datatype datatype,
                                                          MPI_Op op, MPIR_Comm * comm_ptr,
                                                          MPIR_Sched_t s)
{
    int mpi_errno = MPI_SUCCESS;
    int comm_size;
    int rank;
    int log2_comm_size;
    int i, k;
    MPI_Aint true_extent, true_lb;
    int buf0_was_inout;
    void *tmp_buf0;
    void *tmp_buf1;
    void *result_ptr;

    MPIR_COMM_RANK_SIZE(comm_ptr, rank, comm_size);

    MPIR_Type_get_true_extent_impl(datatype, &true_lb, &true_extent);

#ifdef HAVE_ERROR_CHECKING
    /* begin error checking */
    MPIR_Assert(MPL_pof2(comm_size));   /* FIXME this version only works for power of 2 procs */
    /* end error checking */
#endif

    log2_comm_size = MPL_log2(comm_size);

    /* size of a block (count of datatype per block, NOT bytes per block) */
    MPI_Aint block_size, total_count;
    block_size = recvcount;
    total_count = block_size * comm_size;

    tmp_buf0 = MPIR_Sched_alloc_state(s, true_extent * total_count);
    MPIR_ERR_CHKANDJUMP(!tmp_buf0, mpi_errno, MPI_ERR_OTHER, "**nomem");
    tmp_buf1 = MPIR_Sched_alloc_state(s, true_extent * total_count);
    MPIR_ERR_CHKANDJUMP(!tmp_buf1, mpi_errno, MPI_ERR_OTHER, "**nomem");

    /* adjust for potential negative lower bound in datatype */
    tmp_buf0 = (void *) ((char *) tmp_buf0 - true_lb);
    tmp_buf1 = (void *) ((char *) tmp_buf1 - true_lb);

    /* Copy our send data to tmp_buf0.  We do this one block at a time and
     * permute the blocks as we go according to the mirror permutation. */
    for (i = 0; i < comm_size; ++i) {
        mpi_errno =
            MPIR_Sched_copy(((char *) (sendbuf ==
                                       MPI_IN_PLACE ? (const void *) recvbuf : sendbuf) +
                             (i * true_extent * block_size)), block_size, datatype,
                            ((char *) tmp_buf0 +
                             (MPL_mirror_permutation(i, log2_comm_size) * true_extent *
                              block_size)), block_size, datatype, s);
        MPIR_ERR_CHECK(mpi_errno);
    }
    MPIR_SCHED_BARRIER(s);
    buf0_was_inout = 1;

    MPI_Aint send_offset, recv_offset, size;
    send_offset = 0;
    recv_offset = 0;
    size = total_count;
    for (k = 0; k < log2_comm_size; ++k) {
        /* use a double-buffering scheme to avoid local copies */
        char *incoming_data = (buf0_was_inout ? tmp_buf1 : tmp_buf0);
        char *outgoing_data = (buf0_was_inout ? tmp_buf0 : tmp_buf1);
        int peer = rank ^ (0x1 << k);
        size /= 2;

        if (rank > peer) {
            /* we have the higher rank: send top half, recv bottom half */
            recv_offset += size;
        } else {
            /* we have the lower rank: recv top half, send bottom half */
            send_offset += size;
        }

        mpi_errno = MPIR_Sched_send((outgoing_data + send_offset * true_extent),
                                    size, datatype, peer, comm_ptr, s);
        MPIR_ERR_CHECK(mpi_errno);
        mpi_errno = MPIR_Sched_recv((incoming_data + recv_offset * true_extent),
                                    size, datatype, peer, comm_ptr, s);
        MPIR_ERR_CHECK(mpi_errno);
        MPIR_SCHED_BARRIER(s);

        /* always perform the reduction at recv_offset, the data at send_offset
         * is now our peer's responsibility */
        if (rank > peer) {
            /* higher ranked value so need to call op(received_data, my_data) */
            mpi_errno = MPIR_Sched_reduce((incoming_data + recv_offset * true_extent),
                                          (outgoing_data + recv_offset * true_extent),
                                          size, datatype, op, s);
            MPIR_ERR_CHECK(mpi_errno);
        } else {
            /* lower ranked value so need to call op(my_data, received_data) */
            mpi_errno = MPIR_Sched_reduce((outgoing_data + recv_offset * true_extent),
                                          (incoming_data + recv_offset * true_extent),
                                          size, datatype, op, s);
            MPIR_ERR_CHECK(mpi_errno);
            buf0_was_inout = !buf0_was_inout;
        }
        MPIR_SCHED_BARRIER(s);

        /* the next round of send/recv needs to happen within the block (of size
         * "size") that we just received and reduced */
        send_offset = recv_offset;
    }

    MPIR_Assert(size == recvcount);

    /* copy the reduced data to the recvbuf */
    result_ptr = (char *) (buf0_was_inout ? tmp_buf0 : tmp_buf1) + recv_offset * true_extent;
    mpi_errno = MPIR_Sched_copy(result_ptr, size, datatype, recvbuf, size, datatype, s);
    MPIR_ERR_CHECK(mpi_errno);

  fn_exit:
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}
