/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpidimpl.h"
#include "mpidrma.h"

int MPIDI_CH3U_Handle_recv_req(MPIDI_VC_t * vc, MPIR_Request * rreq, int *complete)
{
    static int in_routine ATTRIBUTE((unused)) = FALSE;
    int mpi_errno = MPI_SUCCESS;
    int (*reqFn) (MPIDI_VC_t *, MPIR_Request *, int *);

    MPIR_FUNC_ENTER;

    MPIR_Assert(in_routine == FALSE);
    in_routine = TRUE;

    reqFn = rreq->dev.OnDataAvail;
    if (!reqFn) {
        MPIR_Assert(MPIDI_Request_get_type(rreq) == MPIDI_REQUEST_TYPE_RECV);
        mpi_errno = MPID_Request_complete(rreq);
        MPIR_ERR_CHECK(mpi_errno);
        *complete = TRUE;
    }
    else {
        mpi_errno = reqFn(vc, rreq, complete);
    }

    in_routine = FALSE;

  fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

/* ----------------------------------------------------------------------- */
/* Here are the functions that implement the actions that are taken when
 * data is available for a receive request (or other completion operations)
 * These include "receive" requests that are part of the RMA implementation.
 *
 * The convention for the names of routines that are called when data is
 * available is
 *    MPIDI_CH3_ReqHandler_<type>(MPIDI_VC_t *, MPIR_Request *, int *)
 * as in
 *    MPIDI_CH3_ReqHandler_...
 *
 * ToDo:
 *    We need a way for each of these functions to describe what they are,
 *    so that given a pointer to one of these functions, we can retrieve
 *    a description of the routine.  We may want to use a static string
 *    and require the user to maintain thread-safety, at least while
 *    accessing the string.
 */
/* ----------------------------------------------------------------------- */
int MPIDI_CH3_ReqHandler_RecvComplete(MPIDI_VC_t * vc ATTRIBUTE((unused)),
                                      MPIR_Request * rreq, int *complete)
{
    int mpi_errno = MPI_SUCCESS;

    /* mark data transfer as complete and decrement CC */
    mpi_errno = MPID_Request_complete(rreq);
    MPIR_ERR_CHECK(mpi_errno);

    *complete = TRUE;

  fn_exit:
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

int MPIDI_CH3_ReqHandler_PutRecvComplete(MPIDI_VC_t * vc, MPIR_Request * rreq, int *complete)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_Win *win_ptr;
    MPI_Win source_win_handle = rreq->dev.source_win_handle;
    int pkt_flags = rreq->dev.pkt_flags;

    MPIR_FUNC_ENTER;

    /* NOTE: It is possible that this request is already completed before
     * entering this handler. This happens when this req handler is called
     * within the same req handler on the same request.
     * Consider this case: req is queued up in SHM queue with ref count of 2:
     * one is for completing the request and another is for dequeueing from
     * the queue. The first called req handler on this request completed
     * this request and decrement ref counter to 1. Request is still in the
     * queue. Within this handler, we call the req handler on the same request
     * for the second time (for example when making progress on SHM queue),
     * and the second called handler also tries to complete this request,
     * which leads to wrong execution.
     * Here we check if req is already completed to prevent processing the
     * same request twice. */
    if (MPIR_Request_is_complete(rreq)) {
        *complete = FALSE;
        goto fn_exit;
    }

    MPIR_Win_get_ptr(rreq->dev.target_win_handle, win_ptr);

    /* mark data transfer as complete and decrement CC */
    mpi_errno = MPID_Request_complete(rreq);
    MPIR_ERR_CHECK(mpi_errno);

    /* NOTE: finish_op_on_target() must be called after we complete this request,
     * because inside finish_op_on_target() we may call this request handler
     * on the same request again (in release_lock()). Marking this request as
     * completed will prevent us from processing the same request twice. */
    mpi_errno = finish_op_on_target(win_ptr, vc, FALSE /* has no response data */ ,
                                    pkt_flags, source_win_handle);
    MPIR_ERR_CHECK(mpi_errno);

    *complete = TRUE;

  fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;

    /* --BEGIN ERROR HANDLING-- */
  fn_fail:
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}


int MPIDI_CH3_ReqHandler_AccumRecvComplete(MPIDI_VC_t * vc, MPIR_Request * rreq, int *complete)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_Win *win_ptr;
    MPI_Win source_win_handle = rreq->dev.source_win_handle;
    int pkt_flags = rreq->dev.pkt_flags;
    MPI_Datatype basic_type;
    MPI_Aint predef_count, predef_dtp_size;
    MPI_Aint stream_offset;

    MPIR_FUNC_ENTER;

    /* NOTE: It is possible that this request is already completed before
     * entering this handler. This happens when this req handler is called
     * within the same req handler on the same request.
     * Consider this case: req is queued up in SHM queue with ref count of 2:
     * one is for completing the request and another is for dequeueing from
     * the queue. The first called req handler on this request completed
     * this request and decrement ref counter to 1. Request is still in the
     * queue. Within this handler, we call the req handler on the same request
     * for the second time (for example when making progress on SHM queue),
     * and the second called handler also tries to complete this request,
     * which leads to wrong execution.
     * Here we check if req is already completed to prevent processing the
     * same request twice. */
    if (MPIR_Request_is_complete(rreq)) {
        *complete = FALSE;
        goto fn_exit;
    }

    MPIR_Win_get_ptr(rreq->dev.target_win_handle, win_ptr);

    MPIR_Assert(MPIDI_Request_get_type(rreq) == MPIDI_REQUEST_TYPE_ACCUM_RECV);

    if (MPIR_DATATYPE_IS_PREDEFINED(rreq->dev.datatype))
        basic_type = rreq->dev.datatype;
    else {
        basic_type = rreq->dev.datatype_ptr->basic_type;
    }
    MPIR_Assert(basic_type != MPI_DATATYPE_NULL);

    MPIR_Datatype_get_size_macro(basic_type, predef_dtp_size);
    predef_count = rreq->dev.recv_data_sz / predef_dtp_size;
    MPIR_Assert(predef_count > 0);

    stream_offset = 0;
    MPIDI_CH3_ExtPkt_Accum_get_stream(pkt_flags,
                                      (!MPIR_DATATYPE_IS_PREDEFINED(rreq->dev.datatype)),
                                      rreq->dev.ext_hdr_ptr, &stream_offset);

    if (win_ptr->shm_allocated == TRUE) {
        MPIDI_CH3I_SHM_MUTEX_LOCK(win_ptr);
    }
    /* accumulate data from tmp_buf into user_buf */
    MPIR_Assert(predef_count == (int) predef_count);
    mpi_errno = do_accumulate_op(rreq->dev.user_buf, (int) predef_count, basic_type,
                                 rreq->dev.real_user_buf, rreq->dev.user_count, rreq->dev.datatype,
                                 stream_offset, rreq->dev.op, MPIDI_RMA_ACC_SRCBUF_DEFAULT);
    if (win_ptr->shm_allocated == TRUE) {
        MPIDI_CH3I_SHM_MUTEX_UNLOCK(win_ptr);
    }
    MPIR_ERR_CHECK(mpi_errno);

    /* free the temporary buffer */
    MPIDI_CH3U_SRBuf_free(rreq);

    /* mark data transfer as complete and decrement CC */
    mpi_errno = MPID_Request_complete(rreq);
    MPIR_ERR_CHECK(mpi_errno);

    /* NOTE: finish_op_on_target() must be called after we complete this request,
     * because inside finish_op_on_target() we may call this request handler
     * on the same request again (in release_lock()). Marking this request as
     * completed will prevent us from processing the same request twice. */
    mpi_errno = finish_op_on_target(win_ptr, vc, FALSE /* has no response data */ ,
                                    pkt_flags, source_win_handle);
    MPIR_ERR_CHECK(mpi_errno);

    *complete = TRUE;

  fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;

    /* --BEGIN ERROR HANDLING-- */
  fn_fail:
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}


