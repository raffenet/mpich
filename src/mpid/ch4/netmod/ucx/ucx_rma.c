/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpidimpl.h"
#include "ucx_impl.h"
#include "ucx_noinline.h"

#define MPIR_CVAR_CH4_UCX_RMA_IOVEC_MAX (16 * 1024)
#define MPIDI_UCX_DEFAULT_SHORT_SEND_SIZE (16 * 1024)

static void nopack_putget_cb(void *request, ucs_status_t status, void *user_data)
{
    MPIR_FUNC_ENTER;

    MPIDI_Request_complete_fast(user_data);

    MPIR_FUNC_EXIT;
}

static void MPIDI_UCX_load_iov(const void *buffer, int count,
                               MPI_Datatype datatype, MPI_Aint max_len,
                               MPI_Aint * loaded_iov_offset, struct iovec *iov)
{
    MPI_Aint outlen;
    MPIR_Typerep_to_iov_offset(buffer, count, datatype, *loaded_iov_offset, iov, max_len, &outlen);
    *loaded_iov_offset += outlen;
}

int MPIDI_UCX_nopack_putget(const void *origin_addr, MPI_Aint origin_count,
                            MPI_Datatype origin_datatype, int target_rank, MPI_Aint target_disp,
                            MPI_Aint target_count, MPI_Datatype target_datatype,
                            MPIR_Win * win,
                            MPIDI_av_entry_t * addr, int rma_type, MPIR_Request ** sigreq)
{
    int mpi_errno = MPI_SUCCESS;
    int vci = MPIDI_WIN(win, am_vci);
    int vci_target = MPIDI_WIN_TARGET_VCI(win, target_rank);
    MPIDI_UCX_win_info_t *win_info = &(MPIDI_UCX_WIN_INFO(win, target_rank));
    MPI_Aint target_addr = win_info->addr + (target_disp * win_info->disp);
    MPIDI_UCX_ucp_request_t *ucp_request;

    /* allocate target iovecs */
    struct iovec *target_iov;
    MPI_Aint total_target_iov_len;
    MPI_Aint target_len;
    MPI_Aint target_iov_offset = 0;
    MPIR_Typerep_get_iov_len(target_count, target_datatype, &total_target_iov_len);
    target_len = MPL_MIN(total_target_iov_len, MPIR_CVAR_CH4_UCX_RMA_IOVEC_MAX);
    target_iov = MPL_malloc(sizeof(struct iovec) * target_len, MPL_MEM_RMA);

    /* allocate origin iovecs */
    struct iovec *origin_iov;
    MPI_Aint total_origin_iov_len;
    MPI_Aint origin_len;
    MPI_Aint origin_iov_offset = 0;
    MPIR_Typerep_get_iov_len(origin_count, origin_datatype, &total_origin_iov_len);
    origin_len = MPL_MIN(total_origin_iov_len, MPIR_CVAR_CH4_UCX_RMA_IOVEC_MAX);
    origin_iov = MPL_malloc(sizeof(struct iovec) * origin_len, MPL_MEM_RMA);

    MPIDI_UCX_WIN(win).target_sync[target_rank].need_sync = MPIDI_UCX_WIN_SYNC_FLUSH;
    ucp_request_param_t param = { 0 };
    ucp_ep_h ep = MPIDI_UCX_WIN_AV_TO_EP(addr, vci, vci_target);
    ucp_rkey_h rkey = win_info->rkey;
    if (sigreq) {
        *sigreq = MPIR_Request_create_from_pool(MPIR_REQUEST_KIND__RMA, 0, 2);
        MPIR_ERR_CHKANDSTMT(*sigreq == NULL, mpi_errno, MPIX_ERR_NOREQ, goto fn_fail, "**nomemreq");
        param.op_attr_mask |= UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_USER_DATA;
        param.cb.send = nopack_putget_cb;
        param.user_data = *sigreq;
    }

    int i = 0, j = 0;
    size_t msg_len;
    while (i < total_origin_iov_len && j < total_target_iov_len) {
        MPI_Aint origin_cur = i % origin_len;
        MPI_Aint target_cur = j % target_len;
        if (i == origin_iov_offset)
            MPIDI_UCX_load_iov(origin_addr, origin_count, origin_datatype, origin_len,
                               &origin_iov_offset, origin_iov);
        if (j == target_iov_offset)
            MPIDI_UCX_load_iov((const void *) (uintptr_t) target_addr, target_count,
                               target_datatype, target_len, &target_iov_offset, target_iov);

        msg_len = MPL_MIN(origin_iov[origin_cur].iov_len, target_iov[target_cur].iov_len);

        if (rma_type == MPIDI_UCX_PUT) {
            ucp_request =
                ucp_put_nbx(ep, origin_iov[origin_cur].iov_base, msg_len,
                            (uint64_t) target_iov[target_cur].iov_base, rkey, &param);
        } else if (rma_type == MPIDI_UCX_GET) {
            ucp_request = ucp_get_nbx(ep, origin_iov[origin_cur].iov_base, msg_len, (uint64_t) target_iov[target_cur].iov_base, rkey, &param);
        } else {
            MPIR_Assert(0);
        }
        if (ucp_request != UCS_OK) {
            ucp_request_release(ucp_request);
            MPIDI_UCX_WIN(win).target_sync[target_rank].need_sync = MPIDI_UCX_WIN_SYNC_FLUSH_LOCAL;
            if (sigreq) {
                MPIR_cc_inc((*sigreq)->cc_ptr);
            }
        }

        if (msg_len < origin_iov[origin_cur].iov_len) {
            origin_iov[origin_cur].iov_base = (char *) origin_iov[origin_cur].iov_base + msg_len;
            origin_iov[origin_cur].iov_len -= msg_len;
        } else {
            i++;
        }

        if (msg_len < target_iov[target_cur].iov_len) {
            target_iov[target_cur].iov_base = (char *) target_iov[target_cur].iov_base + msg_len;
            target_iov[target_cur].iov_len -= msg_len;
        } else {
            j++;
        }
    }
    MPIR_Assert(i == total_origin_iov_len);
    MPIR_Assert(j == total_target_iov_len);
    MPL_free(origin_iov);
    MPL_free(target_iov);

  fn_exit:
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}
