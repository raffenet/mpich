/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpitest.h"

#ifdef MULTI_TESTS
#define run coll_allgatherv3
int run(const char *arg);
#endif

/* Test Allgatherv on array of doubles, same as allgatherv2 but without
 * MPI_IN_PLACE. This is the trivial version based on the allgather3
 * test (allgatherv but with constant data sizes) */

int run(const char *arg)
{
    double *vecout, *invec;
    MPI_Comm comm;
    int count, minsize = 2;
    int i, errs = 0;
    int rank, size;
    int *displs, *recvcounts;

    while (MTestGetIntracommGeneral(&comm, minsize, 1)) {
        if (comm == MPI_COMM_NULL)
            continue;
        /* Determine the sender and receiver */
        MPI_Comm_rank(comm, &rank);
        MPI_Comm_size(comm, &size);

        displs = (int *) malloc(size * sizeof(int));
        recvcounts = (int *) malloc(size * sizeof(int));

        for (count = 1; count < 9000; count = count * 2) {
            invec = (double *) malloc(count * sizeof(double));
            vecout = (double *) malloc(size * count * sizeof(double));

            for (i = 0; i < count; i++) {
                invec[i] = rank * count + i;
            }
            for (i = 0; i < size; i++) {
                recvcounts[i] = count;
                displs[i] = i * count;
            }
            MPI_Allgatherv(invec, count, MPI_DOUBLE, vecout, recvcounts, displs, MPI_DOUBLE, comm);
            for (i = 0; i < count * size; i++) {
                if (vecout[i] != i) {
                    errs++;
                    if (errs < 10) {
                        fprintf(stderr, "vecout[%d]=%d\n", i, (int) vecout[i]);
                    }
                }
            }
            free(invec);
            free(vecout);
        }
        free(displs);
        free(recvcounts);
        MTestFreeComm(&comm);
    }

    return errs;
}
