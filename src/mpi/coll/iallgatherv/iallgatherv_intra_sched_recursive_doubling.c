/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpiimpl.h"

int MPIR_Iallgatherv_intra_sched_recursive_doubling(const void *sendbuf, MPI_Aint sendcount,
                                                    MPI_Datatype sendtype, void *recvbuf,
                                                    const MPI_Aint recvcounts[],
                                                    const MPI_Aint displs[], MPI_Datatype recvtype,
                                                    MPIR_Comm * comm_ptr, MPIR_Sched_t s)
{
    int mpi_errno = MPI_SUCCESS;
    int comm_size, rank, i, j, k;
    int mask, dst, my_tree_root, dst_tree_root;
    MPI_Aint recvtype_extent, recvtype_sz, position, offset;
    void *tmp_buf = NULL;

    MPIR_COMM_RANK_SIZE(comm_ptr, rank, comm_size);

#ifdef HAVE_ERROR_CHECKING
    /* Currently this algorithm can only handle power-of-2 comm_size.
     * Non power-of-2 comm_size is still experimental */
    MPIR_Assert(!(comm_size & (comm_size - 1)));
#endif /* HAVE_ERROR_CHECKING */

    /* need to receive contiguously into tmp_buf because
     * displs could make the recvbuf noncontiguous */
    MPIR_Datatype_get_size_macro(recvtype, recvtype_sz);
    MPIR_Datatype_get_extent_macro(recvtype, recvtype_extent);

    MPI_Aint total_count;
    total_count = 0;
    for (i = 0; i < comm_size; i++)
        total_count += recvcounts[i];

    if (total_count == 0)
        goto fn_exit;

    tmp_buf = MPIR_Sched_alloc_state(s, total_count * recvtype_sz);
    MPIR_ERR_CHKANDJUMP(!tmp_buf, mpi_errno, MPI_ERR_OTHER, "**nomem");

    /* copy local data into right location in tmp_buf */
    position = 0;
    for (i = 0; i < rank; i++)
        position += recvcounts[i];
    if (sendbuf != MPI_IN_PLACE) {
        mpi_errno = MPIR_Sched_copy(sendbuf, sendcount, sendtype,
                                    ((char *) tmp_buf + position * recvtype_sz),
                                    recvcounts[rank] * recvtype_sz, MPIR_BYTE_INTERNAL, s);
        MPIR_ERR_CHECK(mpi_errno);
    } else {
        /* if in_place specified, local data is found in recvbuf */
        mpi_errno = MPIR_Sched_copy(((char *) recvbuf + displs[rank] * recvtype_extent),
                                    recvcounts[rank], recvtype,
                                    ((char *) tmp_buf + position * recvtype_sz),
                                    recvcounts[rank] * recvtype_sz, MPIR_BYTE_INTERNAL, s);
        MPIR_ERR_CHECK(mpi_errno);
    }

    MPI_Aint curr_count;
    curr_count = recvcounts[rank];

    /* never used uninitialized w/o this, but compiler can't tell that */
    MPI_Aint incoming_count;
    incoming_count = -1;

    /* [goodell@] random notes that help slightly when deciphering this code:
     * - mask is also equal to the number of blocks that we are going to recv
     *   (less if comm_size is non-pof2)
     * - FOO_tree_root is the leftmost (lowest ranked) process with whom FOO has
     *   communicated, directly or indirectly, at the beginning of round the
     *   round.  FOO is either "dst" or "my", where "my" means use my rank.
     * - in each round we are going to recv the blocks
     *   B[dst_tree_root],B[dst_tree_root+1],...,B[min(dst_tree_root+mask,comm_size)]
     */
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

        if (dst < comm_size) {
            MPI_Aint send_offset, recv_offset;

            send_offset = 0;
            for (j = 0; j < my_tree_root; j++)
                send_offset += recvcounts[j];

            recv_offset = 0;
            for (j = 0; j < dst_tree_root; j++)
                recv_offset += recvcounts[j];

            incoming_count = 0;
            for (j = dst_tree_root; j < (dst_tree_root + mask) && j < comm_size; ++j)
                incoming_count += recvcounts[j];

            mpi_errno = MPIR_Sched_send(((char *) tmp_buf + send_offset * recvtype_sz),
                                        curr_count * recvtype_sz, MPIR_BYTE_INTERNAL, dst, comm_ptr,
                                        s);
            MPIR_ERR_CHECK(mpi_errno);
            /* sendrecv, no barrier here */
            mpi_errno = MPIR_Sched_recv(((char *) tmp_buf + recv_offset * recvtype_sz),
                                        incoming_count * recvtype_sz, MPIR_BYTE_INTERNAL, dst,
                                        comm_ptr, s);
            MPIR_ERR_CHECK(mpi_errno);
            MPIR_SCHED_BARRIER(s);

            curr_count += incoming_count;
        }

        /* if some processes in this process's subtree in this step
         * did not have any destination process to communicate with
         * because of non-power-of-two, we need to send them the
         * data that they would normally have received from those
         * processes. That is, the haves in this subtree must send to
         * the havenots. We use a logarithmic
         * recursive-halfing algorithm for this. */

        /* This part of the code will not currently be
         * executed because we are not using recursive
         * doubling for non power of two. Mark it as experimental
         * so that it doesn't show up as red in the coverage
         * tests. */

        /* --BEGIN EXPERIMENTAL-- */
        if (dst_tree_root + mask > comm_size) {
            int tmp_mask, tree_root;
            int nprocs_completed = comm_size - my_tree_root - mask;
            /* nprocs_completed is the number of processes in this
             * subtree that have all the data. Send data to others
             * in a tree fashion. First find root of current tree
             * that is being divided into two. k is the number of
             * least-significant bits in this process's rank that
             * must be zeroed out to find the rank of the root */
            /* [goodell@] it looks like (k==i) is always true, could possibly
             * skip the loop below */
            j = mask;
            k = 0;
            while (j) {
                j >>= 1;
                k++;
            }
            k--;

            tmp_mask = mask >> 1;

            while (tmp_mask) {
                dst = rank ^ tmp_mask;

                tree_root = rank >> k;
                tree_root <<= k;

                /* send only if this proc has data and destination
                 * doesn't have data. at any step, multiple processes
                 * can send if they have the data */
                if ((dst > rank) &&
                    (rank < tree_root + nprocs_completed) &&
                    (dst >= tree_root + nprocs_completed)) {
                    offset = 0;
                    for (j = 0; j < (my_tree_root + mask); j++)
                        offset += recvcounts[j];
                    offset *= recvtype_sz;

                    /* incoming_count was set in the previous
                     * receive. that's the amount of data to be
                     * sent now. */
                    mpi_errno = MPIR_Sched_send(((char *) tmp_buf + offset),
                                                incoming_count * recvtype_sz, MPIR_BYTE_INTERNAL,
                                                dst, comm_ptr, s);
                    MPIR_ERR_CHECK(mpi_errno);
                    MPIR_SCHED_BARRIER(s);
                }
                /* recv only if this proc. doesn't have data and sender
                 * has data */
                else if ((dst < rank) &&
                         (dst < tree_root + nprocs_completed) &&
                         (rank >= tree_root + nprocs_completed)) {

                    offset = 0;
                    for (j = 0; j < (my_tree_root + mask); j++)
                        offset += recvcounts[j];

                    /* recalculate incoming_count, since not all processes will have
                     * this value */
                    incoming_count = 0;
                    for (j = dst_tree_root; j < (dst_tree_root + mask) && j < comm_size; ++j)
                        incoming_count += recvcounts[j];

                    mpi_errno = MPIR_Sched_recv(((char *) tmp_buf + offset * recvtype_sz),
                                                incoming_count * recvtype_sz, MPIR_BYTE_INTERNAL,
                                                dst, comm_ptr, s);
                    MPIR_ERR_CHECK(mpi_errno);
                    MPIR_SCHED_BARRIER(s);
                    curr_count += incoming_count;
                }
                tmp_mask >>= 1;
                k--;
            }
        }
        /* --END EXPERIMENTAL-- */

        mask <<= 1;
        i++;
    }

    /* sanity check that we got all of the data blocks */
    MPIR_Assert(curr_count == total_count);

    /* copy data from tmp_buf to recvbuf */
    position = 0;
    for (j = 0; j < comm_size; j++) {
        if ((sendbuf != MPI_IN_PLACE) || (j != rank)) {
            /* not necessary to copy if in_place and
             * j==rank. otherwise copy. */
            mpi_errno = MPIR_Sched_copy(((char *) tmp_buf + position * recvtype_sz),
                                        recvcounts[j] * recvtype_sz, MPIR_BYTE_INTERNAL,
                                        ((char *) recvbuf + displs[j] * recvtype_extent),
                                        recvcounts[j], recvtype, s);
            MPIR_ERR_CHECK(mpi_errno);
        }
        position += recvcounts[j];
    }

  fn_exit:
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}