int MPIDI_CH3_ReqHandler_GaccumRecvComplete(MPIDI_VC_t * vc, MPIR_Request * rreq, int *complete)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_Win *win_ptr;
    MPIDI_CH3_Pkt_t upkt;
    MPIDI_CH3_Pkt_get_accum_resp_t *get_accum_resp_pkt = &upkt.get_accum_resp;
    MPIR_Request *resp_req;
    struct iovec iov[MPL_IOV_LIMIT];
    int iovcnt;
    int is_contig;
    MPI_Datatype basic_type;
    MPI_Aint predef_count, predef_dtp_size;
    MPI_Aint dt_true_lb;
    MPI_Aint stream_offset;
    int is_empty_origin = FALSE;
    MPI_Aint extent, type_size;
    MPI_Aint stream_data_len, total_len;
    MPIR_CHKPMEM_DECL();

    MPIR_FUNC_ENTER;

    /* Judge if origin buffer is empty */
    if (rreq->dev.op == MPI_NO_OP) {
        is_empty_origin = TRUE;
    }

    MPIR_Win_get_ptr(rreq->dev.target_win_handle, win_ptr);

    if (MPIR_DATATYPE_IS_PREDEFINED(rreq->dev.datatype))
        basic_type = rreq->dev.datatype;
    else {
        basic_type = rreq->dev.datatype_ptr->basic_type;
    }
    MPIR_Assert(basic_type != MPI_DATATYPE_NULL);

    stream_offset = 0;
    MPIDI_CH3_ExtPkt_Gaccum_get_stream(rreq->dev.pkt_flags,
                                       (!MPIR_DATATYPE_IS_PREDEFINED(rreq->dev.datatype)),
                                       rreq->dev.ext_hdr_ptr, &stream_offset);

    /* Use target data to calculate current stream unit size */
    MPIR_Datatype_get_size_macro(rreq->dev.datatype, type_size);
    total_len = type_size * rreq->dev.user_count;
    MPIR_Datatype_get_size_macro(basic_type, predef_dtp_size);
    MPIR_Datatype_get_extent_macro(basic_type, extent);
    stream_data_len = MPL_MIN(total_len - (stream_offset / extent) * predef_dtp_size,
                               (MPIDI_CH3U_SRBuf_size / extent) * predef_dtp_size);

    predef_count = stream_data_len / predef_dtp_size;
    MPIR_Assert(predef_count > 0);

    MPIDI_Pkt_init(get_accum_resp_pkt, MPIDI_CH3_PKT_GET_ACCUM_RESP);
    get_accum_resp_pkt->request_handle = rreq->dev.resp_request_handle;
    get_accum_resp_pkt->target_rank = win_ptr->comm_ptr->rank;
    get_accum_resp_pkt->pkt_flags = MPIDI_CH3_PKT_FLAG_NONE;
    if (rreq->dev.pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_LOCK_SHARED ||
        rreq->dev.pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_LOCK_EXCLUSIVE)
        get_accum_resp_pkt->pkt_flags |= MPIDI_CH3_PKT_FLAG_RMA_LOCK_GRANTED;
    if ((rreq->dev.pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_FLUSH) ||
        (rreq->dev.pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_UNLOCK))
        get_accum_resp_pkt->pkt_flags |= MPIDI_CH3_PKT_FLAG_RMA_ACK;

    /* check if data is contiguous and get true lb */
    MPIR_Datatype_is_contig(rreq->dev.datatype, &is_contig);
    MPIR_Datatype_get_true_lb(rreq->dev.datatype, &dt_true_lb);

    resp_req = MPIR_Request_create(MPIR_REQUEST_KIND__UNDEFINED);
    MPIR_ERR_CHKANDJUMP(resp_req == NULL, mpi_errno, MPI_ERR_OTHER, "**nomemreq");
    MPIR_Object_set_ref(resp_req, 1);
    MPIDI_Request_set_type(resp_req, MPIDI_REQUEST_TYPE_GET_ACCUM_RESP);

    MPIR_CHKPMEM_MALLOC(resp_req->dev.user_buf, stream_data_len, MPL_MEM_BUFFER);

    /* NOTE: 'copy data + ACC' needs to be atomic */

    if (win_ptr->shm_allocated == TRUE) {
        MPIDI_CH3I_SHM_MUTEX_LOCK(win_ptr);
    }

    /* Copy data from target window to temporary buffer */

    if (is_contig) {
        MPIR_Memcpy(resp_req->dev.user_buf,
                    MPIR_get_contig_ptr(rreq->dev.real_user_buf, dt_true_lb + stream_offset),
                    stream_data_len);
    }
    else {
        MPI_Aint actual_pack_bytes;
        MPIR_Typerep_pack(rreq->dev.real_user_buf, rreq->dev.user_count, rreq->dev.datatype,
                       stream_offset, resp_req->dev.user_buf, stream_data_len, &actual_pack_bytes,
                       MPIR_TYPEREP_FLAG_NONE);
        MPIR_Assert(actual_pack_bytes == stream_data_len);
    }

    /* accumulate data from tmp_buf into user_buf */
    MPIR_Assert(predef_count == (int) predef_count);
    mpi_errno = do_accumulate_op(rreq->dev.user_buf, (int) predef_count, basic_type,
                                 rreq->dev.real_user_buf, rreq->dev.user_count, rreq->dev.datatype,
                                 stream_offset, rreq->dev.op, MPIDI_RMA_ACC_SRCBUF_DEFAULT);

    if (win_ptr->shm_allocated == TRUE) {
        MPIDI_CH3I_SHM_MUTEX_UNLOCK(win_ptr);
    }

    MPIR_ERR_CHECK(mpi_errno);

    resp_req->dev.OnFinal = MPIDI_CH3_ReqHandler_GaccumSendComplete;
    resp_req->dev.OnDataAvail = MPIDI_CH3_ReqHandler_GaccumSendComplete;
    resp_req->dev.target_win_handle = rreq->dev.target_win_handle;
    resp_req->dev.pkt_flags = rreq->dev.pkt_flags;

    /* here we increment the Active Target counter to guarantee the GET-like
     * operation are completed when counter reaches zero. */
    win_ptr->at_completion_counter++;

    iov[0].iov_base = (void *) get_accum_resp_pkt;
    iov[0].iov_len = sizeof(*get_accum_resp_pkt);
    iov[1].iov_base = (void *) ((char *) resp_req->dev.user_buf);
    iov[1].iov_len = stream_data_len;
    iovcnt = 2;

    mpi_errno = MPIDI_CH3_iSendv(vc, resp_req, iov, iovcnt);

    MPIR_ERR_CHKANDJUMP(mpi_errno != MPI_SUCCESS, mpi_errno, MPI_ERR_OTHER, "**ch3|rmamsg");

    /* Mark get portion as handled */
    rreq->dev.resp_request_handle = MPI_REQUEST_NULL;

    MPIR_Assert(MPIDI_Request_get_type(rreq) == MPIDI_REQUEST_TYPE_GET_ACCUM_RECV);

    if (is_empty_origin == FALSE) {
        /* free the temporary buffer.
         * When origin data is zero, there
         * is no temporary buffer allocated */
        MPIDI_CH3U_SRBuf_free(rreq);
    }

    /* mark data transfer as complete and decrement CC */
    mpi_errno = MPID_Request_complete(rreq);
    MPIR_ERR_CHECK(mpi_errno);

    *complete = TRUE;
  fn_exit:
    MPIR_CHKPMEM_COMMIT();
    MPIR_FUNC_EXIT;
    return mpi_errno;

    /* --BEGIN ERROR HANDLING-- */
  fn_fail:
    MPIR_CHKPMEM_REAP();
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}


int MPIDI_CH3_ReqHandler_FOPRecvComplete(MPIDI_VC_t * vc, MPIR_Request * rreq, int *complete)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_Win *win_ptr = NULL;
    MPI_Aint type_size;
    MPIR_Request *resp_req = NULL;
    struct iovec iov[MPL_IOV_LIMIT];
    int iovcnt;
    MPIDI_CH3_Pkt_t upkt;
    MPIDI_CH3_Pkt_fop_resp_t *fop_resp_pkt = &upkt.fop_resp;
    int is_contig;
    int is_empty_origin = FALSE;
    MPIR_CHKPMEM_DECL();

    MPIR_FUNC_ENTER;

    /* Judge if origin buffer is empty */
    if (rreq->dev.op == MPI_NO_OP) {
        is_empty_origin = TRUE;
    }

    MPIR_Assert(MPIDI_Request_get_type(rreq) == MPIDI_REQUEST_TYPE_FOP_RECV);

    MPIR_Win_get_ptr(rreq->dev.target_win_handle, win_ptr);

    MPIR_Datatype_get_size_macro(rreq->dev.datatype, type_size);

    MPIR_Datatype_is_contig(rreq->dev.datatype, &is_contig);

    /* Create response request */
    resp_req = MPIR_Request_create(MPIR_REQUEST_KIND__UNDEFINED);
    MPIR_ERR_CHKANDJUMP(resp_req == NULL, mpi_errno, MPI_ERR_OTHER, "**nomemreq");
    MPIDI_Request_set_type(resp_req, MPIDI_REQUEST_TYPE_FOP_RESP);
    MPIR_Object_set_ref(resp_req, 1);
    resp_req->dev.OnFinal = MPIDI_CH3_ReqHandler_FOPSendComplete;
    resp_req->dev.OnDataAvail = MPIDI_CH3_ReqHandler_FOPSendComplete;
    resp_req->dev.target_win_handle = rreq->dev.target_win_handle;
    resp_req->dev.pkt_flags = rreq->dev.pkt_flags;

    MPIR_CHKPMEM_MALLOC(resp_req->dev.user_buf, type_size, MPL_MEM_BUFFER);

    /* here we increment the Active Target counter to guarantee the GET-like
     * operation are completed when counter reaches zero. */
    win_ptr->at_completion_counter++;

    /* NOTE: 'copy data + ACC' needs to be atomic */

    if (win_ptr->shm_allocated == TRUE) {
        MPIDI_CH3I_SHM_MUTEX_LOCK(win_ptr);
    }

    /* Copy data into a temporary buffer in response request */
    if (is_contig) {
        MPIR_Memcpy(resp_req->dev.user_buf, rreq->dev.real_user_buf, type_size);
    }
    else {
        MPI_Aint actual_pack_bytes;
        MPIR_Typerep_pack(rreq->dev.real_user_buf, 1, rreq->dev.datatype, 0, resp_req->dev.user_buf,
                       type_size, &actual_pack_bytes, MPIR_TYPEREP_FLAG_NONE);
        MPIR_Assert(actual_pack_bytes == type_size);
    }

    /* Perform accumulate computation */
    mpi_errno = do_accumulate_op(rreq->dev.user_buf, 1, rreq->dev.datatype,
                                 rreq->dev.real_user_buf, 1, rreq->dev.datatype, 0, rreq->dev.op,
                                 MPIDI_RMA_ACC_SRCBUF_DEFAULT);

    if (win_ptr->shm_allocated == TRUE) {
        MPIDI_CH3I_SHM_MUTEX_UNLOCK(win_ptr);
    }

    MPIR_ERR_CHECK(mpi_errno);

    /* Send back data */
    MPIDI_Pkt_init(fop_resp_pkt, MPIDI_CH3_PKT_FOP_RESP);
    fop_resp_pkt->request_handle = rreq->dev.resp_request_handle;
    fop_resp_pkt->target_rank = win_ptr->comm_ptr->rank;
    fop_resp_pkt->pkt_flags = MPIDI_CH3_PKT_FLAG_NONE;
    if (rreq->dev.pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_LOCK_SHARED ||
        rreq->dev.pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_LOCK_EXCLUSIVE)
        fop_resp_pkt->pkt_flags |= MPIDI_CH3_PKT_FLAG_RMA_LOCK_GRANTED;
    if ((rreq->dev.pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_FLUSH) ||
        (rreq->dev.pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_UNLOCK))
        fop_resp_pkt->pkt_flags |= MPIDI_CH3_PKT_FLAG_RMA_ACK;

    iov[0].iov_base = (void *) fop_resp_pkt;
    iov[0].iov_len = sizeof(*fop_resp_pkt);
    iov[1].iov_base = (void *) ((char *) resp_req->dev.user_buf);
    iov[1].iov_len = type_size;
    iovcnt = 2;

    mpi_errno = MPIDI_CH3_iSendv(vc, resp_req, iov, iovcnt);

    MPIR_ERR_CHKANDJUMP(mpi_errno != MPI_SUCCESS, mpi_errno, MPI_ERR_OTHER, "**ch3|rmamsg");

    if (is_empty_origin == FALSE) {
        /* free the temporary buffer.
         * When origin data is zero, there
         * is no temporary buffer allocated */
        MPL_free((char *) rreq->dev.user_buf);
    }

    /* mark data transfer as complete and decrement CC */
    mpi_errno = MPID_Request_complete(rreq);
    MPIR_ERR_CHECK(mpi_errno);

    *complete = TRUE;

  fn_exit:
    MPIR_CHKPMEM_COMMIT();
    MPIR_FUNC_EXIT;
    return mpi_errno;
    /* --BEGIN ERROR HANDLING-- */
  fn_fail:
    MPIR_CHKPMEM_REAP();
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}


