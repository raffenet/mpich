/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 *  Copyright (C) by Argonne National Laboratory.
 *     See COPYRIGHT in top-level directory.
 *
 */

#include "mpiimpl.h"
#include "mpidimpl.h"
#include "ipc_types.h"
#include "../xpmem/xpmem_post.h"
#include "../gpu/gpu_post.h"
#include "ipc_p2p.h"

int MPIDI_IPC_allreduce(const void *sendbuf, void *recvbuf, MPI_Aint count,
                        MPI_Datatype datatype, MPI_Op op,
                        MPIR_Comm * comm, MPIR_Errflag_t * errflag)
{
    int mpi_errno = MPI_SUCCESS, mpi_errno_ret = MPI_SUCCESS;
    int i, src, dst;
    int nranks, is_inplace, rank;

    is_inplace = (sendbuf == MPI_IN_PLACE);
    nranks = MPIR_Comm_size(comm);
    rank = MPIR_Comm_rank(comm);

    /* MPI_IN_PLACE is not supported */
    if (is_inplace) {
        MPID_Abort(NULL, MPI_SUCCESS, 1, NULL);
    }

    mpi_errno = MPIR_Localcopy(sendbuf, count, datatype, recvbuf, count, datatype);
    if (mpi_errno) {
        MPIR_ERR_POP(mpi_errno);
    }

    MPIDI_IPCI_ipc_attr_t ipc_attr;
    MPIR_GPU_query_pointer_attr(sendbuf, &ipc_attr.gpu_attr);
    mpi_errno = MPIDI_GPU_get_ipc_attr(sendbuf, rank, comm, &ipc_attr);
    MPIR_ERR_CHECK(mpi_errno);
    mpi_errno = MPIDI_GPU_ipc_handle_cache_insert(rank, comm, ipc_attr.ipc_handle.gpu);
    MPIR_ERR_CHECK(mpi_errno);
    MPIDI_IPCI_ipc_handle_t my_handle = ipc_attr.ipc_handle;
    MPIDI_IPCI_ipc_handle_t *ipc_handles = MPL_malloc(nranks * sizeof(MPIDI_IPCI_ipc_handle_t), MPL_MEM_COLL);

    /* allgather IPC handles */
    mpi_errno =  MPIR_Allgather(&my_handle, sizeof(my_handle), MPI_BYTE, ipc_handles,
                                sizeof(ipc_handles[0]), MPI_BYTE, comm, errflag);
    if (mpi_errno) {
        /* for communication errors, just record the error but continue */
        *errflag = MPIR_ERR_OTHER;
        MPIR_ERR_SET(mpi_errno, *errflag, "**fail");
        MPIR_ERR_ADD(mpi_errno_ret, mpi_errno);
    }

    MPL_pointer_attr_t attr;
    MPIR_GPU_query_pointer_attr(recvbuf, &attr);
    int dev_id = MPL_gpu_get_dev_id_from_attr(&attr);
    //printf("srcbuf = %p, recvbuf = %p\n", srcbuf, recvbuf);

    for (i = 0; i < nranks; i++) {
        if (i == rank) {
            continue;
        }

        int map_dev = MPIDI_GPU_ipc_get_map_dev(ipc_handles[i].gpu.global_dev_id, dev_id, datatype);
        void *srcbuf;
        MPIDI_GPU_ipc_handle_map(ipc_handles[i].gpu, map_dev, &srcbuf);
        MPIR_Typerep_op(srcbuf, count, datatype, recvbuf, count, datatype, op,
                        true, map_dev);
    }

    MPL_free(ipc_handles);

  fn_exit:
    if (mpi_errno_ret)
        mpi_errno = mpi_errno_ret;
    else if (*errflag != MPIR_ERR_NONE)
        MPIR_ERR_SET(mpi_errno, *errflag, "**coll_fail");
    return mpi_errno;

  fn_fail:
    goto fn_exit;
}

