/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#ifndef MPIR_ERRHANDLER_H_INCLUDED
#define MPIR_ERRHANDLER_H_INCLUDED

/*E
  errhandler_fn - MPIR Structure to hold an error handler function

  Notes:
  The MPI-1 Standard declared only the C version of this, implicitly
  assuming that 'int' and 'MPI_Fint' were the same.

  Since Fortran does not have a C-style variable number of arguments
  interface, the Fortran interface simply accepts two arguments.  Some
  calling conventions for Fortran (particularly under Windows) require
  this.

  Module:
  ErrHand-DS

  Questions:
  What do we want to do about C++?  Do we want a hook for a routine that can
  be called to throw an exception in C++, particularly if we give C++ access
  to this structure?  Does the C++ handler need to be different (not part
  of the union)?

  E*/
typedef union errhandler_fn {
    MPI_Comm_errhandler_function *C_Comm_Handler_function;
    MPI_File_errhandler_function *C_File_Handler_function;
    MPI_Win_errhandler_function *C_Win_Handler_function;
    MPI_Session_errhandler_function *C_Session_Handler_function;
    MPIX_Comm_errhandler_function_x *X_Comm_Handler_function;
    MPIX_File_errhandler_function_x *X_File_Handler_function;
    MPIX_Win_errhandler_function_x *X_Win_Handler_function;
    MPIX_Session_errhandler_function_x *X_Session_Handler_function;
    void (*F77_Handler_function) (MPI_Fint *, MPI_Fint *);
} errhandler_fn;

/*S
  MPIR_Errhandler - Description of the error handler structure

  Notes:
  Device-specific information may indicate whether the error handler is active;
  this can help prevent infinite recursion in error handlers caused by
  user-error without requiring the user to be as careful.  We might want to
  make this part of the interface so that the 'MPI_xxx_call_errhandler'
  routines would check.

  It is useful to have a way to indicate that the errhandler is no longer
  valid, to help catch the case where the user has freed the errhandler but
  is still using a copy of the 'MPI_Errhandler' value.  We may want to
  define the 'id' value for deleted errhandlers.

  Module:
  ErrHand-DS
  S*/
typedef struct MPIR_Errhandler {
    MPIR_OBJECT_HEADER;         /* adds handle and ref_count fields */
    MPIR_Lang_t language;
    MPII_Object_kind kind;
    void *extra_state;
    errhandler_fn errfn;
    MPIX_Destructor_function *destructor_fn;
    /* Other, device-specific information */
#ifdef MPID_DEV_ERRHANDLER_DECL
     MPID_DEV_ERRHANDLER_DECL
#endif
} MPIR_Errhandler;
extern MPIR_Object_alloc_t MPIR_Errhandler_mem;
/* Preallocated errhandler objects */
extern MPIR_Errhandler MPIR_Errhandler_builtin[];
extern MPIR_Errhandler MPIR_Errhandler_direct[];

/* We never reference count the builtin error handler objects, regardless of how
 * we decide to reference count the other predefined objects.  If we get to the
 * point where we never reference count *any* of the builtin objects then we
 * should probably remove these checks and let them fall through to the checks
 * for BUILTIN down in the MPIR_Object_* routines. */
#define MPIR_Errhandler_add_ref(_errhand)                               \
    do {                                                                  \
        if (!HANDLE_IS_BUILTIN((_errhand)->handle)) { \
            MPIR_Object_add_ref(_errhand);                              \
        }                                                                 \
    } while (0)
#define MPIR_Errhandler_release_ref(_errhand, _inuse)                   \
    do {                                                                  \
        if (!HANDLE_IS_BUILTIN((_errhand)->handle)) { \
            MPIR_Object_release_ref((_errhand), (_inuse));              \
        }                                                                 \
        else {                                                            \
            *(_inuse) = 1;                                                \
        }                                                                 \
    } while (0)

typedef struct {
    int kind;
    union {
        int handle;
        MPI_File fh;
    } u;
} MPIR_handle;

int MPIR_call_errhandler(MPIR_Errhandler * errhandler, int errorcode, MPIR_handle handle);

#endif /* MPIR_ERRHANDLER_H_INCLUDED */
