/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_get_group = PMPI_File_get_group
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_get_group MPI_File_get_group
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_get_group as PMPI_File_get_group
/* end of weak pragmas */
#elif defined(HAVE_WEAK_ATTRIBUTE)
int MPI_File_get_group(MPI_File fh, MPI_Group * group)
    __attribute__ ((weak, alias("PMPI_File_get_group")));
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/*@
    MPI_File_get_group - Returns the group of processes that
                         opened the file

Input Parameters:
. fh - file handle (handle)

Output Parameters:
. group - group that opened the file (handle)

.N fortran
@*/
int MPI_File_get_group(MPI_File fh, MPI_Group * group)
{
    int error_code;
    ROMIO_THREAD_CS_ENTER();

    error_code = MPIR_File_get_group_impl(fh, group);
    if (error_code) {
        goto fn_fail;
    }

  fn_exit:
    ROMIO_THREAD_CS_EXIT();
    return error_code;
  fn_fail:
    error_code = MPIO_Err_return_file(fh, error_code);
    goto fn_exit;
}
