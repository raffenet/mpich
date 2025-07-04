/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpidimpl.h"
#include "mpir_info.h"
#include "mpidrma.h"

extern MPIR_T_pvar_timer_t PVAR_TIMER_rma_wincreate_allgather ATTRIBUTE((unused));

int MPIDI_Win_fns_init(MPIDI_CH3U_Win_fns_t * win_fns)
{
    int mpi_errno = MPI_SUCCESS;

    MPIR_FUNC_ENTER;

    win_fns->create = MPIDI_CH3U_Win_create;
    win_fns->allocate = MPIDI_CH3U_Win_allocate;
    win_fns->allocate_shared = MPIDI_CH3U_Win_allocate_shared;
    win_fns->create_dynamic = MPIDI_CH3U_Win_create_dynamic;
    win_fns->gather_info = MPIDI_CH3U_Win_gather_info;
    win_fns->shared_query = MPIDI_CH3U_Win_shared_query;

    MPIR_FUNC_EXIT;

    return mpi_errno;
}


int MPIDI_CH3U_Win_gather_info(void *base, MPI_Aint size, int disp_unit,
                               MPIR_Info * info, MPIR_Comm * comm_ptr, MPIR_Win ** win_ptr)
{
    int mpi_errno = MPI_SUCCESS, i, k, comm_size, rank;
    MPI_Aint *tmp_buf;
    MPIR_CHKPMEM_DECL();
    MPIR_CHKLMEM_DECL();

    MPIR_FUNC_ENTER;

    comm_size = (*win_ptr)->comm_ptr->local_size;
    rank = (*win_ptr)->comm_ptr->rank;

    MPIR_T_PVAR_TIMER_START(RMA, rma_wincreate_allgather);
    /* allocate memory for the base addresses, disp_units, and
     * completion counters of all processes */
    MPIR_CHKPMEM_MALLOC((*win_ptr)->basic_info_table,
                        comm_size * sizeof(MPIDI_Win_basic_info_t),
                        MPL_MEM_RMA);

    /* get the addresses of the windows, window objects, and completion
     * counters of all processes.  allocate temp. buffer for communication */
    MPIR_CHKLMEM_MALLOC(tmp_buf, 4 * comm_size * sizeof(MPI_Aint));

    /* FIXME: If we wanted to validate the transfer as within range at the
     * origin, we'd also need the window size. */
    tmp_buf[4 * rank] = MPIR_Ptr_to_aint(base);
    tmp_buf[4 * rank + 1] = size;
    tmp_buf[4 * rank + 2] = (MPI_Aint) disp_unit;
    tmp_buf[4 * rank + 3] = (MPI_Aint) (*win_ptr)->handle;

    mpi_errno = MPIR_Allgather(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL,
                               tmp_buf, 4, MPIR_AINT_INTERNAL, (*win_ptr)->comm_ptr, 0);
    MPIR_T_PVAR_TIMER_END(RMA, rma_wincreate_allgather);
    MPIR_ERR_CHECK(mpi_errno);

    k = 0;
    for (i = 0; i < comm_size; i++) {
        (*win_ptr)->basic_info_table[i].base_addr = MPIR_Aint_to_ptr(tmp_buf[k++]);
        (*win_ptr)->basic_info_table[i].size = tmp_buf[k++];
        (*win_ptr)->basic_info_table[i].disp_unit = (int) tmp_buf[k++];
        (*win_ptr)->basic_info_table[i].win_handle = (MPI_Win) tmp_buf[k++];
    }

  fn_exit:
    MPIR_CHKLMEM_FREEALL();
    MPIR_FUNC_EXIT;
    return mpi_errno;
    /* --BEGIN ERROR HANDLING-- */
  fn_fail:
    MPIR_CHKPMEM_REAP();
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}


