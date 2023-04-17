/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpidimpl.h"
#include "ucx_impl.h"
#include "ucx_noinline.h"

static void noncontig_put_cb(void *request, ucs_status_t status, void *user_data)
{
    MPIR_FUNC_ENTER;

    MPL_free(user_data);
    ucp_request_free(request);

    MPIR_FUNC_EXIT;
}

MPL_STATIC_INLINE_PREFIX int MPIDI_UCX_noncontig_put(const void *origin_addr,
                                                     MPI_Aint origin_count,
                                                     MPI_Datatype origin_datatype, int target_rank,
                                                     size_t size, MPI_Aint target_disp,
                                                     MPI_Aint true_lb, MPIR_Win * win,
                                                     MPIDI_av_entry_t * addr,
                                                     MPIR_Request ** reqptr ATTRIBUTE((unused)),
                                                     int vci, int vci_target)
{
    MPIDI_UCX_win_info_t *win_info = &(MPIDI_UCX_WIN_INFO(win, target_rank));
    size_t base, offset;
    int mpi_errno = MPI_SUCCESS;
    ucs_status_ptr_t status;
    char *buffer = NULL;
    ucp_ep_h ep = MPIDI_UCX_WIN_AV_TO_EP(addr, vci, vci_target);

    buffer = MPL_malloc(size, MPL_MEM_BUFFER);
    MPIR_Assert(buffer);

    MPI_Aint actual_pack_bytes;
    mpi_errno = MPIR_Typerep_pack(origin_addr, origin_count, origin_datatype, 0, buffer, size,
                                  &actual_pack_bytes, MPIR_TYPEREP_FLAG_NONE);
    MPIR_ERR_CHECK(mpi_errno);
    MPIR_Assert(actual_pack_bytes == size);

    base = win_info->addr;
    offset = target_disp * win_info->disp + true_lb;

    ucp_request_param_t param = {
        .op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_USER_DATA,
        .cb.send = noncontig_put_cb,
        .user_data = buffer,
    }

    status = ucp_put_nbx(ep, buffer, size, base + offset, win_info->rkey, &param);
    if (status == UCS_OK) {
        MPIDI_UCX_WIN(win).target_sync[target_rank].need_sync = MPIDI_UCX_WIN_SYNC_FLUSH;
    } else {
        MPIDI_UCX_CHK_REQUEST(status);
        MPIDI_UCX_WIN(win).target_sync[target_rank].need_sync = MPIDI_UCX_WIN_SYNC_FLUSH_LOCAL;
    }

  fn_exit:
    MPL_free(buffer);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}
