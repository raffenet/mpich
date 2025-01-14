/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpitest.h"
#include <string.h>

#ifdef MULTI_TESTS
#define run coll_alltoallv
int run(const char *arg);
#endif

/*
  This program tests MPI_Alltoallv by having processor i send different
  amounts of data to each processor.

  Because there are separate send and receive types to alltoallv,
  there need to be tests to rearrange data on the fly.  Not done yet.

  The first test sends i items to processor i from all processors.

  Currently, the test uses only MPI_INT; this is adequate for testing systems
  that use point-to-point operations
 */

int run(const char *arg)
{

    MPI_Comm comm;
    int *sbuf, *rbuf;
    int rank, size;
    int *sendcounts, *recvcounts, *rdispls, *sdispls;
    int i, j, *p, errs;

    errs = 0;

    while (MTestGetIntracommGeneral(&comm, 2, 1)) {
        if (comm == MPI_COMM_NULL)
            continue;

        /* Create the buffer */
        MPI_Comm_size(comm, &size);
        MPI_Comm_rank(comm, &rank);
        sbuf = (int *) malloc(size * size * sizeof(int));
        rbuf = (int *) malloc(size * size * sizeof(int));
        if (!sbuf || !rbuf) {
            fprintf(stderr, "Could not allocated buffers!\n");
            MPI_Abort(comm, 1);
        }

        /* Load up the buffers */
        for (i = 0; i < size * size; i++) {
            sbuf[i] = i + 100 * rank;
            rbuf[i] = -i;
        }

        /* Create and load the arguments to alltoallv */
        sendcounts = (int *) malloc(size * sizeof(int));
        recvcounts = (int *) malloc(size * sizeof(int));
        rdispls = (int *) malloc(size * sizeof(int));
        sdispls = (int *) malloc(size * sizeof(int));
        if (!sendcounts || !recvcounts || !rdispls || !sdispls) {
            fprintf(stderr, "Could not allocate arg items!\n");
            MPI_Abort(comm, 1);
        }
        for (i = 0; i < size; i++) {
            sendcounts[i] = i;
            recvcounts[i] = rank;
            rdispls[i] = i * rank;
            sdispls[i] = (i * (i + 1)) / 2;
        }
        MPI_Alltoallv(sbuf, sendcounts, sdispls, MPI_INT, rbuf, recvcounts, rdispls, MPI_INT, comm);

        /* Check rbuf */
        for (i = 0; i < size; i++) {
            p = rbuf + rdispls[i];
            for (j = 0; j < rank; j++) {
                if (p[j] != i * 100 + (rank * (rank + 1)) / 2 + j) {
                    fprintf(stderr, "[%d] got %d expected %d for %dth\n",
                            rank, p[j], (i * (i + 1)) / 2 + j, j);
                    errs++;
                }
            }
        }

        free(sdispls);
        free(sendcounts);
        free(sbuf);

#if MTEST_HAVE_MIN_MPI_VERSION(2,2)
        /* check MPI_IN_PLACE, added in MPI-2.2 */
        free(rbuf);
        rbuf = (int *) malloc(size * (2 * size) * sizeof(int));
        if (!rbuf) {
            fprintf(stderr, "Could not reallocate rbuf!\n");
            MPI_Abort(comm, 1);
        }

        /* Load up the buffers */
        for (i = 0; i < size; i++) {
            recvcounts[i] = i + rank;
            rdispls[i] = i * (2 * size);
        }
        memset(rbuf, -1, size * (2 * size) * sizeof(int));
        for (i = 0; i < size; i++) {
            p = rbuf + rdispls[i];
            for (j = 0; j < recvcounts[i]; ++j) {
                p[j] = 100 * rank + 10 * i + j;
            }
        }
        MPI_Alltoallv(MPI_IN_PLACE, NULL, NULL, MPI_INT, rbuf, recvcounts, rdispls, MPI_INT, comm);
        /* Check rbuf */
        for (i = 0; i < size; i++) {
            p = rbuf + rdispls[i];
            for (j = 0; j < recvcounts[i]; j++) {
                int expected = 100 * i + 10 * rank + j;
                if (p[j] != expected) {
                    fprintf(stderr, "[%d] got %d expected %d for block=%d, element=%dth\n",
                            rank, p[j], expected, i, j);
                    ++errs;
                }
            }
        }
#endif

        free(rdispls);
        free(recvcounts);
        free(rbuf);
        MTestFreeComm(&comm);
    }

    return errs;
}
