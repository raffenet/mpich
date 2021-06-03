/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#ifndef MPIDIG_PERSIST_H_INCLUDED
#define MPIDIG_PERSIST_H_INCLUDED

int MPIDIG_mpi_send_init(const void *buffer, int count, MPI_Datatype datatype, int dest, int tag,
                         MPIR_Comm * comm, int attr, MPIR_Request ** request);
int MPIDIG_mpi_ssend_init(const void *buffer, int count, MPI_Datatype datatype, int dest, int tag,
                          MPIR_Comm * comm, int attr, MPIR_Request ** request);
int MPIDIG_mpi_bsend_init(const void *buffer, int count, MPI_Datatype datatype, int dest, int tag,
                          MPIR_Comm * comm, int attr, MPIR_Request ** request);
int MPIDIG_mpi_rsend_init(const void *buffer, int count, MPI_Datatype datatype, int dest, int tag,
                          MPIR_Comm * comm, int attr, MPIR_Request ** request);

MPL_STATIC_INLINE_PREFIX int MPIDIG_prequest_start(MPIR_Request * preq)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_FUNC_ENTER;

    /* nothing to do if the source/dest is MPI_PROC_NULL */
    if (MPIDI_PREQUEST(preq, rank) == MPI_PROC_NULL)
        goto fn_exit;

    MPIR_Comm *comm = preq->comm;
    switch (MPIDI_PREQUEST(preq, p_type)) {

        case MPIDI_PTYPE_RECV:
            mpi_errno = MPID_Irecv(MPIDI_PREQUEST(preq, buffer), MPIDI_PREQUEST(preq, count),
                                   MPIDI_PREQUEST(preq, datatype), MPIDI_PREQUEST(preq, rank),
                                   MPIDI_PREQUEST(preq, tag), comm,
                                   0, &preq->u.persist.real_request);
            break;

        case MPIDI_PTYPE_SEND:
            mpi_errno = MPID_Isend(MPIDI_PREQUEST(preq, buffer), MPIDI_PREQUEST(preq, count),
                                   MPIDI_PREQUEST(preq, datatype), MPIDI_PREQUEST(preq, rank),
                                   MPIDI_PREQUEST(preq, tag), comm,
                                   0, &preq->u.persist.real_request);
            break;

        case MPIDI_PTYPE_SSEND:
            mpi_errno = MPID_Issend(MPIDI_PREQUEST(preq, buffer), MPIDI_PREQUEST(preq, count),
                                    MPIDI_PREQUEST(preq, datatype), MPIDI_PREQUEST(preq, rank),
                                    MPIDI_PREQUEST(preq, tag), comm,
                                    0, &preq->u.persist.real_request);
            break;

        case MPIDI_PTYPE_BSEND:
            mpi_errno =
                MPIR_Bsend_isend(MPIDI_PREQUEST(preq, buffer), MPIDI_PREQUEST(preq, count),
                                 MPIDI_PREQUEST(preq, datatype), MPIDI_PREQUEST(preq, rank),
                                 MPIDI_PREQUEST(preq, tag), comm, &preq->u.persist.real_request);
            if (mpi_errno == MPI_SUCCESS) {
                preq->status.MPI_ERROR = MPI_SUCCESS;
                preq->cc_ptr = &preq->cc;
                /* bsend is local-complete */
                MPIR_cc_set(preq->cc_ptr, 0);
                goto fn_exit;
            }
            break;

        default:
            mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, __FUNCTION__,
                                             __LINE__, MPI_ERR_INTERN, "**ch4|badreqtype",
                                             "**ch4|badreqtype %d", MPIDI_PREQUEST(preq, p_type));
    }

    if (mpi_errno == MPI_SUCCESS) {
        preq->status.MPI_ERROR = MPI_SUCCESS;
        preq->cc_ptr = &preq->u.persist.real_request->cc;
    } else {
        preq->u.persist.real_request = NULL;
        preq->status.MPI_ERROR = mpi_errno;
        preq->cc_ptr = &preq->cc;
        MPID_Request_set_completed(preq);
    }

  fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;
}

#endif /* MPIDIG_PERSIST_H_INCLUDED */
