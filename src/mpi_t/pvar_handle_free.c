/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpiimpl.h"
#include "utlist.h"

/* -- Begin Profiling Symbol Block for routine MPI_T_pvar_handle_free */
#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_T_pvar_handle_free = PMPI_T_pvar_handle_free
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_T_pvar_handle_free  MPI_T_pvar_handle_free
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_T_pvar_handle_free as PMPI_T_pvar_handle_free
#elif defined(HAVE_WEAK_ATTRIBUTE)
int MPI_T_pvar_handle_free(MPI_T_pvar_session session, MPI_T_pvar_handle * handle)
    __attribute__ ((weak, alias("PMPI_T_pvar_handle_free")));
#endif
/* -- End Profiling Symbol Block */

/* Define MPICH_MPI_FROM_PMPI if weak symbols are not supported to build
   the MPI routines */
#ifndef MPICH_MPI_FROM_PMPI
#undef MPI_T_pvar_handle_free
#define MPI_T_pvar_handle_free PMPI_T_pvar_handle_free

/* any non-MPI functions go here, especially non-static ones */

int MPIR_T_pvar_handle_free_impl(MPI_T_pvar_session session, MPI_T_pvar_handle * handle)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_T_pvar_handle_t *hnd = *handle;

    DL_DELETE(session->hlist, hnd);

    /* Unlink handle from pvar if it is a watermark */
    if (MPIR_T_pvar_is_watermark(hnd)) {
        MPIR_T_pvar_watermark_t *mark = (MPIR_T_pvar_watermark_t *) hnd->addr;
        if (MPIR_T_pvar_is_first(hnd)) {
            mark->first_used = 0;
            mark->first_started = 0;
        } else {
            MPIT_Assert(mark->hlist);
            if (mark->hlist == hnd) {
                /* hnd happens to be the head */
                mark->hlist = hnd->next2;
                if (mark->hlist != NULL)
                    mark->hlist->prev2 = mark->hlist;
            } else {
                hnd->prev2->next2 = hnd->next2;
                if (hnd->next2 != NULL)
                    hnd->next2->prev2 = hnd->prev2;
            }
        }
    }

    MPL_free(hnd);
    *handle = MPI_T_PVAR_HANDLE_NULL;

    return mpi_errno;
}

#endif /* MPICH_MPI_FROM_PMPI */

/*@
MPI_T_pvar_handle_free - Free an existing handle for a performance variable

Input/Output Parameters:
+ session - identifier of performance experiment session (handle)
- handle - handle to be freed (handle)

.N ThreadSafe

.N Errors
.N MPI_SUCCESS
.N MPI_T_ERR_NOT_INITIALIZED
.N MPI_T_ERR_INVALID_SESSION
.N MPI_T_ERR_INVALID_HANDLE
@*/
int MPI_T_pvar_handle_free(MPI_T_pvar_session session, MPI_T_pvar_handle * handle)
{
    int mpi_errno = MPI_SUCCESS;

    MPIR_FUNC_TERSE_STATE_DECL(MPID_STATE_MPI_T_PVAR_HANDLE_FREE);
    MPIT_ERRTEST_MPIT_INITIALIZED();
    MPIR_T_THREAD_CS_ENTER();
    MPIR_FUNC_TERSE_ENTER(MPID_STATE_MPI_T_PVAR_HANDLE_FREE);

    /* Validate parameters, especially handles needing to be converted */
    MPIT_ERRTEST_ARGNULL(handle);
    if (*handle == MPI_T_PVAR_HANDLE_NULL)      /* free NULL is OK */
        goto fn_exit;
    MPIT_ERRTEST_PVAR_SESSION(session);
    MPIT_ERRTEST_PVAR_HANDLE(*handle);

    if ((*handle) == MPI_T_PVAR_ALL_HANDLES || (*handle)->session != session) {
        mpi_errno = MPI_T_ERR_INVALID_HANDLE;
        goto fn_fail;
    }

    /* ... body of routine ...  */

    mpi_errno = MPIR_T_pvar_handle_free_impl(session, handle);
    MPIR_ERR_CHECK(mpi_errno);

    /* ... end of body of routine ... */

  fn_exit:
    MPIR_FUNC_TERSE_EXIT(MPID_STATE_MPI_T_PVAR_HANDLE_FREE);
    MPIR_T_THREAD_CS_EXIT();
    return mpi_errno;

  fn_fail:
    goto fn_exit;
}
