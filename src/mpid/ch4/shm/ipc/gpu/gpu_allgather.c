/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpiimpl.h"
#include "gpu_types.h"

int MPIDI_GPU_allgather(const void *sendbuf, MPI_Aint sendcount,
                        MPI_Datatype sendtype, void *recvbuf,
                        MPI_Aint recvcount, MPI_Datatype recvtype,
                        MPIR_Comm * comm, MPIR_Errflag_t errflag)
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
    int send_contig, recv_contig;
    MPIR_Datatype_is_contig(sendtype, &send_contig);
    MPIR_Datatype_is_contig(recvtype, &recv_contig);
    bool do_kernel_copy = pack_size < 8192 && send_contig && recv_contig;
    MPIR_Datatype_get_extent_macro(recvtype, recvtype_extent);

    /* First, load the "local" version in the recvbuf. */
    if (sendbuf != MPI_IN_PLACE) {
        if (do_kernel_copy) {
            MPL_gpu_memcpy((char *) recvbuf + rank * recvcount * recvtype_extent, sendbuf, pack_size, MPIDI_GPUI_global.ipc_stream);
        } else {
            mpi_errno = MPIR_Localcopy_stream(sendbuf, sendcount, sendtype,
                                              ((char *) recvbuf +
                                               rank * recvcount * recvtype_extent), recvcount, recvtype, &MPIDI_GPUI_global.ipc_stream);
            MPIR_ERR_CHECK(mpi_errno);
        }
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
        if (do_kernel_copy) {
            MPL_gpu_send_fastbox((char *) recvbuf + j * recvcount * recvtype_extent, MPIDI_GPUI_global.ipc_buffers[1], pack_size, MPIDI_GPUI_global.ipc_stream);
            MPL_gpu_recv_fastbox(MPIDI_GPUI_global.ipc_buffers[0], (char *) recvbuf + jnext * recvcount * recvtype_extent, pack_size, MPIDI_GPUI_global.ipc_stream);
        } else {
            MPL_gpu_wait_cts(MPIDI_GPUI_global.ipc_buffers[1], MPIDI_GPUI_global.ipc_stream);
            MPI_Aint actual_pack_bytes;
            MPIR_Typerep_pack_stream((char *) recvbuf + j * recvcount * recvtype_extent, recvcount, recvtype, 0, (char *)MPIDI_GPUI_global.ipc_buffers[1] + 64, pack_size, &actual_pack_bytes, &MPIDI_GPUI_global.ipc_stream);
            MPL_gpu_set_data(MPIDI_GPUI_global.ipc_buffers[1], MPIDI_GPUI_global.ipc_stream);

            MPL_gpu_wait_data(MPIDI_GPUI_global.ipc_buffers[0], MPIDI_GPUI_global.ipc_stream);
            MPI_Aint actual_unpack_bytes;
            MPIR_Typerep_unpack_stream((char *)MPIDI_GPUI_global.ipc_buffers[0] + 64, pack_size, (char *) recvbuf + jnext * recvcount * recvtype_extent, recvcount, recvtype, 0, &actual_unpack_bytes, &MPIDI_GPUI_global.ipc_stream);
            MPL_gpu_set_cts(MPIDI_GPUI_global.ipc_buffers[0], MPIDI_GPUI_global.ipc_stream);
        }

        /* update for next interation */
        j = jnext;
        jnext = (size + jnext - 1) % size;
    }
    cudaStreamSynchronize(MPIDI_GPUI_global.ipc_stream);

fn_exit:
    return mpi_errno;

fn_fail:
    goto fn_exit;
}