int MPIDI_CH3_ReqHandler_PutDerivedDTRecvComplete(MPIDI_VC_t * vc ATTRIBUTE((unused)),
                                                  MPIR_Request * rreq, int *complete)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_Datatype*new_dtp = NULL;

    MPIR_FUNC_ENTER;

    /* get data from extended header */
    new_dtp = (MPIR_Datatype *) MPIR_Handle_obj_alloc(&MPIR_Datatype_mem);
    if (!new_dtp) {
        MPIR_ERR_SETANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem", "**nomem %s",
                             "MPIR_Datatype_mem");
    }
    /* Note: handle is filled in by MPIR_Handle_obj_alloc() */
    MPIR_Object_set_ref(new_dtp, 1);
    MPIR_Typerep_unflatten(new_dtp, rreq->dev.flattened_type);

    /* update request to get the data */
    MPIDI_Request_set_type(rreq, MPIDI_REQUEST_TYPE_PUT_RECV);
    rreq->dev.datatype = new_dtp->handle;
    rreq->dev.recv_data_sz = new_dtp->size * rreq->dev.user_count;

    rreq->dev.datatype_ptr = new_dtp;

    rreq->dev.msg_offset = 0;
    rreq->dev.msgsize = rreq->dev.recv_data_sz;

    mpi_errno = MPIDI_CH3U_Request_load_recv_iov(rreq);
    if (mpi_errno != MPI_SUCCESS) {
        MPIR_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**ch3|loadrecviov");
    }
    if (!rreq->dev.OnDataAvail)
        rreq->dev.OnDataAvail = MPIDI_CH3_ReqHandler_PutRecvComplete;

    *complete = FALSE;
  fn_fail:
    MPIR_FUNC_EXIT;
    return mpi_errno;
}

int MPIDI_CH3_ReqHandler_AccumMetadataRecvComplete(MPIDI_VC_t * vc ATTRIBUTE((unused)),
                                                   MPIR_Request * rreq, int *complete)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_Datatype*new_dtp = NULL;
    MPI_Aint basic_type_extent, basic_type_size;
    MPI_Aint total_len, rest_len, stream_elem_count;
    MPI_Aint stream_offset;
    MPI_Aint type_size;
    MPI_Datatype basic_dtp;

    MPIR_FUNC_ENTER;

    stream_offset = 0;

    /* get stream offset */
    if (rreq->dev.pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_STREAM) {
        MPIDI_CH3_Ext_pkt_stream_t *ext_hdr = NULL;
        MPIR_Assert(rreq->dev.ext_hdr_ptr != NULL);
        ext_hdr = ((MPIDI_CH3_Ext_pkt_stream_t *) rreq->dev.ext_hdr_ptr);
        stream_offset = ext_hdr->stream_offset;
    }

    if (MPIDI_Request_get_type(rreq) == MPIDI_REQUEST_TYPE_ACCUM_RECV_DERIVED_DT) {
        new_dtp = (MPIR_Datatype *) MPIR_Handle_obj_alloc(&MPIR_Datatype_mem);
        if (!new_dtp) {
            MPIR_ERR_SETANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem", "**nomem %s",
                                 "MPIR_Datatype_mem");
        }
        /* Note: handle is filled in by MPIR_Handle_obj_alloc() */
        MPIR_Object_set_ref(new_dtp, 1);
        MPIR_Typerep_unflatten(new_dtp, rreq->dev.flattened_type);

        /* update new request to get the data */
        MPIDI_Request_set_type(rreq, MPIDI_REQUEST_TYPE_ACCUM_RECV);

        MPIR_Assert(rreq->dev.datatype == MPI_DATATYPE_NULL);
        rreq->dev.datatype = new_dtp->handle;
        rreq->dev.datatype_ptr = new_dtp;

        type_size = new_dtp->size;

        basic_dtp = new_dtp->basic_type;
    }
    else {
        MPIR_Assert(MPIDI_Request_get_type(rreq) == MPIDI_REQUEST_TYPE_ACCUM_RECV);
        MPIR_Assert(rreq->dev.datatype != MPI_DATATYPE_NULL);

        MPIR_Datatype_get_size_macro(rreq->dev.datatype, type_size);

        basic_dtp = rreq->dev.datatype;
    }

    MPIR_Datatype_get_size_macro(basic_dtp, basic_type_size);
    MPIR_Datatype_get_extent_macro(basic_dtp, basic_type_extent);

    MPIR_Assert(!MPIDI_Request_get_srbuf_flag(rreq));
    /* allocate a SRBuf for receiving stream unit */
    MPIDI_CH3U_SRBuf_alloc(rreq, MPIDI_CH3U_SRBuf_size);
    /* --BEGIN ERROR HANDLING-- */
    if (rreq->dev.tmpbuf_sz == 0) {
        MPL_DBG_MSG(MPIDI_CH3_DBG_CHANNEL, TYPICAL, "SRBuf allocation failure");
        mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                         __func__, __LINE__, MPI_ERR_OTHER, "**nomem",
                                         "**nomem %d", MPIDI_CH3U_SRBuf_size);
        rreq->status.MPI_ERROR = mpi_errno;
        goto fn_fail;
    }
    /* --END ERROR HANDLING-- */

    rreq->dev.user_buf = rreq->dev.tmpbuf;

    total_len = type_size * rreq->dev.user_count;
    rest_len = total_len - stream_offset;
    stream_elem_count = MPIDI_CH3U_SRBuf_size / basic_type_extent;

    rreq->dev.recv_data_sz = MPL_MIN(rest_len, stream_elem_count * basic_type_size);

    rreq->dev.msg_offset = 0;
    rreq->dev.msgsize = rreq->dev.recv_data_sz;

    MPI_Aint actual_iov_bytes, actual_iov_len;
    MPIR_Typerep_to_iov(rreq->dev.tmpbuf, rreq->dev.recv_data_sz / basic_type_size, basic_dtp,
                     0, rreq->dev.iov, MPL_IOV_LIMIT, rreq->dev.recv_data_sz,
                     &actual_iov_len, &actual_iov_bytes);
    rreq->dev.iov_count = (int) actual_iov_len;
    rreq->dev.iov_offset = 0;

    rreq->dev.OnDataAvail = MPIDI_CH3_ReqHandler_AccumRecvComplete;

    *complete = FALSE;
  fn_fail:
    MPIR_FUNC_EXIT;
    return mpi_errno;
}


int MPIDI_CH3_ReqHandler_GaccumMetadataRecvComplete(MPIDI_VC_t * vc,
                                                    MPIR_Request * rreq, int *complete)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_Datatype*new_dtp = NULL;
    MPI_Aint basic_type_extent, basic_type_size;
    MPI_Aint total_len, rest_len, stream_elem_count;
    MPI_Aint stream_offset;
    MPI_Aint type_size;
    MPI_Datatype basic_dtp;
    int is_empty_origin = FALSE;

    MPIR_FUNC_ENTER;

    /* Judge if origin buffer is empty */
    if (rreq->dev.op == MPI_NO_OP) {
        is_empty_origin = TRUE;
    }

    stream_offset = 0;

    /* get stream offset */
    if (rreq->dev.pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_STREAM) {
        MPIDI_CH3_Ext_pkt_stream_t *ext_hdr = NULL;
        MPIR_Assert(rreq->dev.ext_hdr_ptr != NULL);
        ext_hdr = ((MPIDI_CH3_Ext_pkt_stream_t *) rreq->dev.ext_hdr_ptr);
        stream_offset = ext_hdr->stream_offset;
    }

    if (MPIDI_Request_get_type(rreq) == MPIDI_REQUEST_TYPE_GET_ACCUM_RECV_DERIVED_DT) {
        new_dtp = (MPIR_Datatype *) MPIR_Handle_obj_alloc(&MPIR_Datatype_mem);
        if (!new_dtp) {
            MPIR_ERR_SETANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem", "**nomem %s",
                                 "MPIR_Datatype_mem");
        }
        /* Note: handle is filled in by MPIR_Handle_obj_alloc() */
        MPIR_Object_set_ref(new_dtp, 1);
        MPIR_Typerep_unflatten(new_dtp, rreq->dev.flattened_type);

        /* update new request to get the data */
        MPIDI_Request_set_type(rreq, MPIDI_REQUEST_TYPE_GET_ACCUM_RECV);

        MPIR_Assert(rreq->dev.datatype == MPI_DATATYPE_NULL);
        rreq->dev.datatype = new_dtp->handle;
        rreq->dev.datatype_ptr = new_dtp;

        type_size = new_dtp->size;

        basic_dtp = new_dtp->basic_type;
    }
    else {
        MPIR_Assert(MPIDI_Request_get_type(rreq) == MPIDI_REQUEST_TYPE_GET_ACCUM_RECV);
        MPIR_Assert(rreq->dev.datatype != MPI_DATATYPE_NULL);

        MPIR_Datatype_get_size_macro(rreq->dev.datatype, type_size);

        basic_dtp = rreq->dev.datatype;
    }

    if (is_empty_origin == TRUE) {
        rreq->dev.recv_data_sz = 0;

        /* There is no origin data coming, directly call final req handler. */
        mpi_errno = MPIDI_CH3_ReqHandler_GaccumRecvComplete(vc, rreq, complete);
        MPIR_ERR_CHECK(mpi_errno);
    }
    else {
        MPIR_Datatype_get_size_macro(basic_dtp, basic_type_size);
        MPIR_Datatype_get_extent_macro(basic_dtp, basic_type_extent);

        MPIR_Assert(!MPIDI_Request_get_srbuf_flag(rreq));
        /* allocate a SRBuf for receiving stream unit */
        MPIDI_CH3U_SRBuf_alloc(rreq, MPIDI_CH3U_SRBuf_size);
        /* --BEGIN ERROR HANDLING-- */
        if (rreq->dev.tmpbuf_sz == 0) {
            MPL_DBG_MSG(MPIDI_CH3_DBG_CHANNEL, TYPICAL, "SRBuf allocation failure");
            mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                             __func__, __LINE__, MPI_ERR_OTHER, "**nomem",
                                             "**nomem %d", MPIDI_CH3U_SRBuf_size);
            rreq->status.MPI_ERROR = mpi_errno;
            goto fn_fail;
        }
        /* --END ERROR HANDLING-- */

        rreq->dev.user_buf = rreq->dev.tmpbuf;

        total_len = type_size * rreq->dev.user_count;
        rest_len = total_len - stream_offset;
        stream_elem_count = MPIDI_CH3U_SRBuf_size / basic_type_extent;

        rreq->dev.recv_data_sz = MPL_MIN(rest_len, stream_elem_count * basic_type_size);

        rreq->dev.msg_offset = 0;
        rreq->dev.msgsize = rreq->dev.recv_data_sz;

        MPI_Aint actual_iov_bytes, actual_iov_len;
        MPIR_Typerep_to_iov(rreq->dev.tmpbuf, rreq->dev.recv_data_sz / basic_type_size, basic_dtp,
                         0, rreq->dev.iov, MPL_IOV_LIMIT, rreq->dev.recv_data_sz,
                         &actual_iov_len, &actual_iov_bytes);
        rreq->dev.iov_count = actual_iov_len;
        rreq->dev.iov_offset = 0;

        rreq->dev.OnDataAvail = MPIDI_CH3_ReqHandler_GaccumRecvComplete;

        *complete = FALSE;
    }

  fn_fail:
    MPIR_FUNC_EXIT;
    return mpi_errno;
}



