/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpiimpl.h"

/* -- Begin Profiling Symbol Block for routine MPI_T_pvar_write */
#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_T_pvar_write = PMPI_T_pvar_write
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_T_pvar_write  MPI_T_pvar_write
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_T_pvar_write as PMPI_T_pvar_write
#elif defined(HAVE_WEAK_ATTRIBUTE)
int MPI_T_pvar_write(MPI_T_pvar_session session, MPI_T_pvar_handle handle, const void *buf)
    __attribute__ ((weak, alias("PMPI_T_pvar_write")));
#endif
/* -- End Profiling Symbol Block */

/* Define MPICH_MPI_FROM_PMPI if weak symbols are not supported to build
   the MPI routines */
#ifndef MPICH_MPI_FROM_PMPI
#undef MPI_T_pvar_write
#define MPI_T_pvar_write PMPI_T_pvar_write

/* any non-MPI functions go here, especially non-static ones */

int MPIR_T_pvar_write_impl(MPI_T_pvar_session session, MPI_T_pvar_handle handle, const void *buf)
{
    /* This function should never be called */
    return MPI_ERR_INTERN;
}

#endif /* MPICH_MPI_FROM_PMPI */

/*@
MPI_T_pvar_write - Write a performance variable

Input Parameters:
+ session - identifier of performance experiment session (handle)
. handle - handle of a performance variable (handle)
- buf - initial address of storage location for variable value (choice)

Notes:
The MPI_T_pvar_write() call attempts to write the value of the performance variable
with the handle identified by the parameter handle in the session identified by the parameter
session. The value to be written is passed in the buffer identified by the parameter buf. The
user must ensure that the buffer is of the appropriate size to hold the entire value of the
performance variable (based on the datatype and count returned by the corresponding previous
calls to MPI_T_pvar_get_info() and MPI_T_pvar_handle_alloc(), respectively).

The constant MPI_T_PVAR_ALL_HANDLES cannot be used as an argument for the function
MPI_T_pvar_write().

.N ThreadSafe

.N Errors
.N MPI_SUCCESS
.N MPI_T_ERR_NOT_INITIALIZED
.N MPI_T_ERR_INVALID_SESSION
.N MPI_T_ERR_INVALID_HANDLE
.N MPI_T_ERR_PVAR_NO_WRITE
@*/
int MPI_T_pvar_write(MPI_T_pvar_session session, MPI_T_pvar_handle handle, const void *buf)
{
    int mpi_errno = MPI_SUCCESS;

    MPIR_FUNC_TERSE_STATE_DECL(MPID_STATE_MPI_T_PVAR_WRITE);
    MPIT_ERRTEST_MPIT_INITIALIZED();
    MPIR_T_THREAD_CS_ENTER();
    MPIR_FUNC_TERSE_ENTER(MPID_STATE_MPI_T_PVAR_WRITE);

    /* Validate parameters, especially handles needing to be converted */
    MPIT_ERRTEST_PVAR_SESSION(session);
    MPIT_ERRTEST_PVAR_HANDLE(handle);
    MPIT_ERRTEST_ARGNULL(buf);
    if (handle == MPI_T_PVAR_ALL_HANDLES || handle->session != session) {
        mpi_errno = MPI_T_ERR_INVALID_HANDLE;
        goto fn_fail;
    }

    /* ... body of routine ...  */

    if (MPIR_T_pvar_is_readonly(handle)) {
        mpi_errno = MPI_T_ERR_PVAR_NO_WRITE;
        goto fn_fail;
    }

    mpi_errno = MPIR_T_pvar_write_impl(session, handle, buf);
    if (mpi_errno != MPI_SUCCESS)
        goto fn_fail;

    /* ... end of body of routine ... */

  fn_exit:
    MPIR_FUNC_TERSE_EXIT(MPID_STATE_MPI_T_PVAR_WRITE);
    MPIR_T_THREAD_CS_EXIT();
    return mpi_errno;

  fn_fail:
    goto fn_exit;
}
