/*
 * Copyright (C) by Argonne National Laboratory
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SHM_P2P_H_INCLUDED
#define SHM_P2P_H_INCLUDED

#include "../posix/posix_send.h"
#include "../ipc/src/ipc_send.h"
#include "../posix/posix_recv.h"
#include "../posix/posix_probe.h"

MPL_STATIC_INLINE_PREFIX bool MPIDI_SHM_isend_use_posix(int rank, MPI_Aint count,
                                                        MPI_Datatype datatype, const void *buf,
                                                        MPIR_Comm * comm)
{
    if (rank == comm->rank || count == 0 || buf == MPI_BOTTOM) {
        return true;
    }
#ifndef MPIDI_CH4_SHM_ENABLE_GPU
    if (HANDLE_IS_BUILTIN(datatype)) {
        MPI_Aint elem_sz = MPIR_Datatype_get_basic_size(datatype);
        MPI_Aint max_eager_data = MPIDI_POSIX_am_eager_limit() - (MPI_Aint) sizeof(MPIDIG_hdr_t);
        if (elem_sz == 0 || count <= max_eager_data / elem_sz) {
            return true;
        }
    }
#endif
    return false;
}

MPL_STATIC_INLINE_PREFIX int MPIDI_SHM_mpi_isend(const void *buf, MPI_Aint count,
                                                 MPI_Datatype datatype, int rank, int tag,
                                                 MPIR_Comm * comm, int attr,
                                                 MPIDI_av_entry_t * addr, MPIR_Request ** request)
{
    int mpi_errno = MPI_SUCCESS;

    MPIR_FUNC_ENTER;

    /* for self, zero-sized messages, MPI_BOTTOM, or small builtin host messages,
     * we can skip the IPC check */
    /* TODO: extend the ch4/self path to support communicators of size > 1. That way we can
     *   always use MPIR_Localcopy to move the data and support GPU buffers efficiently
     *   with yaksa */
    if (MPIDI_SHM_isend_use_posix(rank, count, datatype, buf, comm)) {
        mpi_errno =
            MPIDI_POSIX_mpi_isend(buf, count, datatype, rank, tag, comm, attr, addr, request);
    } else {
        mpi_errno = MPIDI_IPC_mpi_isend(buf, count, datatype, rank, tag, comm, attr, addr, request);
    }
    MPIR_ERR_CHECK(mpi_errno);

  fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

MPL_STATIC_INLINE_PREFIX int MPIDI_SHM_mpi_cancel_send(MPIR_Request * sreq)
{
    int ret;

    MPIR_FUNC_ENTER;

    ret = MPIDI_POSIX_mpi_cancel_send(sreq);

    MPIR_FUNC_EXIT;
    return ret;
}

MPL_STATIC_INLINE_PREFIX int MPIDI_SHM_mpi_irecv(void *buf, MPI_Aint count, MPI_Datatype datatype,
                                                 int rank, int tag, MPIR_Comm * comm,
                                                 int context_offset, MPIR_Request ** request)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_FUNC_ENTER;

    mpi_errno =
        MPIDI_POSIX_mpi_irecv(buf, count, datatype, rank, tag, comm, context_offset, request);
    MPIR_ERR_CHECK(mpi_errno);

  fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

MPL_STATIC_INLINE_PREFIX int MPIDI_SHM_mpi_imrecv(void *buf, MPI_Aint count, MPI_Datatype datatype,
                                                  MPIR_Request * message)
{
    int mpi_errno = MPI_SUCCESS;

    MPIR_FUNC_ENTER;

    mpi_errno = MPIDI_POSIX_mpi_imrecv(buf, count, datatype, message);
    MPIR_ERR_CHECK(mpi_errno);

  fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

MPL_STATIC_INLINE_PREFIX int MPIDI_SHM_mpi_cancel_recv(MPIR_Request * rreq, bool is_anysrc_partner)
{
    int ret;

    MPIR_FUNC_ENTER;

    ret = MPIDI_POSIX_mpi_cancel_recv(rreq, is_anysrc_partner);

    MPIR_FUNC_EXIT;
    return ret;
}

MPL_STATIC_INLINE_PREFIX int MPIDI_SHM_mpi_improbe(int source, int tag, MPIR_Comm * comm,
                                                   int context_offset, int *flag,
                                                   MPIR_Request ** message, MPI_Status * status)
{
    int ret;

    MPIR_FUNC_ENTER;

    ret = MPIDI_POSIX_mpi_improbe(source, tag, comm, context_offset, flag, message, status);

    MPIR_FUNC_EXIT;
    return ret;
}

MPL_STATIC_INLINE_PREFIX int MPIDI_SHM_mpi_iprobe(int source, int tag, MPIR_Comm * comm,
                                                  int context_offset, int *flag,
                                                  MPI_Status * status)
{
    int ret;

    MPIR_FUNC_ENTER;

    ret = MPIDI_POSIX_mpi_iprobe(source, tag, comm, context_offset, flag, status);

    MPIR_FUNC_EXIT;
    return ret;
}

#endif /* SHM_P2P_H_INCLUDED */