int MPIDI_CH3_ReqHandler_GetDerivedDTRecvComplete(MPIDI_VC_t * vc,
                                                  MPIR_Request * rreq, int *complete)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_Datatype*new_dtp = NULL;
    MPIDI_CH3_Pkt_t upkt;
    MPIDI_CH3_Pkt_get_resp_t *get_resp_pkt = &upkt.get_resp;
    MPIR_Request *sreq;
    MPIR_Win *win_ptr;

    MPIR_FUNC_ENTER;

    MPIR_Win_get_ptr(rreq->dev.target_win_handle, win_ptr);

    MPIR_Assert(!(rreq->dev.pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_IMMED_RESP));

    /* get data from extended header */
    new_dtp = (MPIR_Datatype *) MPIR_Handle_obj_alloc(&MPIR_Datatype_mem);
    if (!new_dtp) {
        MPIR_ERR_SETANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem", "**nomem %s",
                             "MPIR_Datatype_mem");
    }
    /* Note: handle is filled in by MPIR_Handle_obj_alloc() */
    MPIR_Object_set_ref(new_dtp, 1);
    MPIR_Typerep_unflatten(new_dtp, rreq->dev.flattened_type);

    /* create request for sending data */
    sreq = MPIR_Request_create(MPIR_REQUEST_KIND__UNDEFINED);
    MPIR_ERR_CHKANDJUMP(sreq == NULL, mpi_errno, MPI_ERR_OTHER, "**nomemreq");

    sreq->kind = MPIR_REQUEST_KIND__SEND;
    MPIDI_Request_set_type(sreq, MPIDI_REQUEST_TYPE_GET_RESP);
    sreq->dev.OnDataAvail = MPIDI_CH3_ReqHandler_GetSendComplete;
    sreq->dev.OnFinal = MPIDI_CH3_ReqHandler_GetSendComplete;
    sreq->dev.user_buf = rreq->dev.user_buf;
    sreq->dev.user_count = rreq->dev.user_count;
    sreq->dev.datatype = new_dtp->handle;
    sreq->dev.datatype_ptr = new_dtp;
    sreq->dev.target_win_handle = rreq->dev.target_win_handle;
    sreq->dev.pkt_flags = rreq->dev.pkt_flags;

    MPIDI_Pkt_init(get_resp_pkt, MPIDI_CH3_PKT_GET_RESP);
    get_resp_pkt->request_handle = rreq->dev.request_handle;
    get_resp_pkt->target_rank = win_ptr->comm_ptr->rank;
    get_resp_pkt->pkt_flags = MPIDI_CH3_PKT_FLAG_NONE;
    if (rreq->dev.pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_LOCK_SHARED ||
        rreq->dev.pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_LOCK_EXCLUSIVE)
        get_resp_pkt->pkt_flags |= MPIDI_CH3_PKT_FLAG_RMA_LOCK_GRANTED;
    if ((rreq->dev.pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_FLUSH) ||
        (rreq->dev.pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_UNLOCK))
        get_resp_pkt->pkt_flags |= MPIDI_CH3_PKT_FLAG_RMA_ACK;

    sreq->dev.msg_offset = 0;
    sreq->dev.msgsize = new_dtp->size * sreq->dev.user_count;

    mpi_errno = vc->sendNoncontig_fn(vc, sreq, get_resp_pkt, sizeof(*get_resp_pkt),
                                     NULL, 0);
    /* --BEGIN ERROR HANDLING-- */
    if (mpi_errno != MPI_SUCCESS) {
        MPIR_Request_free(sreq);
        sreq = NULL;
        MPIR_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**ch3|rmamsg");
    }
    /* --END ERROR HANDLING-- */

    /* mark receive data transfer as complete and decrement CC in receive
     * request */
    mpi_errno = MPID_Request_complete(rreq);
    MPIR_ERR_CHECK(mpi_errno);

    *complete = TRUE;

  fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}


