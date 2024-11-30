/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpioimpl.h"
#include <limits.h>
#include <assert.h>

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_read_all = PMPI_File_read_all
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_read_all MPI_File_read_all
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_read_all as PMPI_File_read_all
/* end of weak pragmas */
#elif defined(HAVE_WEAK_ATTRIBUTE)
int MPI_File_read_all(MPI_File fh, void *buf, int count, MPI_Datatype datatype, MPI_Status * status)
    __attribute__ ((weak, alias("PMPI_File_read_all")));
#endif

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_read_all_c = PMPI_File_read_all_c
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_read_all_c MPI_File_read_all_c
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_read_all_c as PMPI_File_read_all_c
/* end of weak pragmas */
#elif defined(HAVE_WEAK_ATTRIBUTE)
int MPI_File_read_all_c(MPI_File fh, void *buf, MPI_Count count, MPI_Datatype datatype,
                        MPI_Status * status)
    __attribute__ ((weak, alias("PMPI_File_read_all_c")));
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/* status object not filled currently */

/*@
    MPI_File_read_all - Collective read using individual file pointer

Input Parameters:
. fh - file handle (handle)
. count - number of elements in buffer (nonnegative integer)
. datatype - datatype of each buffer element (handle)

Output Parameters:
. buf - initial address of buffer (choice)
. status - status object (Status)

.N fortran
@*/
int MPI_File_read_all(MPI_File fh, void *buf, int count, MPI_Datatype datatype, MPI_Status * status)
{
    int error_code;
    static char myname[] = "MPI_FILE_READ_ALL";

    error_code = MPIOI_File_read_all(fh, (MPI_Offset) 0,
                                     ADIO_INDIVIDUAL, buf, count, datatype, myname, status);


    return error_code;
}

/* large count function */


/* status object not filled currently */

/*@
    MPI_File_read_all_c - Collective read using individual file pointer

Input Parameters:
. fh - file handle (handle)
. count - number of elements in buffer (nonnegative integer)
. datatype - datatype of each buffer element (handle)

Output Parameters:
. buf - initial address of buffer (choice)
. status - status object (Status)

.N fortran
@*/
int MPI_File_read_all_c(MPI_File fh, void *buf, MPI_Count count, MPI_Datatype datatype,
                        MPI_Status * status)
{
    int error_code;
    static char myname[] = "MPI_FILE_READ_ALL";

    error_code = MPIOI_File_read_all(fh, (MPI_Offset) 0,
                                     ADIO_INDIVIDUAL, buf, count, datatype, myname, status);


    return error_code;
}

/* Note: MPIOI_File_read_all also used by MPI_File_read_at_all */
/* prevent multiple definitions of this routine */
#ifdef MPIO_BUILD_PROFILING
int MPIOI_File_read_all(MPI_File fh,
                        MPI_Offset offset,
                        int file_ptr_type,
                        void *buf,
                        MPI_Aint count, MPI_Datatype datatype, char *myname, MPI_Status * status)
{
    int error_code;
    MPI_Count datatype_size;
    ADIO_File adio_fh;
    void *xbuf = NULL, *e32_buf = NULL, *host_buf = NULL;

    ROMIO_THREAD_CS_ENTER();

    adio_fh = MPIO_File_resolve(fh);

    /* --BEGIN ERROR HANDLING-- */
    MPIO_CHECK_FILE_HANDLE(adio_fh, myname, error_code);
    MPIO_CHECK_COUNT(adio_fh, count, myname, error_code);
    MPIO_CHECK_DATATYPE(adio_fh, datatype, myname, error_code);

    if (file_ptr_type == ADIO_EXPLICIT_OFFSET && offset < 0) {
        error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
                                          myname, __LINE__, MPI_ERR_ARG, "**iobadoffset", 0);
        error_code = MPIO_Err_return_file(adio_fh, error_code);
        goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

    MPI_Type_size_x(datatype, &datatype_size);

    /* --BEGIN ERROR HANDLING-- */
    MPIO_CHECK_INTEGRAL_ETYPE(adio_fh, count, datatype_size, myname, error_code);
    MPIO_CHECK_READABLE(adio_fh, myname, error_code);
    MPIO_CHECK_NOT_SEQUENTIAL_MODE(adio_fh, myname, error_code);
    MPIO_CHECK_COUNT_SIZE(adio_fh, count, datatype_size, myname, error_code);
    /* --END ERROR HANDLING-- */

    xbuf = buf;
    if (adio_fh->is_external32) {
        MPI_Aint e32_size = 0;
        error_code = MPIU_datatype_full_size(datatype, &e32_size);
        if (error_code != MPI_SUCCESS)
            goto fn_exit;

        e32_buf = ADIOI_Malloc(e32_size * count);
        xbuf = e32_buf;
    } else {
        MPIO_GPU_HOST_ALLOC(host_buf, buf, count, datatype);
        if (host_buf != NULL) {
            xbuf = host_buf;
        }
    }

    ADIO_ReadStridedColl(adio_fh, xbuf, count, datatype, file_ptr_type,
                         offset, status, &error_code);

    /* --BEGIN ERROR HANDLING-- */
    if (error_code != MPI_SUCCESS)
        error_code = MPIO_Err_return_file(adio_fh, error_code);
    /* --END ERROR HANDLING-- */

    if (e32_buf != NULL) {
        error_code = MPIU_read_external32_conversion_fn(buf, datatype, count, e32_buf);
        ADIOI_Free(e32_buf);
    }

    MPIO_GPU_SWAP_BACK(host_buf, buf, count, datatype);

  fn_exit:
    ROMIO_THREAD_CS_EXIT();

    return error_code;
}
#endif
