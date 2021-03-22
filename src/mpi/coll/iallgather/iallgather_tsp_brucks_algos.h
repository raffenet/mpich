/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

/* Header protection (i.e., IALLGATHER_TSP_BRUCKS_ALGOS_H_INCLUDED) is
 * intentionally omitted since this header might get included multiple
 * times within the same .c file. */

#include "algo_common.h"
#include "tsp_namespace_def.h"

int
MPIR_TSP_Iallgather_sched_intra_brucks(const void *sendbuf, MPI_Aint sendcount,
                                       MPI_Datatype sendtype, void *recvbuf,
                                       MPI_Aint recvcount, MPI_Datatype recvtype,
                                       MPIR_Comm * comm, MPIR_TSP_sched_t * sched, int k)
{
    int mpi_errno = MPI_SUCCESS;
    int i, j;
    int nphases = 0;
    int n_invtcs;
    int tag;
    int count, left_count;
    int src, dst, p_of_k = 0;   /* Largest power of k that is (strictly) smaller than 'size' */

    int rank = MPIR_Comm_rank(comm);
    int size = MPIR_Comm_size(comm);
    int is_inplace = (sendbuf == MPI_IN_PLACE);
    int max = size - 1;

    MPI_Aint sendtype_extent, sendtype_lb;
    MPI_Aint recvtype_extent, recvtype_lb;
    MPI_Aint sendtype_true_extent, recvtype_true_extent;

    int delta = 1;
    int i_recv = 0;
    int *recv_id = NULL;
    void *tmp_recvbuf = NULL;
    MPIR_CHKLMEM_DECL(1);

    /* For correctness, transport based collectives need to get the
     * tag from the same pool as schedule based collectives */
    mpi_errno = MPIR_Sched_next_tag(comm, &tag);
    MPIR_ERR_CHECK(mpi_errno);

    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIR_TSP_IALLGATHER_SCHED_INTRA_BRUCKS);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIR_TSP_IALLGATHER_SCHED_INTRA_BRUCKS);

    if (is_inplace) {
        sendcount = recvcount;
        sendtype = recvtype;
    }

    /* get datatype info of sendtype and recvtype */
    MPIR_Datatype_get_extent_macro(sendtype, sendtype_extent);
    MPIR_Type_get_true_extent_impl(sendtype, &sendtype_lb, &sendtype_true_extent);
    sendtype_extent = MPL_MAX(sendtype_extent, sendtype_true_extent);

    MPIR_Datatype_get_extent_macro(recvtype, recvtype_extent);
    MPIR_Type_get_true_extent_impl(recvtype, &recvtype_lb, &recvtype_true_extent);
    recvtype_extent = MPL_MAX(recvtype_extent, recvtype_true_extent);

    while (max) {
        nphases++;
        max /= k;
    }

    /* Check if size is power of k */
    if (MPL_ipow(k, nphases) == size)
        p_of_k = 1;

    MPIR_CHKLMEM_MALLOC(recv_id, int *, sizeof(int) * nphases * (k - 1),
                        mpi_errno, "recv_id buffer", MPL_MEM_COLL);

    if (rank == 0)
        tmp_recvbuf = recvbuf;
    else
        tmp_recvbuf = MPIR_TSP_sched_malloc(size * recvcount * recvtype_extent, sched);

    /* Step1: copy own data from sendbuf to top of recvbuf. */
    if (is_inplace && rank != 0)
        MPIR_TSP_sched_localcopy((char *) recvbuf + rank * recvcount * recvtype_extent,
                                 recvcount, recvtype, tmp_recvbuf, recvcount, recvtype, sched, 0,
                                 NULL);
    else if (!is_inplace)
        MPIR_TSP_sched_localcopy(sendbuf, sendcount, sendtype, tmp_recvbuf,
                                 recvcount, recvtype, sched, 0, NULL);

    /* All following sends/recvs and copies depend on this dtcopy */
    MPIR_TSP_sched_fence(sched);

    n_invtcs = 0;
    for (i = 0; i < nphases; i++) {
        for (j = 1; j < k; j++) {
            if (MPL_ipow(k, i) * j >= size)     /* If the first location exceeds comm size, nothing is to be sent. */
                break;

            dst = (int) (size + (rank - delta * j)) % size;
            src = (int) (rank + delta * j) % size;

            /* Amount of data sent in each cycle = k^i, where i = phase_number.
             * if (size != MPL_ipow(k, power_of_k) send less data in the last phase.
             * This might differ for the different values of j in the last phase. */
            if ((i == (nphases - 1)) && (!p_of_k)) {
                count = recvcount * delta;
                left_count = recvcount * (size - delta * j);
                if (j == k - 1)
                    count = left_count;
                else
                    count = MPL_MIN(count, left_count);
            } else {
                count = recvcount * delta;
            }

            /* Receive at the exact location. */
            recv_id[i_recv++] =
                MPIR_TSP_sched_irecv((char *) tmp_recvbuf + j * recvcount * delta * recvtype_extent,
                                     count, recvtype, src, tag, comm, sched, 0, NULL);

            /* Send from the start of recv till `count` amount of data. */
            if (i == 0)
                MPIR_TSP_sched_isend(tmp_recvbuf, count, recvtype, dst, tag, comm, sched, 0, NULL);
            else
                MPIR_TSP_sched_isend(tmp_recvbuf, count, recvtype, dst, tag,
                                     comm, sched, n_invtcs, recv_id);

        }
        n_invtcs += (k - 1);
        delta *= k;
    }
    MPIR_TSP_sched_fence(sched);

    if (rank != 0) {    /* No shift required for rank 0 */
        MPIR_TSP_sched_localcopy((char *) tmp_recvbuf + (size - rank) * recvcount * recvtype_extent,
                                 rank * recvcount, recvtype, (char *) recvbuf, rank * recvcount,
                                 recvtype, sched, 0, NULL);

        MPIR_TSP_sched_localcopy((char *) tmp_recvbuf, (size - rank) * recvcount, recvtype,
                                 (char *) recvbuf + rank * recvcount * recvtype_extent,
                                 (size - rank) * recvcount, recvtype, sched, 0, NULL);
    }

  fn_exit:
    MPIR_CHKLMEM_FREEALL();
    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIR_TSP_IALLGATHER_SCHED_INTRA_BRUCKS);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}


/* Non-blocking brucks based Allgather */
int MPIR_TSP_Iallgather_intra_brucks(const void *sendbuf, MPI_Aint sendcount, MPI_Datatype sendtype,
                                     void *recvbuf, MPI_Aint recvcount, MPI_Datatype recvtype,
                                     MPIR_Comm * comm_ptr, MPIR_Request ** req, int k)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_TSP_sched_t *sched;

    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIR_TSP_IALLGATHER_INTRA_BRUCKS);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIR_TSP_IALLGATHER_INTRA_BRUCKS);

    *req = NULL;

    /* generate the schedule */
    sched = MPL_malloc(sizeof(MPIR_TSP_sched_t), MPL_MEM_COLL);
    MPIR_Assert(sched != NULL);
    MPIR_TSP_sched_create(sched, false);

    mpi_errno = MPIR_TSP_Iallgather_sched_intra_brucks(sendbuf, sendcount, sendtype, recvbuf,
                                                       recvcount, recvtype, comm_ptr, sched, k);
    MPIR_ERR_CHECK(mpi_errno);

    /* start and register the schedule */
    mpi_errno = MPIR_TSP_sched_start(sched, comm_ptr, req);
    MPIR_ERR_CHECK(mpi_errno);

  fn_exit:
    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIR_TSP_IALLGATHER_INTRA_BRUCKS);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}
