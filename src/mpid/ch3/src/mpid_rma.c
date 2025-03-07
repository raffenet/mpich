/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpidimpl.h"
#include "mpidrma.h"

/*
=== BEGIN_MPI_T_CVAR_INFO_BLOCK ===

cvars:
    - name        : MPIR_CVAR_CH3_RMA_SLOTS_SIZE
      category    : CH3
      type        : int
      default     : 262144
      class       : none
      verbosity   : MPI_T_VERBOSITY_USER_BASIC
      scope       : MPI_T_SCOPE_ALL_EQ
      description : >-
        Number of RMA slots during window creation. Each slot contains
        a linked list of target elements. The distribution of ranks among
        slots follows a round-robin pattern. Requires a positive value.

    - name        : MPIR_CVAR_CH3_RMA_TARGET_LOCK_DATA_BYTES
      category    : CH3
      type        : int
      default     : 655360
      class       : none
      verbosity   : MPI_T_VERBOSITY_USER_BASIC
      scope       : MPI_T_SCOPE_ALL_EQ
      description : >-
        Size (in bytes) of available lock data this window can provided. If
        current buffered lock data is more than this value, the process will
        drop the upcoming operation data. Requires a positive value.

=== END_MPI_T_CVAR_INFO_BLOCK ===
*/

static volatile int initRMAoptions = 1;

MPIR_Win *MPIDI_RMA_Win_active_list_head = NULL, *MPIDI_RMA_Win_inactive_list_head = NULL;

/* This variable keeps track of number of active RMA requests, i.e., total number of issued
 * but incomplete RMA operations. We use this variable to control the resources used up by
 * internal requests in RMA infrastructure. */
int MPIDI_CH3I_RMA_Active_req_cnt = 0;

/* This variable stores the index of RMA progress hook in progress hook array */
int MPIDI_CH3I_RMA_Progress_hook_id = 0;

static int win_init(MPI_Aint size, int disp_unit, int create_flavor, int model, MPIR_Info * info,
                    MPIR_Comm * comm_ptr, MPIR_Win ** win_ptr);


int MPID_Win_create(void *base, MPI_Aint size, MPI_Aint disp_unit, MPIR_Info * info,
                    MPIR_Comm * comm_ptr, MPIR_Win ** win_ptr)
{
    int mpi_errno = MPI_SUCCESS;


    MPIR_FUNC_ENTER;

    /* Check to make sure the communicator hasn't already been revoked */
    if (comm_ptr->revoked) {
        MPIR_ERR_SETANDJUMP(mpi_errno, MPIX_ERR_REVOKED, "**revoked");
    }

    MPIR_Assert(disp_unit <= INT_MAX);
    int my_disp_unit = (int) disp_unit;
    mpi_errno = win_init(size, my_disp_unit, MPI_WIN_FLAVOR_CREATE, MPI_WIN_UNIFIED,
                         info, comm_ptr, win_ptr);
    MPIR_ERR_CHECK(mpi_errno);

    (*win_ptr)->base = base;

    mpi_errno = MPIDI_CH3U_Win_fns.create(base, size, my_disp_unit, info, comm_ptr, win_ptr);
    MPIR_ERR_CHECK(mpi_errno);

  fn_fail:
    MPIR_FUNC_EXIT;
    return mpi_errno;
}


int MPID_Win_allocate(MPI_Aint size, MPI_Aint disp_unit, MPIR_Info * info,
                      MPIR_Comm * comm_ptr, void *baseptr, MPIR_Win ** win_ptr)
{
    int mpi_errno = MPI_SUCCESS;

    MPIR_FUNC_ENTER;

    MPIR_Assert(disp_unit <= INT_MAX);
    int my_disp_unit = (int) disp_unit;
    mpi_errno = win_init(size, my_disp_unit, MPI_WIN_FLAVOR_ALLOCATE, MPI_WIN_UNIFIED,
                         info, comm_ptr, win_ptr);
    MPIR_ERR_CHECK(mpi_errno);

    mpi_errno = MPIDI_CH3U_Win_fns.allocate(size, my_disp_unit, info, comm_ptr, baseptr, win_ptr);
    MPIR_ERR_CHECK(mpi_errno);

  fn_fail:
    MPIR_FUNC_EXIT;
    return mpi_errno;
}


int MPID_Win_create_dynamic(MPIR_Info * info, MPIR_Comm * comm_ptr, MPIR_Win ** win_ptr)
{
    int mpi_errno = MPI_SUCCESS;


    MPIR_FUNC_ENTER;

    mpi_errno = win_init(0 /* spec defines size to be 0 */ ,
                         1 /* spec defines disp_unit to be 1 */ ,
                         MPI_WIN_FLAVOR_DYNAMIC, MPI_WIN_UNIFIED, info, comm_ptr, win_ptr);

    MPIR_ERR_CHECK(mpi_errno);

    (*win_ptr)->base = MPI_BOTTOM;

    mpi_errno = MPIDI_CH3U_Win_fns.create_dynamic(info, comm_ptr, win_ptr);
    MPIR_ERR_CHECK(mpi_errno);

  fn_fail:
    MPIR_FUNC_EXIT;
    return mpi_errno;
}