int MPIDI_CH3_ReqHandler_UnpackUEBufComplete(MPIDI_VC_t * vc ATTRIBUTE((unused)),
                                             MPIR_Request * rreq, int *complete)
{
    int recv_pending;
    int mpi_errno = MPI_SUCCESS;

    MPIR_FUNC_ENTER;

    MPIDI_Request_decr_pending(rreq);
    MPIDI_Request_check_pending(rreq, &recv_pending);
    if (!recv_pending) {
        if (rreq->dev.recv_data_sz > 0) {
            MPIDI_CH3U_Request_unpack_uebuf(rreq);
            MPL_free(rreq->dev.tmpbuf);
        }
    }
    else {
        /* The receive has not been posted yet.  MPID_{Recv/Irecv}()
         * is responsible for unpacking the buffer. */
    }

    /* mark data transfer as complete and decrement CC */
    mpi_errno = MPID_Request_complete(rreq);
    MPIR_ERR_CHECK(mpi_errno);

    *complete = TRUE;

  fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

int MPIDI_CH3_ReqHandler_UnpackSRBufComplete(MPIDI_VC_t * vc, MPIR_Request * rreq, int *complete)
{
    int mpi_errno = MPI_SUCCESS;

    MPIR_FUNC_ENTER;

    MPIDI_CH3U_Request_unpack_srbuf(rreq);

    if (MPIDI_Request_get_type(rreq) == MPIDI_REQUEST_TYPE_PUT_RECV) {
        mpi_errno = MPIDI_CH3_ReqHandler_PutRecvComplete(vc, rreq, complete);
    }
    else if (MPIDI_Request_get_type(rreq) == MPIDI_REQUEST_TYPE_ACCUM_RECV) {
        mpi_errno = MPIDI_CH3_ReqHandler_AccumRecvComplete(vc, rreq, complete);
    }
    else if (MPIDI_Request_get_type(rreq) == MPIDI_REQUEST_TYPE_GET_ACCUM_RECV) {
        mpi_errno = MPIDI_CH3_ReqHandler_GaccumRecvComplete(vc, rreq, complete);
    }
    else if (MPIDI_Request_get_type(rreq) == MPIDI_REQUEST_TYPE_FOP_RECV) {
        mpi_errno = MPIDI_CH3_ReqHandler_FOPRecvComplete(vc, rreq, complete);
    }
    else {
        /* mark data transfer as complete and decrement CC */
        mpi_errno = MPID_Request_complete(rreq);
        MPIR_ERR_CHECK(mpi_errno);

        *complete = TRUE;
    }

  fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

int MPIDI_CH3_ReqHandler_UnpackSRBufReloadIOV(MPIDI_VC_t * vc ATTRIBUTE((unused)),
                                              MPIR_Request * rreq, int *complete)
{
    int mpi_errno = MPI_SUCCESS;

    MPIR_FUNC_ENTER;

    MPIDI_CH3U_Request_unpack_srbuf(rreq);
    mpi_errno = MPIDI_CH3U_Request_load_recv_iov(rreq);
    if (mpi_errno != MPI_SUCCESS) {
        MPIR_ERR_SETFATALANDJUMP(mpi_errno, MPI_ERR_OTHER, "**ch3|loadrecviov");
    }
    *complete = FALSE;
  fn_fail:
    MPIR_FUNC_EXIT;
    return mpi_errno;
}

int MPIDI_CH3_ReqHandler_ReloadIOV(MPIDI_VC_t * vc ATTRIBUTE((unused)),
                                   MPIR_Request * rreq, int *complete)
{
    int mpi_errno = MPI_SUCCESS;

    MPIR_FUNC_ENTER;

    mpi_errno = MPIDI_CH3U_Request_load_recv_iov(rreq);
    if (mpi_errno != MPI_SUCCESS) {
        MPIR_ERR_SETFATALANDJUMP(mpi_errno, MPI_ERR_OTHER, "**ch3|loadrecviov");
    }
    *complete = FALSE;
  fn_fail:
    MPIR_FUNC_EXIT;
    return mpi_errno;
}

/* ----------------------------------------------------------------------- */
/* ----------------------------------------------------------------------- */

static inline int perform_put_in_lock_queue(MPIR_Win * win_ptr,
                                            MPIDI_RMA_Target_lock_entry_t * target_lock_entry)
{
    MPIDI_CH3_Pkt_put_t *put_pkt = &((target_lock_entry->pkt).put);
    int mpi_errno = MPI_SUCCESS;

    /* Piggyback candidate should have basic datatype for target datatype. */
    MPIR_Assert(MPIR_DATATYPE_IS_PREDEFINED(put_pkt->datatype));

    /* Make sure that all data is received for this op. */
    MPIR_Assert(target_lock_entry->all_data_recved == 1);

    if (put_pkt->type == MPIDI_CH3_PKT_PUT_IMMED) {
        /* all data fits in packet header */
        mpi_errno = MPIR_Localcopy((void *) &put_pkt->info.data, put_pkt->count, put_pkt->datatype,
                                   put_pkt->addr, put_pkt->count, put_pkt->datatype);
        MPIR_ERR_CHECK(mpi_errno);
    }
    else {
        MPIR_Assert(put_pkt->type == MPIDI_CH3_PKT_PUT);

        mpi_errno = MPIR_Localcopy(target_lock_entry->data, put_pkt->count, put_pkt->datatype,
                                   put_pkt->addr, put_pkt->count, put_pkt->datatype);
        MPIR_ERR_CHECK(mpi_errno);
    }

    /* do final action */
    mpi_errno =
        finish_op_on_target(win_ptr, target_lock_entry->vc, FALSE /* has no response data */ ,
                            put_pkt->pkt_flags, put_pkt->source_win_handle);
    MPIR_ERR_CHECK(mpi_errno);

  fn_exit:
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

static inline int perform_get_in_lock_queue(MPIR_Win * win_ptr,
                                            MPIDI_RMA_Target_lock_entry_t * target_lock_entry)
{
    MPIDI_CH3_Pkt_t upkt;
    MPIDI_CH3_Pkt_get_resp_t *get_resp_pkt = &upkt.get_resp;
    MPIDI_CH3_Pkt_get_t *get_pkt = &((target_lock_entry->pkt).get);
    MPIR_Request *sreq = NULL;
    MPI_Aint type_size;
    size_t len;
    int iovcnt;
    struct iovec iov[MPL_IOV_LIMIT];
    int is_contig;
    int mpi_errno = MPI_SUCCESS;

    /* Piggyback candidate should have basic datatype for target datatype. */
    MPIR_Assert(MPIR_DATATYPE_IS_PREDEFINED(get_pkt->datatype));

    /* Make sure that all data is received for this op. */
    MPIR_Assert(target_lock_entry->all_data_recved == 1);

    sreq = MPIR_Request_create(MPIR_REQUEST_KIND__UNDEFINED);
    if (sreq == NULL) {
        MPIR_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**nomemreq");
    }
    MPIR_Object_set_ref(sreq, 1);

    MPIDI_Request_set_type(sreq, MPIDI_REQUEST_TYPE_GET_RESP);
    sreq->kind = MPIR_REQUEST_KIND__SEND;
    sreq->dev.OnDataAvail = MPIDI_CH3_ReqHandler_GetSendComplete;
    sreq->dev.OnFinal = MPIDI_CH3_ReqHandler_GetSendComplete;

    sreq->dev.target_win_handle = win_ptr->handle;
    sreq->dev.pkt_flags = get_pkt->pkt_flags;

    /* here we increment the Active Target counter to guarantee the GET-like
     * operation are completed when counter reaches zero. */
    win_ptr->at_completion_counter++;

    if (get_pkt->pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_IMMED_RESP) {
        MPIDI_Pkt_init(get_resp_pkt, MPIDI_CH3_PKT_GET_RESP_IMMED);
    }
    else {
        MPIDI_Pkt_init(get_resp_pkt, MPIDI_CH3_PKT_GET_RESP);
    }
    get_resp_pkt->request_handle = get_pkt->request_handle;
    get_resp_pkt->pkt_flags = MPIDI_CH3_PKT_FLAG_NONE;
    if (get_pkt->pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_LOCK_SHARED ||
        get_pkt->pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_LOCK_EXCLUSIVE)
        get_resp_pkt->pkt_flags |= MPIDI_CH3_PKT_FLAG_RMA_LOCK_GRANTED;
    if ((get_pkt->pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_FLUSH) ||
        (get_pkt->pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_UNLOCK))
        get_resp_pkt->pkt_flags |= MPIDI_CH3_PKT_FLAG_RMA_ACK;
    get_resp_pkt->target_rank = win_ptr->comm_ptr->rank;

    /* length of target data */
    MPIR_Datatype_get_size_macro(get_pkt->datatype, type_size);
    MPIR_Assign_trunc(len, get_pkt->count * type_size, size_t);

    MPIR_Datatype_is_contig(get_pkt->datatype, &is_contig);

    if (get_pkt->pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_IMMED_RESP) {
        void *src = (void *) (get_pkt->addr), *dest = (void *) &get_resp_pkt->info.data;
        mpi_errno = immed_copy(src, dest, len);
        MPIR_ERR_CHECK(mpi_errno);

        /* All origin data is in packet header, issue the header. */
        iov[0].iov_base = (void *) get_resp_pkt;
        iov[0].iov_len = sizeof(*get_resp_pkt);
        iovcnt = 1;

        mpi_errno = MPIDI_CH3_iSendv(target_lock_entry->vc, sreq, iov, iovcnt);
        if (mpi_errno != MPI_SUCCESS) {
            MPIR_Request_free(sreq);
            MPIR_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**ch3|rmamsg");
        }
    }
    else if (is_contig) {
        iov[0].iov_base = (void *) get_resp_pkt;
        iov[0].iov_len = sizeof(*get_resp_pkt);
        iov[1].iov_base = (void *) (get_pkt->addr);
        iov[1].iov_len = get_pkt->count * type_size;
        iovcnt = 2;

        mpi_errno = MPIDI_CH3_iSendv(target_lock_entry->vc, sreq, iov, iovcnt);
        if (mpi_errno != MPI_SUCCESS) {
            MPIR_Request_free(sreq);
            MPIR_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**ch3|rmamsg");
        }
    }
    else {
        iov[0].iov_base = (void *) get_resp_pkt;
        iov[0].iov_len = sizeof(*get_resp_pkt);

        sreq->dev.user_buf = get_pkt->addr;
        sreq->dev.user_count = get_pkt->count;
        sreq->dev.datatype = get_pkt->datatype;
        sreq->dev.msg_offset = 0;
        sreq->dev.msgsize = get_pkt->count * type_size;

        mpi_errno = target_lock_entry->vc->sendNoncontig_fn(target_lock_entry->vc, sreq,
                                                            iov[0].iov_base, iov[0].iov_len,
                                                            NULL, 0);
        MPIR_ERR_CHKANDJUMP(mpi_errno, mpi_errno, MPI_ERR_OTHER, "**ch3|rmamsg");
    }

  fn_exit:
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}


static inline int perform_acc_in_lock_queue(MPIR_Win * win_ptr,
                                            MPIDI_RMA_Target_lock_entry_t * target_lock_entry)
{
    MPIDI_CH3_Pkt_accum_t *acc_pkt = &((target_lock_entry->pkt).accum);
    int mpi_errno = MPI_SUCCESS;

    MPIR_Assert(target_lock_entry->all_data_recved == 1);

    /* Piggyback candidate should have basic datatype for target datatype. */
    MPIR_Assert(MPIR_DATATYPE_IS_PREDEFINED(acc_pkt->datatype));

    if (win_ptr->shm_allocated == TRUE) {
        MPIDI_CH3I_SHM_MUTEX_LOCK(win_ptr);
    }

    if (acc_pkt->type == MPIDI_CH3_PKT_ACCUMULATE_IMMED) {
        /* All data fits in packet header */
        mpi_errno = do_accumulate_op((void *) &acc_pkt->info.data, acc_pkt->count,
                                     acc_pkt->datatype, acc_pkt->addr, acc_pkt->count,
                                     acc_pkt->datatype, 0, acc_pkt->op, MPIDI_RMA_ACC_SRCBUF_DEFAULT);
    }
    else {
        MPIR_Assert(acc_pkt->type == MPIDI_CH3_PKT_ACCUMULATE);
        MPI_Aint type_size, type_extent;
        MPI_Aint total_len, recv_count;

        MPIR_Datatype_get_size_macro(acc_pkt->datatype, type_size);
        MPIR_Datatype_get_extent_macro(acc_pkt->datatype, type_extent);

        total_len = type_size * acc_pkt->count;
        recv_count = MPL_MIN((total_len / type_size), (MPIDI_CH3U_SRBuf_size / type_extent));
        MPIR_Assert(recv_count > 0);

        /* Note: here stream_offset is 0 because when piggybacking LOCK, we must use
         * the first stream unit. */
        MPIR_Assert(recv_count == (int) recv_count);
        mpi_errno = do_accumulate_op(target_lock_entry->data, (int) recv_count, acc_pkt->datatype,
                                     acc_pkt->addr, acc_pkt->count, acc_pkt->datatype,
                                     0, acc_pkt->op, MPIDI_RMA_ACC_SRCBUF_DEFAULT);
    }

    if (win_ptr->shm_allocated == TRUE) {
        MPIDI_CH3I_SHM_MUTEX_UNLOCK(win_ptr);
    }

    MPIR_ERR_CHECK(mpi_errno);

    mpi_errno =
        finish_op_on_target(win_ptr, target_lock_entry->vc, FALSE /* has no response data */ ,
                            acc_pkt->pkt_flags, acc_pkt->source_win_handle);
    MPIR_ERR_CHECK(mpi_errno);

  fn_exit:
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}


static inline int perform_get_acc_in_lock_queue(MPIR_Win * win_ptr,
                                                MPIDI_RMA_Target_lock_entry_t * target_lock_entry)
{
    MPIDI_CH3_Pkt_t upkt;
    MPIDI_CH3_Pkt_get_accum_resp_t *get_accum_resp_pkt = &upkt.get_accum_resp;
    MPIDI_CH3_Pkt_get_accum_t *get_accum_pkt = &((target_lock_entry->pkt).get_accum);
    MPIR_Request *sreq = NULL;
    MPI_Aint type_size;
    size_t len;
    int iovcnt;
    struct iovec iov[MPL_IOV_LIMIT];
    int is_contig;
    int mpi_errno = MPI_SUCCESS;
    MPI_Aint type_extent;
    MPI_Aint total_len, recv_count;

    /* Piggyback candidate should have basic datatype for target datatype. */
    MPIR_Assert(MPIR_DATATYPE_IS_PREDEFINED(get_accum_pkt->datatype));

    /* Make sure that all data is received for this op. */
    MPIR_Assert(target_lock_entry->all_data_recved == 1);

    sreq = MPIR_Request_create(MPIR_REQUEST_KIND__UNDEFINED);
    if (sreq == NULL) {
        MPIR_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**nomemreq");
    }
    MPIR_Object_set_ref(sreq, 1);

    MPIDI_Request_set_type(sreq, MPIDI_REQUEST_TYPE_GET_ACCUM_RESP);
    sreq->kind = MPIR_REQUEST_KIND__SEND;
    sreq->dev.OnDataAvail = MPIDI_CH3_ReqHandler_GaccumSendComplete;
    sreq->dev.OnFinal = MPIDI_CH3_ReqHandler_GaccumSendComplete;

    sreq->dev.target_win_handle = win_ptr->handle;
    sreq->dev.pkt_flags = get_accum_pkt->pkt_flags;

    /* Copy data into a temporary buffer */
    MPIR_Datatype_get_size_macro(get_accum_pkt->datatype, type_size);

    /* length of target data */
    MPIR_Assign_trunc(len, get_accum_pkt->count * type_size, size_t);

    if (get_accum_pkt->type == MPIDI_CH3_PKT_GET_ACCUM_IMMED) {
        MPIDI_Pkt_init(get_accum_resp_pkt, MPIDI_CH3_PKT_GET_ACCUM_RESP_IMMED);
        get_accum_resp_pkt->request_handle = get_accum_pkt->request_handle;
        get_accum_resp_pkt->pkt_flags = MPIDI_CH3_PKT_FLAG_NONE;
        if (get_accum_pkt->pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_LOCK_SHARED ||
            get_accum_pkt->pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_LOCK_EXCLUSIVE)
            get_accum_resp_pkt->pkt_flags |= MPIDI_CH3_PKT_FLAG_RMA_LOCK_GRANTED;
        if ((get_accum_pkt->pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_FLUSH) ||
            (get_accum_pkt->pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_UNLOCK))
            get_accum_resp_pkt->pkt_flags |= MPIDI_CH3_PKT_FLAG_RMA_ACK;
        get_accum_resp_pkt->target_rank = win_ptr->comm_ptr->rank;


        /* NOTE: copy 'data + ACC' needs to be atomic */

        if (win_ptr->shm_allocated == TRUE) {
            MPIDI_CH3I_SHM_MUTEX_LOCK(win_ptr);
        }

        /* Copy data from target window to response packet header */

        void *src = (void *) (get_accum_pkt->addr), *dest =
            (void *) &(get_accum_resp_pkt->info.data);
        mpi_errno = immed_copy(src, dest, len);
        if (mpi_errno != MPI_SUCCESS) {
            if (win_ptr->shm_allocated == TRUE) {
                MPIDI_CH3I_SHM_MUTEX_UNLOCK(win_ptr);
            }
            MPIR_ERR_POP(mpi_errno);
        }

        /* Perform ACCUMULATE OP */

        /* All data fits in packet header */
        mpi_errno =
            do_accumulate_op((void *) &get_accum_pkt->info.data, get_accum_pkt->count,
                             get_accum_pkt->datatype, get_accum_pkt->addr, get_accum_pkt->count,
                             get_accum_pkt->datatype, 0, get_accum_pkt->op,
                             MPIDI_RMA_ACC_SRCBUF_DEFAULT);

        if (win_ptr->shm_allocated == TRUE) {
            MPIDI_CH3I_SHM_MUTEX_UNLOCK(win_ptr);
        }

        MPIR_ERR_CHECK(mpi_errno);

        /* here we increment the Active Target counter to guarantee the GET-like
         * operation are completed when counter reaches zero. */
        win_ptr->at_completion_counter++;

        /* All origin data is in packet header, issue the header. */
        iov[0].iov_base = (void *) get_accum_resp_pkt;
        iov[0].iov_len = sizeof(*get_accum_resp_pkt);
        iovcnt = 1;

        mpi_errno = MPIDI_CH3_iSendv(target_lock_entry->vc, sreq, iov, iovcnt);
        if (mpi_errno != MPI_SUCCESS) {
            MPIR_Request_free(sreq);
            MPIR_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**ch3|rmamsg");
        }

        goto fn_exit;
    }

    MPIR_Assert(get_accum_pkt->type == MPIDI_CH3_PKT_GET_ACCUM);

    MPIR_Datatype_get_extent_macro(get_accum_pkt->datatype, type_extent);

    total_len = type_size * get_accum_pkt->count;
    recv_count = MPL_MIN((total_len / type_size), (MPIDI_CH3U_SRBuf_size / type_extent));
    MPIR_Assert(recv_count > 0);

    sreq->dev.user_buf = (void *) MPL_malloc(recv_count * type_size, MPL_MEM_BUFFER);

    MPIR_Datatype_is_contig(get_accum_pkt->datatype, &is_contig);

    /* NOTE: 'copy data + ACC' needs to be atomic */

    if (win_ptr->shm_allocated == TRUE) {
        MPIDI_CH3I_SHM_MUTEX_LOCK(win_ptr);
    }

    /* Copy data from target window to temporary buffer */

    /* Note: here stream_offset is 0 because when piggybacking LOCK, we must use
     * the first stream unit. */

    if (is_contig) {
        MPIR_Memcpy(sreq->dev.user_buf, get_accum_pkt->addr, recv_count * type_size);
    }
    else {
        MPI_Aint actual_pack_bytes;
        MPIR_Typerep_pack(get_accum_pkt->addr, get_accum_pkt->count, get_accum_pkt->datatype,
                       0, sreq->dev.user_buf, type_size * recv_count, &actual_pack_bytes,
                       MPIR_TYPEREP_FLAG_NONE);
        MPIR_Assert(actual_pack_bytes == type_size * recv_count);
    }

    /* Perform ACCUMULATE OP */

    MPIR_Assert(recv_count == (int) recv_count);
    mpi_errno = do_accumulate_op(target_lock_entry->data, (int) recv_count, get_accum_pkt->datatype,
                                 get_accum_pkt->addr, get_accum_pkt->count, get_accum_pkt->datatype,
                                 0, get_accum_pkt->op, MPIDI_RMA_ACC_SRCBUF_DEFAULT);

    if (win_ptr->shm_allocated == TRUE) {
        MPIDI_CH3I_SHM_MUTEX_UNLOCK(win_ptr);
    }

    MPIR_ERR_CHECK(mpi_errno);

    /* here we increment the Active Target counter to guarantee the GET-like
     * operation are completed when counter reaches zero. */
    win_ptr->at_completion_counter++;

    MPIDI_Pkt_init(get_accum_resp_pkt, MPIDI_CH3_PKT_GET_ACCUM_RESP);
    get_accum_resp_pkt->request_handle = get_accum_pkt->request_handle;
    get_accum_resp_pkt->pkt_flags = MPIDI_CH3_PKT_FLAG_NONE;
    if (get_accum_pkt->pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_LOCK_SHARED ||
        get_accum_pkt->pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_LOCK_EXCLUSIVE)
        get_accum_resp_pkt->pkt_flags |= MPIDI_CH3_PKT_FLAG_RMA_LOCK_GRANTED;
    if ((get_accum_pkt->pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_FLUSH) ||
        (get_accum_pkt->pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_UNLOCK))
        get_accum_resp_pkt->pkt_flags |= MPIDI_CH3_PKT_FLAG_RMA_ACK;
    get_accum_resp_pkt->target_rank = win_ptr->comm_ptr->rank;

    iov[0].iov_base = (void *) get_accum_resp_pkt;
    iov[0].iov_len = sizeof(*get_accum_resp_pkt);
    iov[1].iov_base = (void *) ((char *) sreq->dev.user_buf);
    iov[1].iov_len = recv_count * type_size;
    iovcnt = 2;

    mpi_errno = MPIDI_CH3_iSendv(target_lock_entry->vc, sreq, iov, iovcnt);
    if (mpi_errno != MPI_SUCCESS) {
        MPIR_Request_free(sreq);
        MPIR_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**ch3|rmamsg");
    }

  fn_exit:
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}


static inline int perform_fop_in_lock_queue(MPIR_Win * win_ptr,
                                            MPIDI_RMA_Target_lock_entry_t * target_lock_entry)
{
    MPIDI_CH3_Pkt_t upkt;
    MPIDI_CH3_Pkt_fop_resp_t *fop_resp_pkt = &upkt.fop_resp;
    MPIDI_CH3_Pkt_fop_t *fop_pkt = &((target_lock_entry->pkt).fop);
    MPIR_Request *resp_req = NULL;
    MPI_Aint type_size;
    struct iovec iov[MPL_IOV_LIMIT];
    int iovcnt;
    int is_contig;
    int mpi_errno = MPI_SUCCESS;

    /* Piggyback candidate should have basic datatype for target datatype. */
    MPIR_Assert(MPIR_DATATYPE_IS_PREDEFINED(fop_pkt->datatype));

    /* Make sure that all data is received for this op. */
    MPIR_Assert(target_lock_entry->all_data_recved == 1);

    /* FIXME: this function is same with PktHandler_FOP(), should
     * do code refactoring on both of them. */

    MPIR_Datatype_get_size_macro(fop_pkt->datatype, type_size);

    MPIR_Datatype_is_contig(fop_pkt->datatype, &is_contig);

    if (fop_pkt->type == MPIDI_CH3_PKT_FOP_IMMED) {
        MPIDI_Pkt_init(fop_resp_pkt, MPIDI_CH3_PKT_FOP_RESP_IMMED);
    }
    else {
        MPIDI_Pkt_init(fop_resp_pkt, MPIDI_CH3_PKT_FOP_RESP);
    }

    fop_resp_pkt->request_handle = fop_pkt->request_handle;
    fop_resp_pkt->target_rank = win_ptr->comm_ptr->rank;
    fop_resp_pkt->pkt_flags = MPIDI_CH3_PKT_FLAG_NONE;
    if (fop_pkt->pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_LOCK_SHARED ||
        fop_pkt->pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_LOCK_EXCLUSIVE)
        fop_resp_pkt->pkt_flags |= MPIDI_CH3_PKT_FLAG_RMA_LOCK_GRANTED;
    if ((fop_pkt->pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_FLUSH) ||
        (fop_pkt->pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_UNLOCK))
        fop_resp_pkt->pkt_flags |= MPIDI_CH3_PKT_FLAG_RMA_ACK;

    if (fop_pkt->type == MPIDI_CH3_PKT_FOP) {
        resp_req = MPIR_Request_create(MPIR_REQUEST_KIND__UNDEFINED);
        if (resp_req == NULL) {
            MPIR_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**nomemreq");
        }
        MPIR_Object_set_ref(resp_req, 1);

        resp_req->dev.OnDataAvail = MPIDI_CH3_ReqHandler_FOPSendComplete;
        resp_req->dev.OnFinal = MPIDI_CH3_ReqHandler_FOPSendComplete;

        resp_req->dev.target_win_handle = win_ptr->handle;
        resp_req->dev.pkt_flags = fop_pkt->pkt_flags;

        resp_req->dev.user_buf = (void *) MPL_malloc(type_size, MPL_MEM_BUFFER);

        /* here we increment the Active Target counter to guarantee the GET-like
         * operation are completed when counter reaches zero. */
        win_ptr->at_completion_counter++;
    }

    /* NOTE: 'copy data + ACC' needs to be atomic */

    if (win_ptr->shm_allocated == TRUE) {
        MPIDI_CH3I_SHM_MUTEX_LOCK(win_ptr);
    }

    /* Copy data from target window to temporary buffer / response packet header */

    if (fop_pkt->type == MPIDI_CH3_PKT_FOP_IMMED) {
        /* copy data to resp pkt header */
        void *src = fop_pkt->addr, *dest = (void *) &fop_resp_pkt->info.data;
        mpi_errno = immed_copy(src, dest, type_size);
        if (mpi_errno != MPI_SUCCESS) {
            if (win_ptr->shm_allocated == TRUE) {
                MPIDI_CH3I_SHM_MUTEX_UNLOCK(win_ptr);
            }
            MPIR_ERR_POP(mpi_errno);
        }
    }
    else if (is_contig) {
        MPIR_Memcpy(resp_req->dev.user_buf, fop_pkt->addr, type_size);
    }
    else {
        MPI_Aint actual_pack_bytes;
        MPIR_Typerep_pack(fop_pkt->addr, 1, fop_pkt->datatype, 0, resp_req->dev.user_buf,
                       type_size, &actual_pack_bytes, MPIR_TYPEREP_FLAG_NONE);
        MPIR_Assert(actual_pack_bytes == type_size);
    }

    /* Apply the op */
    if (fop_pkt->type == MPIDI_CH3_PKT_FOP_IMMED) {
        mpi_errno = do_accumulate_op((void *) &fop_pkt->info.data, 1, fop_pkt->datatype,
                                     fop_pkt->addr, 1, fop_pkt->datatype, 0, fop_pkt->op,
                                     MPIDI_RMA_ACC_SRCBUF_DEFAULT);
    }
    else {
        mpi_errno = do_accumulate_op(target_lock_entry->data, 1, fop_pkt->datatype,
                                     fop_pkt->addr, 1, fop_pkt->datatype, 0, fop_pkt->op,
                                     MPIDI_RMA_ACC_SRCBUF_DEFAULT);
    }

    if (win_ptr->shm_allocated == TRUE) {
        MPIDI_CH3I_SHM_MUTEX_UNLOCK(win_ptr);
    }

    MPIR_ERR_CHECK(mpi_errno);

    if (fop_pkt->type == MPIDI_CH3_PKT_FOP_IMMED) {
        /* send back the original data */
        mpi_errno =
            MPIDI_CH3_iStartMsg(target_lock_entry->vc, fop_resp_pkt, sizeof(*fop_resp_pkt),
                                &resp_req);
        MPIR_ERR_CHKANDJUMP(mpi_errno != MPI_SUCCESS, mpi_errno, MPI_ERR_OTHER, "**ch3|rmamsg");

        if (resp_req != NULL) {
            if (!MPIR_Request_is_complete(resp_req)) {
                /* sending process is not completed, set proper OnDataAvail
                 * (it is initialized to NULL by lower layer) */
                resp_req->dev.target_win_handle = fop_pkt->target_win_handle;
                resp_req->dev.pkt_flags = fop_pkt->pkt_flags;
                resp_req->dev.OnDataAvail = MPIDI_CH3_ReqHandler_FOPSendComplete;

                /* here we increment the Active Target counter to guarantee the GET-like
                 * operation are completed when counter reaches zero. */
                win_ptr->at_completion_counter++;

                MPIR_Request_free(resp_req);
                goto fn_exit;
            }
            else {
                MPIR_Request_free(resp_req);
            }
        }
    }
    else {
        iov[0].iov_base = (void *) fop_resp_pkt;
        iov[0].iov_len = sizeof(*fop_resp_pkt);
        iov[1].iov_base = (void *) ((char *) resp_req->dev.user_buf);
        iov[1].iov_len = type_size;
        iovcnt = 2;

        mpi_errno = MPIDI_CH3_iSendv(target_lock_entry->vc, resp_req, iov, iovcnt);
        if (mpi_errno != MPI_SUCCESS) {
            MPIR_Request_free(resp_req);
            MPIR_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**ch3|rmamsg");
        }
        goto fn_exit;
    }

    /* do final action */
    mpi_errno = finish_op_on_target(win_ptr, target_lock_entry->vc, TRUE /* has response data */ ,
                                    fop_pkt->pkt_flags, MPI_WIN_NULL);
    MPIR_ERR_CHECK(mpi_errno);

  fn_exit:
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}


static inline int perform_cas_in_lock_queue(MPIR_Win * win_ptr,
                                            MPIDI_RMA_Target_lock_entry_t * target_lock_entry)
{
    MPIDI_CH3_Pkt_t upkt;
    MPIDI_CH3_Pkt_cas_resp_t *cas_resp_pkt = &upkt.cas_resp;
    MPIDI_CH3_Pkt_cas_t *cas_pkt = &((target_lock_entry->pkt).cas);
    MPIR_Request *send_req = NULL;
    MPI_Aint len;
    int mpi_errno = MPI_SUCCESS;

    /* Piggyback candidate should have basic datatype for target datatype. */
    MPIR_Assert(MPIR_DATATYPE_IS_PREDEFINED(cas_pkt->datatype));

    /* Make sure that all data is received for this op. */
    MPIR_Assert(target_lock_entry->all_data_recved == 1);

    MPIDI_Pkt_init(cas_resp_pkt, MPIDI_CH3_PKT_CAS_RESP_IMMED);
    cas_resp_pkt->request_handle = cas_pkt->request_handle;
    cas_resp_pkt->target_rank = win_ptr->comm_ptr->rank;
    cas_resp_pkt->pkt_flags = MPIDI_CH3_PKT_FLAG_NONE;
    if (cas_pkt->pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_LOCK_SHARED ||
        cas_pkt->pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_LOCK_EXCLUSIVE)
        cas_resp_pkt->pkt_flags |= MPIDI_CH3_PKT_FLAG_RMA_LOCK_GRANTED;
    if ((cas_pkt->pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_FLUSH) ||
        (cas_pkt->pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_UNLOCK))
        cas_resp_pkt->pkt_flags |= MPIDI_CH3_PKT_FLAG_RMA_ACK;

    /* Copy old value into the response packet */
    MPIR_Datatype_get_size_macro(cas_pkt->datatype, len);
    MPIR_Assert(len <= sizeof(MPIDI_CH3_CAS_Immed_u));

    if (win_ptr->shm_allocated == TRUE) {
        MPIDI_CH3I_SHM_MUTEX_LOCK(win_ptr);
    }

    MPIR_Memcpy((void *) &cas_resp_pkt->info.data, cas_pkt->addr, len);

    /* Compare and replace if equal */
    if (MPIR_Compare_equal(&cas_pkt->compare_data, cas_pkt->addr, cas_pkt->datatype)) {
        MPIR_Memcpy(cas_pkt->addr, &cas_pkt->origin_data, len);
    }

    if (win_ptr->shm_allocated == TRUE) {
        MPIDI_CH3I_SHM_MUTEX_UNLOCK(win_ptr);
    }

    /* Send the response packet */
    mpi_errno =
        MPIDI_CH3_iStartMsg(target_lock_entry->vc, cas_resp_pkt, sizeof(*cas_resp_pkt), &send_req);

    MPIR_ERR_CHKANDJUMP(mpi_errno != MPI_SUCCESS, mpi_errno, MPI_ERR_OTHER, "**ch3|rmamsg");

    if (send_req != NULL) {
        if (!MPIR_Request_is_complete(send_req)) {
            /* sending process is not completed, set proper OnDataAvail
             * (it is initialized to NULL by lower layer) */
            send_req->dev.target_win_handle = cas_pkt->target_win_handle;
            send_req->dev.pkt_flags = cas_pkt->pkt_flags;
            send_req->dev.OnDataAvail = MPIDI_CH3_ReqHandler_CASSendComplete;

            /* here we increment the Active Target counter to guarantee the GET-like
             * operation are completed when counter reaches zero. */
            win_ptr->at_completion_counter++;

            MPIR_Request_free(send_req);
            goto fn_exit;
        }
        else
            MPIR_Request_free(send_req);
    }

    /* do final action */
    mpi_errno = finish_op_on_target(win_ptr, target_lock_entry->vc, TRUE /* has response data */ ,
                                    cas_pkt->pkt_flags, MPI_WIN_NULL);
    MPIR_ERR_CHECK(mpi_errno);

  fn_exit:
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}


static inline int perform_op_in_lock_queue(MPIR_Win * win_ptr,
                                           MPIDI_RMA_Target_lock_entry_t * target_lock_entry)
{
    int mpi_errno = MPI_SUCCESS;

    if (target_lock_entry->pkt.type == MPIDI_CH3_PKT_LOCK) {

        /* single LOCK request */

        MPIDI_CH3_Pkt_lock_t *lock_pkt = &(target_lock_entry->pkt.lock);
        MPIDI_VC_t *my_vc = NULL;

        MPIDI_Comm_get_vc_set_active(win_ptr->comm_ptr, win_ptr->comm_ptr->rank, &my_vc);

        if (target_lock_entry->vc == my_vc) {
            mpi_errno = handle_lock_ack(win_ptr, win_ptr->comm_ptr->rank,
                                        MPIDI_CH3_PKT_FLAG_RMA_LOCK_GRANTED);
            MPIR_ERR_CHECK(mpi_errno);
        }
        else {
            mpi_errno = MPIDI_CH3I_Send_lock_ack_pkt(target_lock_entry->vc, win_ptr,
                                                     MPIDI_CH3_PKT_FLAG_RMA_LOCK_GRANTED,
                                                     lock_pkt->source_win_handle,
                                                     lock_pkt->request_handle);
            MPIR_ERR_CHECK(mpi_errno);
        }
    }
    else {
        /* LOCK+OP packet */
        switch (target_lock_entry->pkt.type) {
        case (MPIDI_CH3_PKT_PUT):
        case (MPIDI_CH3_PKT_PUT_IMMED):
            mpi_errno = perform_put_in_lock_queue(win_ptr, target_lock_entry);
            MPIR_ERR_CHECK(mpi_errno);
            break;
        case (MPIDI_CH3_PKT_GET):
            mpi_errno = perform_get_in_lock_queue(win_ptr, target_lock_entry);
            MPIR_ERR_CHECK(mpi_errno);
            break;
        case (MPIDI_CH3_PKT_ACCUMULATE):
        case (MPIDI_CH3_PKT_ACCUMULATE_IMMED):
            mpi_errno = perform_acc_in_lock_queue(win_ptr, target_lock_entry);
            MPIR_ERR_CHECK(mpi_errno);
            break;
        case (MPIDI_CH3_PKT_GET_ACCUM):
        case (MPIDI_CH3_PKT_GET_ACCUM_IMMED):
            mpi_errno = perform_get_acc_in_lock_queue(win_ptr, target_lock_entry);
            MPIR_ERR_CHECK(mpi_errno);
            break;
        case (MPIDI_CH3_PKT_FOP):
        case (MPIDI_CH3_PKT_FOP_IMMED):
            mpi_errno = perform_fop_in_lock_queue(win_ptr, target_lock_entry);
            MPIR_ERR_CHECK(mpi_errno);
            break;
        case (MPIDI_CH3_PKT_CAS_IMMED):
            mpi_errno = perform_cas_in_lock_queue(win_ptr, target_lock_entry);
            MPIR_ERR_CHECK(mpi_errno);
            break;
        default:
            MPIR_ERR_SETANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                                 "**invalidpkt", "**invalidpkt %d", target_lock_entry->pkt.type);
        }
    }

  fn_exit:
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}


static int entered_flag = 0;
static int entered_count = 0;

/* Release the current lock on the window and grant the next lock in the
   queue if any */
int MPIDI_CH3I_Release_lock(MPIR_Win * win_ptr)
{
    MPIDI_RMA_Target_lock_entry_t *target_lock_entry, *target_lock_entry_next;
    int requested_lock, mpi_errno = MPI_SUCCESS, temp_entered_count;

    MPIR_FUNC_ENTER;

    if (win_ptr->current_lock_type == MPI_LOCK_SHARED) {
        /* decr ref cnt */
        /* FIXME: MT: Must be done atomically */
        win_ptr->shared_lock_ref_cnt--;
    }

    /* If shared lock ref count is 0 (which is also true if the lock is an
     * exclusive lock), release the lock. */
    if (win_ptr->shared_lock_ref_cnt == 0) {

        /* This function needs to be reentrant even in the single-threaded case
         * because when going through the lock queue, pkt_handler() in
         * perform_op_in_lock_queue() may again call release_lock(). To handle
         * this possibility, we use an entered_flag.
         * If the flag is not 0, we simply increment the entered_count and return.
         * The loop through the lock queue is repeated if the entered_count has
         * changed while we are in the loop.
         */
        if (entered_flag != 0) {
            entered_count++;    /* Count how many times we re-enter */
            goto fn_exit;
        }

        entered_flag = 1;       /* Mark that we are now entering release_lock() */
        temp_entered_count = entered_count;

        do {
            if (temp_entered_count != entered_count)
                temp_entered_count++;

            /* FIXME: MT: The setting of the lock type must be done atomically */
            win_ptr->current_lock_type = MPID_LOCK_NONE;

            /* If there is a lock queue, try to satisfy as many lock requests as
             * possible. If the first one is a shared lock, grant it and grant all
             * other shared locks. If the first one is an exclusive lock, grant
             * only that one. */

            /* FIXME: MT: All queue accesses need to be made atomic */
            target_lock_entry = (MPIDI_RMA_Target_lock_entry_t *) win_ptr->target_lock_queue_head;
            while (target_lock_entry) {
                target_lock_entry_next = target_lock_entry->next;

                if (target_lock_entry->all_data_recved) {
                    int pkt_flags;
                    MPIDI_CH3_PKT_RMA_GET_FLAGS(target_lock_entry->pkt, pkt_flags, mpi_errno);
                    if (pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_LOCK_SHARED)
                        requested_lock = MPI_LOCK_SHARED;
                    else {
                        MPIR_Assert(pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_LOCK_EXCLUSIVE);
                        requested_lock = MPI_LOCK_EXCLUSIVE;
                    }
                    if (MPIDI_CH3I_Try_acquire_win_lock(win_ptr, requested_lock) == 1) {
                        /* dequeue entry from lock queue */
                        DL_DELETE(win_ptr->target_lock_queue_head, target_lock_entry);

                        /* perform this OP */
                        mpi_errno = perform_op_in_lock_queue(win_ptr, target_lock_entry);
                        MPIR_ERR_CHECK(mpi_errno);

                        /* free this entry */
                        mpi_errno =
                            MPIDI_CH3I_Win_target_lock_entry_free(win_ptr, target_lock_entry);
                        MPIR_ERR_CHECK(mpi_errno);

                        /* if the granted lock is exclusive,
                         * no need to continue */
                        if (requested_lock == MPI_LOCK_EXCLUSIVE)
                            break;
                    }
                }
                target_lock_entry = target_lock_entry_next;
            }
        } while (temp_entered_count != entered_count);

        entered_count = entered_flag = 0;
    }

  fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}



int MPIDI_CH3_ReqHandler_PiggybackLockOpRecvComplete(MPIDI_VC_t * vc,
                                                     MPIR_Request * rreq, int *complete)
{
    int requested_lock;
    MPI_Win target_win_handle;
    MPIR_Win *win_ptr = NULL;
    int pkt_flags;
    MPIDI_RMA_Target_lock_entry_t *target_lock_queue_entry = rreq->dev.target_lock_queue_entry;
    int mpi_errno = MPI_SUCCESS;

    MPIR_FUNC_ENTER;

    /* This handler is triggered when we received all data of a lock queue
     * entry */

    /* Note that if we decided to drop op data, here we just need to complete this
     * request; otherwise we try to get the lock again in this handler. */
    if (rreq->dev.target_lock_queue_entry != NULL) {

        /* Mark all data received in lock queue entry */
        target_lock_queue_entry->all_data_recved = 1;

        /* try to acquire the lock here */
        MPIDI_CH3_PKT_RMA_GET_FLAGS(target_lock_queue_entry->pkt, pkt_flags, mpi_errno);
        MPIDI_CH3_PKT_RMA_GET_TARGET_WIN_HANDLE(target_lock_queue_entry->pkt, target_win_handle,
                                                mpi_errno);
        MPIR_Win_get_ptr(target_win_handle, win_ptr);

        if (pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_STREAM &&
            (rreq->dev.target_lock_queue_entry)->data != NULL) {

            MPIR_Assert(target_lock_queue_entry->pkt.type == MPIDI_CH3_PKT_ACCUMULATE ||
                        target_lock_queue_entry->pkt.type == MPIDI_CH3_PKT_GET_ACCUM);

            int ext_hdr_sz = sizeof(MPIDI_CH3_Ext_pkt_stream_t);

            /* here we drop the stream_offset received, because the stream unit that piggybacked with
             * LOCK must be the first stream unit, with stream_offset equals to 0. */
            rreq->dev.recv_data_sz -= ext_hdr_sz;
            memmove((rreq->dev.target_lock_queue_entry)->data,
                    (void *) ((char *) ((rreq->dev.target_lock_queue_entry)->data) + ext_hdr_sz),
                    rreq->dev.recv_data_sz);
        }

        if (pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_LOCK_SHARED) {
            requested_lock = MPI_LOCK_SHARED;
        }
        else {
            MPIR_Assert(pkt_flags & MPIDI_CH3_PKT_FLAG_RMA_LOCK_EXCLUSIVE);
            requested_lock = MPI_LOCK_EXCLUSIVE;
        }

        if (MPIDI_CH3I_Try_acquire_win_lock(win_ptr, requested_lock) == 1) {
            /* dequeue entry from lock queue */
            DL_DELETE(win_ptr->target_lock_queue_head, target_lock_queue_entry);

            /* perform this OP */
            mpi_errno = perform_op_in_lock_queue(win_ptr, target_lock_queue_entry);
            MPIR_ERR_CHECK(mpi_errno);

            /* free this entry */
            mpi_errno = MPIDI_CH3I_Win_target_lock_entry_free(win_ptr, target_lock_queue_entry);
            MPIR_ERR_CHECK(mpi_errno);
        }
        /* If try acquiring lock failed, just leave the lock queue entry in the queue with
         * all_data_recved marked as 1, release_lock() function will traverse the queue
         * and find entry with all_data_recved being 1 to grant the lock. */
    }

    /* mark receive data transfer as complete and decrement CC in receive
     * request */
    mpi_errno = MPID_Request_complete(rreq);
    MPIR_ERR_CHECK(mpi_errno);

    *complete = TRUE;

  fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}
