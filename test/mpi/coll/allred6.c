/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpitest.h"

#ifdef MULTI_TESTS
#define run coll_allred6
int run(const char *arg);
#endif

/*
static char MTEST_Descrip[] = "Test MPI_Allreduce with apparent non-commutative operators";
*/
/* While the operator is in fact commutative, this forces the MPI code to
   run the code that is used for non-commutative operators, and for
   various message lengths.  Other tests check truly non-commutative
   operators */

static void mysum(void *cinPtr, void *coutPtr, int *count, MPI_Datatype * dtype)
{
    const int *cin = (const int *) cinPtr;
    int *cout = (int *) coutPtr;
    int i, n = *count;
    for (i = 0; i < n; i++)
        cout[i] += cin[i];
}

int run(const char *arg)
{
    int errs = 0;
    int rank, size;
    int minsize = 2, count;
    MPI_Comm comm;
    MPI_Op op;
    int *buf, i;

    MPI_Op_create(mysum, 0, &op);

    while (MTestGetIntracommGeneral(&comm, minsize, 1)) {
        if (comm == MPI_COMM_NULL)
            continue;
        MPI_Comm_size(comm, &size);
        MPI_Comm_rank(comm, &rank);

        for (count = 1; count < 65000; count = count * 2) {
            /* Contiguous data */
            buf = (int *) malloc(count * sizeof(int));
            for (i = 0; i < count; i++)
                buf[i] = rank + i;
            MPI_Allreduce(MPI_IN_PLACE, buf, count, MPI_INT, op, comm);
            /* Check the results */
            for (i = 0; i < count; i++) {
                int result = i * size + (size * (size - 1)) / 2;
                if (buf[i] != result) {
                    errs++;
                    if (errs < 10) {
                        fprintf(stderr, "buf[%d] = %d expected %d\n", i, buf[i], result);
                    }
                }
            }
            free(buf);
        }
        MTestFreeComm(&comm);
    }
    MPI_Op_free(&op);

    return errs;
}
