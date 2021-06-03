/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#ifndef CH4_STARTALL_H_INCLUDED
#define CH4_STARTALL_H_INCLUDED

#include "ch4_impl.h"
#include "mpidig_persist.h"

MPL_STATIC_INLINE_PREFIX int MPIDI_part_start(MPIR_Request * request);

MPL_STATIC_INLINE_PREFIX int MPID_Startall(int count, MPIR_Request * requests[])
{
    int mpi_errno = MPI_SUCCESS, i;

    MPIR_FUNC_ENTER;

    for (i = 0; i < count; i++) {
        MPIR_Request *const preq = requests[i];
        switch (preq->kind) {
            case MPIR_REQUEST_KIND__PREQUEST_SEND:
#ifdef MPIDI_CH4_DIRECT_NETMOD
                mpi_errno = MPIDI_NM_prequest_start(preq);
#else
                if (MPIDI_REQUEST(preq, is_local))
                    mpi_errno = MPIDI_SHM_prequest_start(preq);
                else
                    mpi_errno = MPIDI_NM_prequest_start(preq);
#endif
                break;
            case MPIR_REQUEST_KIND__PREQUEST_RECV:
                mpi_errno = MPIDIG_prequest_start(preq);
                break;

            case MPIR_REQUEST_KIND__PREQUEST_COLL:
                mpi_errno = MPIR_Persist_coll_start(preq);
                break;

            case MPIR_REQUEST_KIND__PART_SEND:
            case MPIR_REQUEST_KIND__PART_RECV:
                mpi_errno = MPIDI_part_start(preq);
                break;

            default:
                mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, __FUNCTION__,
                                                 __LINE__, MPI_ERR_INTERN, "**ch4|badstartreq",
                                                 "**ch4|badstartreq %d", preq->kind);
        }
        MPIR_ERR_CHECK(mpi_errno);
    }

  fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

#endif /* CH4_STARTALL_H_INCLUDED */
