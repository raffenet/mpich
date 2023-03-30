/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpiimpl.h"
#include "gpu_types.h"

int MPIDI_GPU_bcast(void *buffer, MPI_Aint count, MPI_Datatype datatype, int root, MPIR_Comm * comm, MPIR_Errflag_t errflag)
{
    int size, rank;
    int mpi_errno = MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    MPI_Aint pack_size;
    int left, right;

    size = MPIR_Comm_size(comm);
    rank = MPIR_Comm_rank(comm);

    MPIR_Pack_size(count, datatype, &pack_size);
    int is_contig, recv_contig;
    MPIR_Datatype_is_contig(datatype, &is_contig);
    bool do_kernel_copy = pack_size < 8192 && is_contig;

    if (do_kernel_copy) {
        if (rank == root) {
            MPL_gpu_send_fastbox(buffer, MPIDI_GPUI_global.ipc_buffers[1], pack_size, MPIDI_GPUI_global.ipc_stream);
        } else if (rank == (size + root - 1) % size) {
            MPL_gpu_recv_fastbox(MPIDI_GPUI_global.ipc_buffers[0], buffer, pack_size, MPIDI_GPUI_global.ipc_stream);
        } else {
            printf("no!\n");
            MPL_gpu_recv_fastbox(MPIDI_GPUI_global.ipc_buffers[0], buffer, pack_size, MPIDI_GPUI_global.ipc_stream);
            MPL_gpu_send_fastbox(buffer, MPIDI_GPUI_global.ipc_buffers[1], pack_size, MPIDI_GPUI_global.ipc_stream);            
        }
    } else {
        if (rank == root) {
            MPL_gpu_wait_cts(MPIDI_GPUI_global.ipc_buffers[1], MPIDI_GPUI_global.ipc_stream);
            MPI_Aint actual_pack_bytes;
            MPIR_Typerep_pack_stream(buffer, count, datatype, 0, (char *)MPIDI_GPUI_global.ipc_buffers[1] + 64, pack_size, &actual_pack_bytes, &MPIDI_GPUI_global.ipc_stream);
            MPL_gpu_set_data(MPIDI_GPUI_global.ipc_buffers[1], MPIDI_GPUI_global.ipc_stream);
        } else if (rank == (size + root - 1) % size) {
            MPL_gpu_wait_data(MPIDI_GPUI_global.ipc_buffers[0], MPIDI_GPUI_global.ipc_stream);
            MPI_Aint actual_unpack_bytes;
            MPIR_Typerep_unpack_stream((char *)MPIDI_GPUI_global.ipc_buffers[0] + 64, pack_size, buffer, count, datatype, 0, &actual_unpack_bytes, &MPIDI_GPUI_global.ipc_stream);
            MPL_gpu_set_cts(MPIDI_GPUI_global.ipc_buffers[0], MPIDI_GPUI_global.ipc_stream);
        } else {
            MPL_gpu_wait_data(MPIDI_GPUI_global.ipc_buffers[0], MPIDI_GPUI_global.ipc_stream);
            MPI_Aint actual_unpack_bytes;
            MPIR_Typerep_unpack_stream((char *)MPIDI_GPUI_global.ipc_buffers[0] + 64, pack_size, buffer, count, datatype, 0, &actual_unpack_bytes, &MPIDI_GPUI_global.ipc_stream);
            MPL_gpu_set_cts(MPIDI_GPUI_global.ipc_buffers[0], MPIDI_GPUI_global.ipc_stream);

            MPL_gpu_wait_cts(MPIDI_GPUI_global.ipc_buffers[1], MPIDI_GPUI_global.ipc_stream);
            MPI_Aint actual_pack_bytes;
            MPIR_Typerep_pack_stream(buffer, count, datatype, 0, (char *)MPIDI_GPUI_global.ipc_buffers[1] + 64, pack_size, &actual_pack_bytes, &MPIDI_GPUI_global.ipc_stream);
            MPL_gpu_set_data(MPIDI_GPUI_global.ipc_buffers[1], MPIDI_GPUI_global.ipc_stream);            
        }
    }

    cudaStreamSynchronize(MPIDI_GPUI_global.ipc_stream);

fn_exit:
    return mpi_errno;

fn_fail:
    goto fn_exit;
}