int MPIDI_CH3U_Win_create(void *base, MPI_Aint size, int disp_unit, MPIR_Info * info,
                          MPIR_Comm * comm_ptr, MPIR_Win ** win_ptr)
{
    int mpi_errno = MPI_SUCCESS;

    MPIR_FUNC_ENTER;

    mpi_errno = MPIDI_CH3U_Win_fns.gather_info(base, size, disp_unit, info, comm_ptr, win_ptr);
    MPIR_ERR_CHECK(mpi_errno);

    if ((*win_ptr)->info_args.alloc_shm == TRUE && MPIDI_CH3U_Win_fns.detect_shm != NULL) {
        /* Detect if shared buffers are specified for the processes in the
         * current node. If so, enable shm RMA.*/
        mpi_errno = MPIDI_CH3U_Win_fns.detect_shm(win_ptr);
        MPIR_ERR_CHECK(mpi_errno);
        goto fn_exit;
    }

  fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;
    /* --BEGIN ERROR HANDLING-- */
  fn_fail:
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}


int MPIDI_CH3U_Win_create_dynamic(MPIR_Info * info, MPIR_Comm * comm_ptr, MPIR_Win ** win_ptr)
{
    int mpi_errno = MPI_SUCCESS;

    MPIR_FUNC_ENTER;

    mpi_errno = MPIDI_CH3U_Win_fns.gather_info(MPI_BOTTOM, 0, 1, info, comm_ptr, win_ptr);
    MPIR_ERR_CHECK(mpi_errno);

  fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;
    /* --BEGIN ERROR HANDLING-- */
  fn_fail:
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}

int MPID_Win_attach(MPIR_Win * win, void *base, MPI_Aint size)
{
    int mpi_errno = MPI_SUCCESS;

    MPIR_FUNC_ENTER;

    /* no op, all of memory is exposed */

    MPIR_FUNC_EXIT;
    return mpi_errno;
}


int MPID_Win_detach(MPIR_Win * win, const void *base)
{
    int mpi_errno = MPI_SUCCESS;

    MPIR_FUNC_ENTER;

    /* no op, all of memory is exposed */

    MPIR_FUNC_EXIT;
    return mpi_errno;
}


