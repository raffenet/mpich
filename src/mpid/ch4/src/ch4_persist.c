/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpidimpl.h"

int MPID_Send_init(const void *buf,
                   MPI_Aint count,
                   MPI_Datatype datatype,
                   int rank, int tag, MPIR_Comm * comm, int context_offset, MPIR_Request ** request)
{
    int mpi_errno = MPI_SUCCESS;

    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPID_SEND_INIT);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPID_SEND_INIT);

#ifdef MPIDI_CH4_DIRECT_NETMOD
    mpi_errno =
        MPIDI_NM_mpi_send_init(buf, count, datatype, rank, tag, comm, context_offset, request);
#else
    MPIDI_av_entry_t *av = MPIDIU_comm_rank_to_av(comm, rank);
    if (MPIDI_av_is_local(av))
        mpi_errno =
            MPIDI_SHM_mpi_send_init(buf, count, datatype, rank, tag, comm, context_offset, request);
    else
        mpi_errno =
            MPIDI_NM_mpi_send_init(buf, count, datatype, rank, tag, comm, context_offset, request);
    if (mpi_errno == MPI_SUCCESS)
        MPIDI_REQUEST(*request, is_local) = MPIDI_av_is_local(av);
#endif
    MPIR_ERR_CHECK(mpi_errno);

  fn_exit:
    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPID_SEND_INIT);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

int MPID_Ssend_init(const void *buf,
                    MPI_Aint count,
                    MPI_Datatype datatype,
                    int rank,
                    int tag, MPIR_Comm * comm, int context_offset, MPIR_Request ** request)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPID_SSEND_INIT);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPID_SSEND_INIT);

#ifdef MPIDI_CH4_DIRECT_NETMOD
    mpi_errno =
        MPIDI_NM_mpi_ssend_init(buf, count, datatype, rank, tag, comm, context_offset, request);
#else
    MPIDI_av_entry_t *av = MPIDIU_comm_rank_to_av(comm, rank);
    if (MPIDI_av_is_local(av))
        mpi_errno =
            MPIDI_SHM_mpi_ssend_init(buf, count, datatype, rank, tag, comm, context_offset,
                                     request);
    else
        mpi_errno =
            MPIDI_NM_mpi_ssend_init(buf, count, datatype, rank, tag, comm, context_offset, request);
    if (mpi_errno == MPI_SUCCESS)
        MPIDI_REQUEST(*request, is_local) = MPIDI_av_is_local(av);
#endif
    MPIR_ERR_CHECK(mpi_errno);

  fn_exit:
    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPID_SSEND_INIT);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

int MPID_Bsend_init(const void *buf,
                    MPI_Aint count,
                    MPI_Datatype datatype,
                    int rank,
                    int tag, MPIR_Comm * comm, int context_offset, MPIR_Request ** request)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPID_BSEND_INIT);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPID_BSEND_INIT);

#ifdef MPIDI_CH4_DIRECT_NETMOD
    mpi_errno =
        MPIDI_NM_mpi_bsend_init(buf, count, datatype, rank, tag, comm, context_offset, request);
#else
    MPIDI_av_entry_t *av = MPIDIU_comm_rank_to_av(comm, rank);
    if (MPIDI_av_is_local(av))
        mpi_errno =
            MPIDI_SHM_mpi_bsend_init(buf, count, datatype, rank, tag, comm, context_offset,
                                     request);
    else
        mpi_errno =
            MPIDI_NM_mpi_bsend_init(buf, count, datatype, rank, tag, comm, context_offset, request);
    if (mpi_errno == MPI_SUCCESS)
        MPIDI_REQUEST(*request, is_local) = MPIDI_av_is_local(av);
#endif
    MPIR_ERR_CHECK(mpi_errno);

  fn_exit:
    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPID_BSEND_INIT);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

int MPID_Rsend_init(const void *buf,
                    MPI_Aint count,
                    MPI_Datatype datatype,
                    int rank,
                    int tag, MPIR_Comm * comm, int context_offset, MPIR_Request ** request)
{
    int mpi_errno = MPI_SUCCESS;

    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPID_RSEND_INIT);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPID_RSEND_INIT);

#ifdef MPIDI_CH4_DIRECT_NETMOD
    mpi_errno =
        MPIDI_NM_mpi_rsend_init(buf, count, datatype, rank, tag, comm, context_offset, request);
#else
    MPIDI_av_entry_t *av = MPIDIU_comm_rank_to_av(comm, rank);
    if (MPIDI_av_is_local(av))
        mpi_errno =
            MPIDI_SHM_mpi_rsend_init(buf, count, datatype, rank, tag, comm, context_offset,
                                     request);
    else
        mpi_errno =
            MPIDI_NM_mpi_rsend_init(buf, count, datatype, rank, tag, comm, context_offset, request);
    if (mpi_errno == MPI_SUCCESS)
        MPIDI_REQUEST(*request, is_local) = MPIDI_av_is_local(av);
#endif
    MPIR_ERR_CHECK(mpi_errno);

  fn_exit:
    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPID_RSEND_INIT);
    return mpi_errno;
  fn_fail:
    goto fn_exit;

}

int MPID_Recv_init(void *buf,
                   MPI_Aint count,
                   MPI_Datatype datatype,
                   int rank, int tag, MPIR_Comm * comm, int context_offset, MPIR_Request ** request)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_Request *rreq;
    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPID_RECV_INIT);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPID_RECV_INIT);

    MPID_THREAD_CS_ENTER(VCI, MPIDI_VCI(0).lock);
    MPIDI_CH4_REQUEST_CREATE(rreq, MPIR_REQUEST_KIND__PREQUEST_RECV, 0, 1);
    MPID_THREAD_CS_EXIT(VCI, MPIDI_VCI(0).lock);
    MPIR_ERR_CHKANDSTMT(rreq == NULL, mpi_errno, MPIX_ERR_NOREQ, goto fn_fail, "**nomemreq");

    *request = rreq;
    rreq->comm = comm;
    MPIR_Comm_add_ref(comm);

    MPIDI_PREQUEST(rreq, buffer) = (void *) buf;
    MPIDI_PREQUEST(rreq, count) = count;
    MPIDI_PREQUEST(rreq, datatype) = datatype;
    MPIDI_PREQUEST(rreq, rank) = rank;
    MPIDI_PREQUEST(rreq, tag) = tag;
    MPIDI_PREQUEST(rreq, context_id) = comm->context_id + context_offset;
    rreq->u.persist.real_request = NULL;
    MPIR_cc_set(rreq->cc_ptr, 0);

    MPIDI_PREQUEST(rreq, p_type) = MPIDI_PTYPE_RECV;
    MPIR_Datatype_add_ref_if_not_builtin(datatype);

  fn_exit:
    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPID_RECV_INIT);
    return mpi_errno;

  fn_fail:
    goto fn_exit;
}