/* The memory allocation functions */
void *MPID_Alloc_mem(MPI_Aint size, MPIR_Info * info_ptr)
{
    void *ap = NULL;

    MPIR_FUNC_ENTER;

    ap = MPIDI_CH3I_Alloc_mem(size, info_ptr);

    MPIR_FUNC_EXIT;
    return ap;
}


int MPID_Free_mem(void *ptr)
{
    int mpi_errno = MPI_SUCCESS;

    MPIR_FUNC_ENTER;

    MPIDI_CH3I_Free_mem(ptr);

    MPIR_FUNC_EXIT;
    return mpi_errno;
}


int MPID_Win_allocate_shared(MPI_Aint size, MPI_Aint disp_unit, MPIR_Info * info, MPIR_Comm * comm_ptr,
                             void *base_ptr, MPIR_Win ** win_ptr)
{
    int mpi_errno = MPI_SUCCESS;

    MPIR_FUNC_ENTER;

    MPIR_Assert(disp_unit <= INT_MAX);
    int my_disp_unit = (int) disp_unit;
    mpi_errno = win_init(size, my_disp_unit, MPI_WIN_FLAVOR_SHARED, MPI_WIN_UNIFIED,
                         info, comm_ptr, win_ptr);
    MPIR_ERR_CHECK(mpi_errno);

    mpi_errno =
        MPIDI_CH3U_Win_fns.allocate_shared(size, my_disp_unit, info, comm_ptr, base_ptr, win_ptr);
    MPIR_ERR_CHECK(mpi_errno);

  fn_fail:
    MPIR_FUNC_EXIT;
    return mpi_errno;
}

