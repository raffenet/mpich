/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpiimpl.h"

/* also known as "Rabenseifner's algorithm" */
int MPIR_Iallreduce_intra_sched_reduce_scatter_allgather(const void *sendbuf, void *recvbuf,
                                                         MPI_Aint count, MPI_Datatype datatype,
                                                         MPI_Op op, MPIR_Comm * comm_ptr,
                                                         MPIR_Sched_t s)
{
    int mpi_errno = MPI_SUCCESS;
    int comm_size, rank, newrank, pof2, rem;
    int i, send_idx, recv_idx, last_idx, mask, newdst, dst;
    MPI_Aint true_lb, true_extent, extent;
    void *tmp_buf = NULL;
    MPIR_CHKLMEM_DECL();

#ifdef HAVE_ERROR_CHECKING
    /* we only support builtin datatypes for now, breaking up user types to do
     * the reduce-scatter is tricky */
    MPIR_Assert(HANDLE_IS_BUILTIN(op));
#endif

    MPIR_COMM_RANK_SIZE(comm_ptr, rank, comm_size);

    /* need to allocate temporary buffer to store incoming data */
    MPIR_Type_get_true_extent_impl(datatype, &true_lb, &true_extent);
    MPIR_Datatype_get_extent_macro(datatype, extent);

    tmp_buf = MPIR_Sched_alloc_state(s, count * (MPL_MAX(extent, true_extent)));
    MPIR_ERR_CHKANDJUMP(!tmp_buf, mpi_errno, MPI_ERR_OTHER, "**nomem");

    /* adjust for potential negative lower bound in datatype */
    tmp_buf = (void *) ((char *) tmp_buf - true_lb);

    /* copy local data into recvbuf */
    if (sendbuf != MPI_IN_PLACE) {
        mpi_errno = MPIR_Sched_copy(sendbuf, count, datatype, recvbuf, count, datatype, s);
        MPIR_ERR_CHECK(mpi_errno);
        MPIR_SCHED_BARRIER(s);
    }

    /* get nearest power-of-two less than or equal to comm_size */
    pof2 = MPL_pof2(comm_size);

    rem = comm_size - pof2;

    /* In the non-power-of-two case, all even-numbered
     * processes of rank < 2*rem send their data to
     * (rank+1). These even-numbered processes no longer
     * participate in the algorithm until the very end. The
     * remaining processes form a nice power-of-two. */

    if (rank < 2 * rem) {
        if (rank % 2 == 0) {    /* even */
            mpi_errno = MPIR_Sched_send(recvbuf, count, datatype, rank + 1, comm_ptr, s);
            MPIR_ERR_CHECK(mpi_errno);
            MPIR_SCHED_BARRIER(s);

            /* temporarily set the rank to -1 so that this
             * process does not pariticipate in recursive
             * doubling */
            newrank = -1;
        } else {        /* odd */
            mpi_errno = MPIR_Sched_recv(tmp_buf, count, datatype, rank - 1, comm_ptr, s);
            MPIR_ERR_CHECK(mpi_errno);
            MPIR_SCHED_BARRIER(s);

            /* do the reduction on received data. since the
             * ordering is right, it doesn't matter whether
             * the operation is commutative or not. */
            mpi_errno = MPIR_Sched_reduce(tmp_buf, recvbuf, count, datatype, op, s);
            MPIR_ERR_CHECK(mpi_errno);
            MPIR_SCHED_BARRIER(s);

            /* change the rank */
            newrank = rank / 2;
        }
    } else      /* rank >= 2*rem */
        newrank = rank - rem;

    if (newrank != -1) {
        /* for the reduce-scatter, calculate the count that
         * each process receives and the displacement within
         * the buffer */
        /* TODO I (goodell@) believe that these counts and displacements could be
         * calculated directly during the loop, rather than requiring a less-scalable
         * "2*pof2"-sized memory allocation */

        MPI_Aint *cnts, *disps;
        MPIR_CHKLMEM_MALLOC(cnts, pof2 * sizeof(MPI_Aint));
        MPIR_CHKLMEM_MALLOC(disps, pof2 * sizeof(MPI_Aint));

        MPIR_Assert(count >= pof2);     /* the cnts calculations assume this */
        for (i = 0; i < (pof2 - 1); i++)
            cnts[i] = count / pof2;
        cnts[pof2 - 1] = count - (count / pof2) * (pof2 - 1);

        if (pof2)
            disps[0] = 0;
        for (i = 1; i < pof2; i++)
            disps[i] = disps[i - 1] + cnts[i - 1];

        mask = 0x1;
        send_idx = recv_idx = 0;
        last_idx = pof2;
        while (mask < pof2) {
            newdst = newrank ^ mask;
            /* find real rank of dest */
            dst = (newdst < rem) ? newdst * 2 + 1 : newdst + rem;

            MPI_Aint send_cnt, recv_cnt;
            send_cnt = recv_cnt = 0;
            if (newrank < newdst) {
                send_idx = recv_idx + pof2 / (mask * 2);
                for (i = send_idx; i < last_idx; i++)
                    send_cnt += cnts[i];
                for (i = recv_idx; i < send_idx; i++)
                    recv_cnt += cnts[i];
            } else {
                recv_idx = send_idx + pof2 / (mask * 2);
                for (i = send_idx; i < recv_idx; i++)
                    send_cnt += cnts[i];
                for (i = recv_idx; i < last_idx; i++)
                    recv_cnt += cnts[i];
            }

            /* Send data from recvbuf. Recv into tmp_buf */
            mpi_errno = MPIR_Sched_recv(((char *) tmp_buf + disps[recv_idx] * extent),
                                        recv_cnt, datatype, dst, comm_ptr, s);
            MPIR_ERR_CHECK(mpi_errno);
            /* sendrecv, no barrier here */
            mpi_errno = MPIR_Sched_send(((char *) recvbuf + disps[send_idx] * extent),
                                        send_cnt, datatype, dst, comm_ptr, s);
            MPIR_ERR_CHECK(mpi_errno);
            MPIR_SCHED_BARRIER(s);

            /* tmp_buf contains data received in this step.
             * recvbuf contains data accumulated so far */

            /* This algorithm is used only for predefined ops
             * and predefined ops are always commutative. */
            mpi_errno = MPIR_Sched_reduce(((char *) tmp_buf + disps[recv_idx] * extent),
                                          ((char *) recvbuf + disps[recv_idx] * extent),
                                          recv_cnt, datatype, op, s);
            MPIR_ERR_CHECK(mpi_errno);
            MPIR_SCHED_BARRIER(s);

            /* update send_idx for next iteration */
            send_idx = recv_idx;
            mask <<= 1;

            /* update last_idx, but not in last iteration
             * because the value is needed in the allgather
             * step below. */
            if (mask < pof2)
                last_idx = recv_idx + pof2 / mask;
        }

        /* now do the allgather */

        mask >>= 1;
        while (mask > 0) {
            newdst = newrank ^ mask;
            /* find real rank of dest */
            dst = (newdst < rem) ? newdst * 2 + 1 : newdst + rem;

            MPI_Aint send_cnt, recv_cnt;
            send_cnt = recv_cnt = 0;
            if (newrank < newdst) {
                /* update last_idx except on first iteration */
                if (mask != pof2 / 2)
                    last_idx = last_idx + pof2 / (mask * 2);

                recv_idx = send_idx + pof2 / (mask * 2);
                for (i = send_idx; i < recv_idx; i++)
                    send_cnt += cnts[i];
                for (i = recv_idx; i < last_idx; i++)
                    recv_cnt += cnts[i];
            } else {
                recv_idx = send_idx - pof2 / (mask * 2);
                for (i = send_idx; i < last_idx; i++)
                    send_cnt += cnts[i];
                for (i = recv_idx; i < send_idx; i++)
                    recv_cnt += cnts[i];
            }

            mpi_errno = MPIR_Sched_recv(((char *) recvbuf + disps[recv_idx] * extent),
                                        recv_cnt, datatype, dst, comm_ptr, s);
            MPIR_ERR_CHECK(mpi_errno);
            /* sendrecv, no barrier here */
            mpi_errno = MPIR_Sched_send(((char *) recvbuf + disps[send_idx] * extent),
                                        send_cnt, datatype, dst, comm_ptr, s);
            MPIR_ERR_CHECK(mpi_errno);
            MPIR_SCHED_BARRIER(s);

            if (newrank > newdst)
                send_idx = recv_idx;

            mask >>= 1;
        }
    }

    /* In the non-power-of-two case, all odd-numbered
     * processes of rank < 2*rem send the result to
     * (rank-1), the ranks who didn't participate above. */
    if (rank < 2 * rem) {
        if (rank % 2) { /* odd */
            mpi_errno = MPIR_Sched_send(recvbuf, count, datatype, rank - 1, comm_ptr, s);
            MPIR_ERR_CHECK(mpi_errno);
        } else {        /* even */
            mpi_errno = MPIR_Sched_recv(recvbuf, count, datatype, rank + 1, comm_ptr, s);
            MPIR_ERR_CHECK(mpi_errno);
        }
    }

  fn_exit:
    MPIR_CHKLMEM_FREEALL();
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}