int MPIDI_IPC_allreduce_stream(const void *sendbuf, void *recvbuf, MPI_Aint count,
                               MPI_Datatype datatype, MPI_Op op,
                               MPIR_Comm * comm, MPIR_Errflag_t * errflag)
{
    int mpi_errno = MPI_SUCCESS, mpi_errno_ret = MPI_SUCCESS;
    int i, src, dst;
    int nranks, is_inplace, rank;

    is_inplace = (sendbuf == MPI_IN_PLACE);
    nranks = MPIR_Comm_size(comm);
    rank = MPIR_Comm_rank(comm);

    /* MPI_IN_PLACE is not supported */
    if (is_inplace) {
        MPID_Abort(NULL, MPI_SUCCESS, 1, NULL);
    }

    MPL_gpu_stream_t gpu_stream;
    mpi_errno = get_local_gpu_stream(comm, &gpu_stream);
    MPIR_ERR_CHECK(mpi_errno);

    mpi_errno = MPIR_Localcopy_stream(sendbuf, count, datatype, recvbuf, count, datatype, &gpu_stream);
    if (mpi_errno) {
        MPIR_ERR_POP(mpi_errno);
    }

    MPIDI_IPCI_ipc_attr_t ipc_attr;
    MPIR_GPU_query_pointer_attr(sendbuf, &ipc_attr.gpu_attr);
    mpi_errno = MPIDI_GPU_get_ipc_attr(sendbuf, rank, comm, &ipc_attr);
    MPIR_ERR_CHECK(mpi_errno);
    mpi_errno = MPIDI_GPU_ipc_handle_cache_insert(rank, comm, ipc_attr.ipc_handle.gpu);
    MPIR_ERR_CHECK(mpi_errno);
    MPIDI_IPCI_ipc_handle_t my_handle = ipc_attr.ipc_handle;
    MPIDI_IPCI_ipc_handle_t *ipc_handles = MPL_malloc(nranks * sizeof(MPIDI_IPCI_ipc_handle_t), MPL_MEM_COLL);

    /* allgather IPC handles */
    mpi_errno =  MPIR_Allgather(&my_handle, sizeof(my_handle), MPI_BYTE, ipc_handles,
                                sizeof(ipc_handles[0]), MPI_BYTE, comm, errflag);
    if (mpi_errno) {
        /* for communication errors, just record the error but continue */
        *errflag = MPIR_ERR_OTHER;
        MPIR_ERR_SET(mpi_errno, *errflag, "**fail");
        MPIR_ERR_ADD(mpi_errno_ret, mpi_errno);
    }

    MPL_pointer_attr_t attr;
    MPIR_GPU_query_pointer_attr(recvbuf, &attr);
    int dev_id = MPL_gpu_get_dev_id_from_attr(&attr);

    yaksa_type_t yaksa_type = MPII_Typerep_get_yaksa_type(datatype);
    yaksa_op_t yaksa_op = MPII_Typerep_get_yaksa_op(op);

    MPI_Aint data_sz, actual_pack_bytes;
    MPIR_Pack_size(count, datatype, &data_sz);

    /* yaksa_info_t info = NULL; */
    /* yaksa_info_create(&info); */
    /* yaksa_info_keyval_append(info, "yaksa_mapped_device", &map_dev, sizeof(int)); */

    for (i = 0; i < nranks; i++) {
        if (i == rank) {
            continue;
        }

        int map_dev = MPIDI_GPU_ipc_get_map_dev(ipc_handles[i].gpu.global_dev_id, dev_id, datatype);
        void *srcbuf;
        MPIDI_GPU_ipc_handle_map(ipc_handles[i].gpu, map_dev, &srcbuf);
        yaksa_unpack_stream(srcbuf, data_sz, recvbuf, count, yaksa_type, 0,  &actual_pack_bytes, NULL, yaksa_op, &gpu_stream);
    }

    MPL_free(ipc_handles);

  fn_exit:
    if (mpi_errno_ret)
        mpi_errno = mpi_errno_ret;
    else if (*errflag != MPIR_ERR_NONE)
        MPIR_ERR_SET(mpi_errno, *errflag, "**coll_fail");
    return mpi_errno;

  fn_fail:
    goto fn_exit;
}
