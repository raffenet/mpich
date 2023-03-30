/*
 *  Copyright (C) by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpl_gpu_cuda.h"
#include <stdio.h>

#define FASTBOX_SIZE 16*1024*1024

__global__ void MPL_gpu_kernel_trigger(MPL_gpu_event_t *var)
{
    *var -= 1;
    __threadfence_system();
}

__global__ void MPL_gpu_kernel_wait(MPL_gpu_event_t *var)
{
    while(*var > 0);
}

__global__ void MPL_gpu_kernel_fbox_send(void *fastbox, const void *src, size_t len)
{
    int idx = blockDim.x * blockIdx.x + threadIdx.x;
    char *dstbuf = (char *)fastbox + 64;
    char *srcbuf = (char *)src;
    volatile int *flag = (int *)fastbox;

    if (idx == 0) {
        while (*flag != 0); /* wait for empty fastbox, mark as busy */
    }
    __syncthreads();

    for (int i = idx; i < len; i += 256) {
       dstbuf[i] = srcbuf[i];
    }

    __threadfence_system();
    __syncthreads();
    if (idx == 0) {
        *flag = 1;
    }
}

__global__ void MPL_gpu_kernel_fbox_recv(void *dst, const void *fastbox, size_t len)
{
    int idx = blockDim.x * blockIdx.x + threadIdx.x;
    char *dstbuf = (char *)dst;
    char *srcbuf = (char *)fastbox + 64;
    volatile int *flag = (int *)fastbox;

    if (idx == 0) {
        while (*flag != 1); /* wait for full fastbox */
    }
    __syncthreads();

    for (int i = idx; i < len; i += 256) {
        dstbuf[i] = srcbuf[i];
    }

    __syncthreads();
    if (idx == 0) {
        *flag = 0; /* mark fastbox as empty (0) */
    }
}

extern "C"
void MPL_gpu_enqueue_trigger(volatile int *var, cudaStream_t stream)
{
    cudaError_t cerr;
    void *args[] = {&var};
    cerr = cudaLaunchKernel((const void *) MPL_gpu_kernel_trigger, dim3(1,1,1), dim3(1,1,1),
                            args, 0, stream);
    if (cerr != cudaSuccess) {
        fprintf(stderr, "CUDA Error (%s): %s\n", __func__, cudaGetErrorString(cerr));
    }
}

extern "C"
void MPL_gpu_enqueue_wait(volatile int *var, cudaStream_t stream)
{
    cudaError_t cerr;

    void *args[] = {&var};
    cerr = cudaLaunchKernel((const void *) MPL_gpu_kernel_wait, dim3(1,1,1), dim3(1,1,1),
                            args, 0, stream);
    if (cerr != cudaSuccess) {
        fprintf(stderr, "CUDA Error (%s): %s\n", __func__, cudaGetErrorString(cerr));
    }
}

extern "C"
void MPL_gpu_event_init_count(MPL_gpu_event_t *var, int count)
{
    *var = count;
}

extern "C"
void MPL_gpu_event_complete(MPL_gpu_event_t *var)
{
    *var -= 1;
}

extern "C"
bool MPL_gpu_event_is_complete(MPL_gpu_event_t *var)
{
    return (*var) <= 0;
}

extern "C"
void MPL_gpu_send_fastbox(const void *sendbuf, void *fastbox, size_t len, cudaStream_t stream)
{
    //printf("sending %lu bytes to %p\n", len, fastbox);
    MPL_gpu_kernel_fbox_send<<<1, 256, 0, stream>>>(fastbox, sendbuf, len);
}

extern "C"
void MPL_gpu_recv_fastbox(const void *fastbox, void *recvbuf, size_t len, cudaStream_t stream)
{
    //printf("recving %lu bytes from %p\n", len, fastbox);
    MPL_gpu_kernel_fbox_recv<<<1, 256, 0, stream>>>(recvbuf, fastbox, len);
}

__global__ void MPL_gpu_kernel_copy(void *dst, const void *src, size_t len)
{
    int idx = blockDim.x * blockIdx.x + threadIdx.x;
    char *dstbuf = (char *)dst;
    char *srcbuf = (char *)src;

    for (int i = idx; i < len; i += 256) {
       dstbuf[i] = srcbuf[i];
    }
}

extern "C"
void MPL_gpu_memcpy(void *dst, const void *src, size_t len, cudaStream_t stream)
{
    MPL_gpu_kernel_copy<<<1, 256, 0, stream>>>(dst, src, len);
}

__global__ void MPL_gpu_kernel_wait(volatile int *flag, int val)
{
    while (*flag != val);
}

__global__ void MPL_gpu_kernel_set(volatile int *flag, int val)
{
    *flag = val;
}

extern "C"
void MPL_gpu_wait_cts(volatile int *flag, cudaStream_t stream)
{
    MPL_gpu_kernel_wait<<<1, 1, 0, stream>>>(flag, 0);
}

extern "C"
void MPL_gpu_wait_data(volatile int *flag, cudaStream_t stream)
{
    MPL_gpu_kernel_wait<<<1, 1, 0, stream>>>(flag, 1);
}

extern "C"
void MPL_gpu_set_cts(volatile int *flag, cudaStream_t stream)
{
    MPL_gpu_kernel_set<<<1, 1, 0, stream>>>(flag, 0);
}

extern "C"
void MPL_gpu_set_data(volatile int *flag, cudaStream_t stream)
{
    MPL_gpu_kernel_set<<<1, 1, 0, stream>>>(flag, 1);
}