int MPIDI_CH3U_Win_allocate(MPI_Aint size, int disp_unit, MPIR_Info * info,
                            MPIR_Comm * comm_ptr, void *baseptr, MPIR_Win ** win_ptr)
{
    int mpi_errno = MPI_SUCCESS;

    MPIR_FUNC_ENTER;

    if ((*win_ptr)->info_args.alloc_shm == TRUE) {
        if (MPIDI_CH3U_Win_fns.allocate_shm != NULL) {
            mpi_errno = MPIDI_CH3U_Win_fns.allocate_shm(size, disp_unit, info, comm_ptr,
                                                        baseptr, win_ptr, 0);
            MPIR_ERR_CHECK(mpi_errno);
            goto fn_exit;
        }
    }

    mpi_errno = MPIDI_CH3U_Win_allocate_no_shm(size, disp_unit, info, comm_ptr, baseptr, win_ptr);
    MPIR_ERR_CHECK(mpi_errno);

  fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

int MPIDI_CH3U_Win_allocate_shared(MPI_Aint size, int disp_unit, MPIR_Info * info,
                                   MPIR_Comm * comm_ptr, void *baseptr, MPIR_Win ** win_ptr)
{
    int mpi_errno = MPI_SUCCESS;

    MPIR_FUNC_ENTER;

    if ((*win_ptr)->info_args.alloc_shm == TRUE) {
        if (MPIDI_CH3U_Win_fns.allocate_shm != NULL) {
            mpi_errno = MPIDI_CH3U_Win_fns.allocate_shm(size, disp_unit, info, comm_ptr,
                                                        baseptr, win_ptr, 1);
            MPIR_ERR_CHECK(mpi_errno);
            goto fn_exit;
        }
    }

    if (comm_ptr->local_size == 1) {
        mpi_errno = MPIDI_CH3U_Win_allocate_no_shm(size, disp_unit, info, comm_ptr, baseptr, win_ptr);
        MPIR_ERR_CHECK(mpi_errno);
    } else {
        MPIR_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**winallocnotshared");
    }

  fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}


int MPIDI_CH3U_Win_allocate_no_shm(MPI_Aint size, int disp_unit, MPIR_Info * info,
                                   MPIR_Comm * comm_ptr, void *baseptr, MPIR_Win ** win_ptr)
{
    void **base_pp = (void **) baseptr;
    int mpi_errno = MPI_SUCCESS;
    MPIR_CHKPMEM_DECL();

    MPIR_FUNC_ENTER;

    if (size > 0) {
        MPIR_CHKPMEM_MALLOC(*base_pp, size, MPL_MEM_RMA);
        MPL_VG_MEM_INIT(*base_pp, size);
    }
    else if (size == 0) {
        *base_pp = NULL;
    }
    else {
        MPIR_ERR_SETANDJUMP(mpi_errno, MPI_ERR_SIZE, "**rmasize");
    }

    (*win_ptr)->base = *base_pp;

    mpi_errno = MPIDI_CH3U_Win_fns.gather_info(*base_pp, size, disp_unit, info, comm_ptr, win_ptr);
    MPIR_ERR_CHECK(mpi_errno);

  fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;
    /* --BEGIN ERROR HANDLING-- */
  fn_fail:
    MPIR_CHKPMEM_REAP();
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}


int MPIDI_CH3U_Win_shared_query(MPIR_Win * win_ptr, int target_rank, MPI_Aint * size,
                                int *disp_unit, void *baseptr)
{
    int mpi_errno = MPI_SUCCESS;

    MPIR_FUNC_ENTER;

    /* We don't really support shared memory, only return local process' info
     * if the window size is 1 or it is querying its own rank */
    if (target_rank == MPI_PROC_NULL && win_ptr->comm_ptr->local_size == 1) {
        *(void **) baseptr = win_ptr->base;
        *size = win_ptr->size;
        *disp_unit = win_ptr->disp_unit;
    } else if (target_rank == win_ptr->comm_ptr->rank) {
        *(void **) baseptr = win_ptr->base;
        *size = win_ptr->size;
        *disp_unit = win_ptr->disp_unit;
    } else {
        *(void **) baseptr = NULL;
        *size = 0;
        *disp_unit = 0;
    }

    MPIR_FUNC_EXIT;
    return mpi_errno;
}


int MPID_Win_set_info(MPIR_Win * win, MPIR_Info * info)
{
    int mpi_errno = MPI_SUCCESS;

    MPIR_FUNC_ENTER;

    if (info != NULL) {
        int info_flag = 0;
        char info_value[MPI_MAX_INFO_VAL + 1];

        /********************************************************/
        /************** check for info no_locks *****************/
        /********************************************************/
        MPIR_Info_get_impl(info, "no_locks", MPI_MAX_INFO_VAL, info_value, &info_flag);
        if (info_flag) {
            if (!strncmp(info_value, "true", strlen("true")))
                win->info_args.no_locks = 1;
            if (!strncmp(info_value, "false", strlen("false")))
                win->info_args.no_locks = 0;
        }

        /********************************************************/
        /*************** check for info alloc_shm ***************/
        /********************************************************/
        info_flag = 0;
        MPIR_Info_get_impl(info, "alloc_shm", MPI_MAX_INFO_VAL, info_value, &info_flag);
        if (info_flag) {
            if (!strncmp(info_value, "true", sizeof("true")))
                win->info_args.alloc_shm = TRUE;
            if (!strncmp(info_value, "false", sizeof("false")))
                win->info_args.alloc_shm = FALSE;
        }

        if (win->create_flavor == MPI_WIN_FLAVOR_DYNAMIC)
            win->info_args.alloc_shm = FALSE;

        /********************************************************/
        /******* check for info alloc_shared_noncontig **********/
        /********************************************************/
        info_flag = 0;
        MPIR_Info_get_impl(info, "alloc_shared_noncontig", MPI_MAX_INFO_VAL,
                           info_value, &info_flag);
        if (info_flag) {
            if (!strncmp(info_value, "true", strlen("true")))
                win->info_args.alloc_shared_noncontig = 1;
            if (!strncmp(info_value, "false", strlen("false")))
                win->info_args.alloc_shared_noncontig = 0;
        }

        /********************************************************/
        /******* check for info accumulate_ordering    **********/
        /********************************************************/
        info_flag = 0;
        MPIR_Info_get_impl(info, "accumulate_ordering", MPI_MAX_INFO_VAL, info_value, &info_flag);
        if (info_flag) {
            if (!strncmp(info_value, "none", strlen("none"))) {
                win->info_args.accumulate_ordering = 0;
            }
            else {
                char *token, *save_ptr;
                int new_ordering = 0;

                token = (char *) strtok_r(info_value, ",", &save_ptr);
                while (token) {
                    if (!memcmp(token, "rar", 3))
                        new_ordering |= MPIDI_ACC_ORDER_RAR;
                    else if (!memcmp(token, "raw", 3))
                        new_ordering |= MPIDI_ACC_ORDER_RAW;
                    else if (!memcmp(token, "war", 3))
                        new_ordering |= MPIDI_ACC_ORDER_WAR;
                    else if (!memcmp(token, "waw", 3))
                        new_ordering |= MPIDI_ACC_ORDER_WAW;
                    else
                        MPIR_ERR_SETANDSTMT(mpi_errno, MPI_ERR_ARG, goto fn_fail, "**info");

                    token = (char *) strtok_r(NULL, ",", &save_ptr);
                }

                win->info_args.accumulate_ordering = new_ordering;
            }
        }

        /********************************************************/
        /******* check for info accumulate_ops         **********/
        /********************************************************/
        info_flag = 0;
        MPIR_Info_get_impl(info, "accumulate_ops", MPI_MAX_INFO_VAL, info_value, &info_flag);
        if (info_flag) {
            if (!strncmp(info_value, "same_op", sizeof("same_op")))
                win->info_args.accumulate_ops = MPIDI_ACC_OPS_SAME_OP;
            if (!strncmp(info_value, "same_op_no_op", sizeof("same_op_no_op")))
                win->info_args.accumulate_ops = MPIDI_ACC_OPS_SAME_OP_NO_OP;
        }

        /********************************************************/
        /******* check for info same_size              **********/
        /********************************************************/
        info_flag = 0;
        MPIR_Info_get_impl(info, "same_size", MPI_MAX_INFO_VAL, info_value, &info_flag);
        if (info_flag) {
            if (!strncmp(info_value, "true", sizeof("true")))
                win->info_args.same_size = TRUE;
            if (!strncmp(info_value, "false", sizeof("false")))
                win->info_args.same_size = FALSE;
        }

        /********************************************************/
        /******* check for info same_disp_unit         **********/
        /********************************************************/
        info_flag = 0;
        MPIR_Info_get_impl(info, "same_disp_unit", MPI_MAX_INFO_VAL, info_value, &info_flag);
        if (info_flag) {
            if (!strncmp(info_value, "true", sizeof("true")))
                win->info_args.same_disp_unit = TRUE;
            if (!strncmp(info_value, "false", sizeof("false")))
                win->info_args.same_disp_unit = FALSE;
        }

        /********************************************************/
        /*******  check for info mpi_accumualte_granularity *****/
        /********************************************************/
        info_flag = 0;
        MPIR_Info_get_impl(info, "mpi_acumulate_granularity", MPI_MAX_INFO_VAL, info_value, &info_flag);
        if (info_flag) {
            int value = atoi(info_value);
            if (value > 0) {
                win->info_args.accumulate_granularity = value;
            } else {
                win->info_args.accumulate_granularity = 0;
            }
        }
    }


  fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}


int MPID_Win_get_info(MPIR_Win * win, MPIR_Info ** info_used)
{
    int mpi_errno = MPI_SUCCESS;

    MPIR_FUNC_ENTER;

    /* Allocate an empty info object */
    mpi_errno = MPIR_Info_alloc(info_used);
    MPIR_ERR_CHECK(mpi_errno);

    /* Populate the predefined info keys */
    if (win->info_args.no_locks)
        mpi_errno = MPIR_Info_set_impl(*info_used, "no_locks", "true");
    else
        mpi_errno = MPIR_Info_set_impl(*info_used, "no_locks", "false");

    MPIR_ERR_CHECK(mpi_errno);

    {
#define BUFSIZE 32
        char buf[BUFSIZE];
        if (win->info_args.accumulate_ordering == 0) {
            strncpy(buf, "none", BUFSIZE);
        }
        else {
            int c = 0;
            if (win->info_args.accumulate_ordering & MPIDI_ACC_ORDER_RAR)
                c += snprintf(buf + c, BUFSIZE - c, "%srar", (c > 0) ? "," : "");
            if (win->info_args.accumulate_ordering & MPIDI_ACC_ORDER_RAW)
                c += snprintf(buf + c, BUFSIZE - c, "%sraw", (c > 0) ? "," : "");
            if (win->info_args.accumulate_ordering & MPIDI_ACC_ORDER_WAR)
                c += snprintf(buf + c, BUFSIZE - c, "%swar", (c > 0) ? "," : "");
            if (win->info_args.accumulate_ordering & MPIDI_ACC_ORDER_WAW)
                c += snprintf(buf + c, BUFSIZE - c, "%swaw", (c > 0) ? "," : "");
        }

        mpi_errno = MPIR_Info_set_impl(*info_used, "accumulate_ordering", buf);
        MPIR_ERR_CHECK(mpi_errno);
#undef BUFSIZE
    }

    if (win->info_args.accumulate_ops == MPIDI_ACC_OPS_SAME_OP)
        mpi_errno = MPIR_Info_set_impl(*info_used, "accumulate_ops", "same_op");
    else
        mpi_errno = MPIR_Info_set_impl(*info_used, "accumulate_ops", "same_op_no_op");

    MPIR_ERR_CHECK(mpi_errno);

    if (win->info_args.alloc_shm == TRUE)
        mpi_errno = MPIR_Info_set_impl(*info_used, "alloc_shm", "true");
    else
        mpi_errno = MPIR_Info_set_impl(*info_used, "alloc_shm", "false");

    MPIR_ERR_CHECK(mpi_errno);

    if (win->info_args.alloc_shared_noncontig)
        mpi_errno = MPIR_Info_set_impl(*info_used, "alloc_shared_noncontig", "true");
    else
        mpi_errno = MPIR_Info_set_impl(*info_used, "alloc_shared_noncontig", "false");

    MPIR_ERR_CHECK(mpi_errno);

    if (win->info_args.same_size)
        mpi_errno = MPIR_Info_set_impl(*info_used, "same_size", "true");
    else
        mpi_errno = MPIR_Info_set_impl(*info_used, "same_size", "false");

    MPIR_ERR_CHECK(mpi_errno);

    if (win->info_args.same_disp_unit)
        mpi_errno = MPIR_Info_set_impl(*info_used, "same_disp_unit", "true");
    else
        mpi_errno = MPIR_Info_set_impl(*info_used, "same_disp_unit", "false");
    MPIR_ERR_CHECK(mpi_errno);

    if (win->comm_ptr) {
        char *memory_alloc_kinds;
        MPIR_get_memory_kinds_from_comm(win->comm_ptr, &memory_alloc_kinds);
        mpi_errno = MPIR_Info_set_impl(*info_used, "mpi_memory_alloc_kinds", memory_alloc_kinds);
        MPIR_ERR_CHECK(mpi_errno);
    }

  fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}