int MPID_Win_shared_query(MPIR_Win * win, int rank, MPI_Aint * size, int *disp_unit, void *baseptr)
{
    int mpi_errno = MPI_SUCCESS;


    MPIR_FUNC_ENTER;

    mpi_errno = MPIDI_CH3U_Win_fns.shared_query(win, rank, size, disp_unit, baseptr);
    MPIR_ERR_CHECK(mpi_errno);

  fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

static int win_init(MPI_Aint size, int disp_unit, int create_flavor, int model, MPIR_Info * info,
                    MPIR_Comm * comm_ptr, MPIR_Win ** win_ptr)
{
    int mpi_errno = MPI_SUCCESS;
    int i;
    MPIR_Comm *win_comm_ptr;
    int win_target_pool_size;
    MPIR_CHKPMEM_DECL();

    MPIR_FUNC_ENTER;

    if (initRMAoptions) {

        MPIDI_CH3_RMA_Init_sync_pvars();
        MPIDI_CH3_RMA_Init_pkthandler_pvars();

        initRMAoptions = 0;
    }

    *win_ptr = (MPIR_Win *) MPIR_Handle_obj_alloc(&MPIR_Win_mem);
    MPIR_ERR_CHKANDJUMP1(!(*win_ptr), mpi_errno, MPI_ERR_OTHER, "**nomem",
                         "**nomem %s", "MPIR_Win_mem");

    mpi_errno = MPIR_Comm_dup_impl(comm_ptr, &win_comm_ptr);
    MPIR_ERR_CHECK(mpi_errno);

    MPIR_Object_set_ref(*win_ptr, 1);
    {
        int thr_err;
        MPID_Thread_mutex_create(&(*win_ptr)->mutex, &thr_err);
        MPIR_Assert(thr_err == 0);
    }

    (*win_ptr)->errhandler = NULL;
    /* (*win_ptr)->base is set by caller; */
    (*win_ptr)->size = size;
    (*win_ptr)->disp_unit = disp_unit;
    (*win_ptr)->create_flavor = create_flavor;
    (*win_ptr)->model = model;
    (*win_ptr)->attributes = NULL;
    (*win_ptr)->comm_ptr = win_comm_ptr;

    (*win_ptr)->at_completion_counter = 0;
    (*win_ptr)->shm_base_addrs = NULL;
    /* (*win_ptr)->basic_info_table[] is set by caller; */
    (*win_ptr)->current_lock_type = MPID_LOCK_NONE;
    (*win_ptr)->shared_lock_ref_cnt = 0;
    (*win_ptr)->target_lock_queue_head = NULL;
    (*win_ptr)->shm_allocated = FALSE;
    (*win_ptr)->states.access_state = MPIDI_RMA_NONE;
    (*win_ptr)->states.exposure_state = MPIDI_RMA_NONE;
    (*win_ptr)->num_targets_with_pending_net_ops = 0;
    (*win_ptr)->start_ranks_in_win_grp = NULL;
    (*win_ptr)->start_grp_size = 0;
    (*win_ptr)->lock_all_assert = 0;
    (*win_ptr)->lock_epoch_count = 0;
    (*win_ptr)->outstanding_locks = 0;
    (*win_ptr)->current_target_lock_data_bytes = 0;
    (*win_ptr)->sync_request_cnt = 0;
    (*win_ptr)->active = FALSE;
    (*win_ptr)->next = NULL;
    (*win_ptr)->prev = NULL;
    (*win_ptr)->outstanding_acks = 0;

    /* Initialize the info flags */
    (*win_ptr)->info_args.no_locks = 0;
    (*win_ptr)->info_args.accumulate_ordering = MPIDI_ACC_ORDER_RAR | MPIDI_ACC_ORDER_RAW |
        MPIDI_ACC_ORDER_WAR | MPIDI_ACC_ORDER_WAW;
    (*win_ptr)->info_args.accumulate_ops = MPIDI_ACC_OPS_SAME_OP_NO_OP;
    (*win_ptr)->info_args.same_size = 0;
    (*win_ptr)->info_args.same_disp_unit = FALSE;
    (*win_ptr)->info_args.alloc_shared_noncontig = 0;
    (*win_ptr)->info_args.alloc_shm = FALSE;
    if ((*win_ptr)->create_flavor == MPI_WIN_FLAVOR_ALLOCATE ||
        (*win_ptr)->create_flavor == MPI_WIN_FLAVOR_SHARED) {
        (*win_ptr)->info_args.alloc_shm = TRUE;
    }
    (*win_ptr)->info_args.accumulate_granularity = 0;

    /* Set info_args on window based on info provided by user */
    mpi_errno = MPID_Win_set_info((*win_ptr), info);
    MPIR_ERR_CHECK(mpi_errno);

    MPIR_CHKPMEM_MALLOC((*win_ptr)->op_pool_start,
                        sizeof(MPIDI_RMA_Op_t) * MPIR_CVAR_CH3_RMA_OP_WIN_POOL_SIZE,
                        MPL_MEM_RMA);
    (*win_ptr)->op_pool_head = NULL;
    for (i = 0; i < MPIR_CVAR_CH3_RMA_OP_WIN_POOL_SIZE; i++) {
        (*win_ptr)->op_pool_start[i].pool_type = MPIDI_RMA_POOL_WIN;
        DL_APPEND((*win_ptr)->op_pool_head, &((*win_ptr)->op_pool_start[i]));
    }

    win_target_pool_size =
        MPL_MIN(MPIR_CVAR_CH3_RMA_TARGET_WIN_POOL_SIZE, MPIR_Comm_size(win_comm_ptr));
    MPIR_CHKPMEM_MALLOC((*win_ptr)->target_pool_start,
                        sizeof(MPIDI_RMA_Target_t) * win_target_pool_size,
                        MPL_MEM_RMA);
    (*win_ptr)->target_pool_head = NULL;
    for (i = 0; i < win_target_pool_size; i++) {
        (*win_ptr)->target_pool_start[i].pool_type = MPIDI_RMA_POOL_WIN;
        DL_APPEND((*win_ptr)->target_pool_head, &((*win_ptr)->target_pool_start[i]));
    }

    (*win_ptr)->num_slots = MPL_MIN(MPIR_CVAR_CH3_RMA_SLOTS_SIZE, MPIR_Comm_size(win_comm_ptr));
    MPIR_CHKPMEM_MALLOC((*win_ptr)->slots,
                        sizeof(MPIDI_RMA_Slot_t) * (*win_ptr)->num_slots,
                        MPL_MEM_RMA);
    for (i = 0; i < (*win_ptr)->num_slots; i++) {
        (*win_ptr)->slots[i].target_list_head = NULL;
    }

    MPIR_CHKPMEM_MALLOC((*win_ptr)->target_lock_entry_pool_start,
                        sizeof(MPIDI_RMA_Target_lock_entry_t) * MPIR_CVAR_CH3_RMA_TARGET_LOCK_ENTRY_WIN_POOL_SIZE,
                        MPL_MEM_RMA);
    (*win_ptr)->target_lock_entry_pool_head = NULL;
    for (i = 0; i < MPIR_CVAR_CH3_RMA_TARGET_LOCK_ENTRY_WIN_POOL_SIZE; i++) {
        DL_APPEND((*win_ptr)->target_lock_entry_pool_head,
                      &((*win_ptr)->target_lock_entry_pool_start[i]));
    }

    if (MPIDI_RMA_Win_inactive_list_head == NULL && MPIDI_RMA_Win_active_list_head == NULL) {
        /* this is the first window, register RMA progress hook */
        mpi_errno = MPIR_Progress_hook_register(-1, MPIDI_CH3I_RMA_Make_progress_global,
                                                &MPIDI_CH3I_RMA_Progress_hook_id);
        MPIR_ERR_CHECK(mpi_errno);
    }

    DL_APPEND(MPIDI_RMA_Win_inactive_list_head, (*win_ptr));

    if (MPIDI_CH3U_Win_hooks.win_init != NULL) {
        mpi_errno =
            MPIDI_CH3U_Win_hooks.win_init(size, disp_unit, create_flavor, model, info, comm_ptr,
                                          win_ptr);
        MPIR_ERR_CHECK(mpi_errno);
    }

  fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;
  fn_fail:
    MPIR_CHKPMEM_REAP();
    goto fn_exit;
}
