/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpiimpl.h"
#include "gpu_types.h"

int MPIDI_GPU_allgather(const void *sendbuf, MPI_Aint sendcount,
                        MPI_Datatype sendtype, void *recvbuf,
                        MPI_Aint recvcount, MPI_Datatype recvtype,
                        MPIR_Comm * comm, MPIR_Errflag_t * errflag)
{
    int size, rank;
    int mpi_errno = MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    MPI_Aint pack_size;
    MPI_Aint recvtype_extent;
    int j, i;
    int left, right, jnext;

    size = MPIR_Comm_size(comm);
    rank = MPIR_Comm_rank(comm);

    MPIR_Pack_size(recvcount, recvtype, &pack_size);
    MPIR_Assert(pack_size <= GPU_COLL_BUFFER_SIZE);
    MPIR_Datatype_get_extent_macro(recvtype, recvtype_extent);

    double start, stop;
    double sendrecv = 0;
    double packing = 0;
    double unpacking = 0;

    /* First, load the "local" version in the recvbuf. */
    if (sendbuf != MPI_IN_PLACE) {
        mpi_errno = MPIR_Localcopy(sendbuf, sendcount, sendtype,
                                   ((char *) recvbuf +
                                    rank * recvcount * recvtype_extent), recvcount, recvtype);
        MPIR_ERR_CHECK(mpi_errno);
    }

    /*
     * Now, send left to right.  This fills in the receive area in
     * reverse order.
     */
    left = (size + rank - 1) % size;
    right = (rank + 1) % size;

    j = rank;
    jnext = left;
    for (i = 1; i < size; i++) {
        MPIC_Sendrecv(((char *) recvbuf +
                       j * recvcount * recvtype_extent),
                      recvcount, recvtype, right,
                      MPIR_ALLGATHER_TAG,
                      ((char *) recvbuf +
                       jnext * recvcount * recvtype_extent),
                      recvcount, recvtype, left,
                      MPIR_ALLGATHER_TAG, comm, MPI_STATUS_IGNORE, errflag);
        /* /\* send CTS to left neighbor, recv CTS from right neighbor *\/ */
        /* start = MPI_Wtime(); */
        /* MPIC_Sendrecv(NULL, 0, MPI_DATATYPE_NULL, left, MPIR_ALLGATHER_TAG, NULL, 0, MPI_DATATYPE_NULL, left, MPIR_ALLGATHER_TAG, comm, MPI_STATUS_IGNORE, errflag); */
        /* stop = MPI_Wtime(); */
        /* sendrecv += stop - start; */

        /* /\* send data to right neighbor *\/ */
        /* MPI_Aint actual_pack_bytes; */
        /* start = MPI_Wtime(); */
        /* MPIR_Typerep_pack((char *) recvbuf + j * recvcount * recvtype_extent, recvcount, */
        /*                    recvtype, 0, MPIDI_GPUI_global.ipc_buffers[1], pack_size, &actual_pack_bytes, MPIR_TYPEREP_FLAG_NONE); */
        /* stop = MPI_Wtime(); */
        /* packing += stop - start; */

        /* /\* notify right neighbor data is available *\/ */
        /* start = MPI_Wtime(); */
        /* MPIC_Sendrecv(NULL, 0, MPI_DATATYPE_NULL, right, MPIR_ALLGATHER_TAG, NULL, 0, MPI_DATATYPE_NULL, left, MPIR_ALLGATHER_TAG, comm, MPI_STATUS_IGNORE, errflag); */
        /* stop = MPI_Wtime(); */
        /* sendrecv += stop - start; */

        /* /\* unpack from tmpbuf *\/ */
        /* MPI_Aint actual_unpack_bytes; */
        /* start = MPI_Wtime(); */
        /* MPIR_Typerep_unpack(MPIDI_GPUI_global.ipc_buffers[0], pack_size, (char *) recvbuf + jnext * recvcount * recvtype_extent, recvcount, recvtype, 0, &actual_unpack_bytes, MPIR_TYPEREP_FLAG_NONE); */
        /* stop = MPI_Wtime(); */
        /* unpacking += stop - start; */

        /* update for next interation */
        j = jnext;
        jnext = (size + jnext - 1) % size;
    }

    /* printf("sendrecv = %f\n", sendrecv); */
    /* printf("packing = %f\n", packing); */
    /* printf("unpacking = %f\n", unpacking); */

fn_exit:
    return mpi_errno;

fn_fail:
    goto fn_exit;
}
