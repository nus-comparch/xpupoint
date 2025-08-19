/* Copyright (c) 2019, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <unordered_set>
#include <fstream>
#include <set>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>


/* every tool needs to include this once */
#include "nvbit_tool.h"

/* nvbit interface file */
#include "nvbit.h"

/* nvbit utility functions */
#include "utils/utils.h"

/* nvbit interface file */
#include "NVBitShim.h"

using namespace std;

cpu_on_gpu_fini_ptr_t cpu_on_gpu_fini = 0;
cpu_on_gpu_init_ptr_t cpu_on_gpu_init = 0;
cpu_on_kernel_complete_ptr_t cpu_on_kernel_complete = 0;

uint32_t kernel_id = 0;

/* global control variables for this tool */
int verbose = 0;
bool mangled = true;
std::string  gpu_perfout = "gpu_perfout.txt";
std::string  gpu_outdir = "./gpuperfdir";
std::ofstream perf_fs;

uint64_t myrdtsc(){
    unsigned int lo,hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}
uint64_t                _completeCounter = 0;
uint64_t								_runCounter = 0;

/* used to select region of insterest when active from start is off */
bool active_region = true;

/* a pthread mutex, used to prevent multiple kernels to run concurrently and
 * therefore to "corrupt" the counter variable */
pthread_mutex_t mutex;

/* nvbit_at_init() is executed as soon as the nvbit tool is loaded. We typically
 * do initializations in this call. In this case for instance we get some
 * environment variables values which we use as input arguments to the tool */
void nvbit_at_init() {
    /* just make sure all managed variables are allocated on GPU */
    setenv("CUDA_MANAGED_FORCE_DEVICE_ALLOC", "1", 1);

    /* we get some environment variables that are going to be use to selectively
     * instrument (within a interval of kernel indexes and instructions). By
     * default we instrument everything. */

    GET_VAR_INT(verbose, "TOOL_VERBOSE", 0, "Enable verbosity inside the tool");
    std::string pad(100, '-');
    printf("%s\n", pad.c_str());

    GET_VAR_STR(gpu_perfout, "TOOL_GPU_PERFOUT", "Output xpu_event RDTSC here");
    printf("%s\n", pad.c_str());

    GET_VAR_STR(gpu_outdir, "TOOL_GPU_OUTDIR", "Output dirctory");
    printf("%s\n", pad.c_str());

   if(gpu_perfout != "" )
   {
      string fname = gpu_perfout;
      string outdir = gpu_outdir;
      string tidstr = std::to_string(getpid());
      printf("outdir %s\n", outdir.c_str());
      outdir=outdir+"."+tidstr;
      printf("outdir %s\n", outdir.c_str());
      mkdir(outdir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
      string outfile=outdir+"/"+ fname;
      printf("outfile %s\n", outfile.c_str());
      perf_fs.open(outfile);
   }
   // begining of slice/interval 0
   uint64_t init_rdtsc = myrdtsc();
   if(perf_fs.is_open()) perf_fs << "0 GPU_Init : TSC " << init_rdtsc << endl;
}

/* Set used to avoid re-instrumenting the same functions multiple times */
std::unordered_set<CUfunction> already_instrumented;

void instrument_function_if_needed(CUcontext ctx, CUfunction func) {
}

/* This call-back is triggered every time a CUDA driver call is encountered.
 * Here we can look for a particular CUDA driver call by checking at the
 * call back ids  which are defined in tools_cuda_api_meta.h.
 * This call back is triggered bith at entry and at exit of each CUDA driver
 * call, is_exit=0 is entry, is_exit=1 is exit.
 * */
void nvbit_at_cuda_event(CUcontext ctx, int is_exit, nvbit_api_cuda_t cbid,
                         const char *name, void *params, CUresult *pStatus) {
    /* Identify all the possible CUDA launch events */
    if (cbid == API_CUDA_cuLaunch || cbid == API_CUDA_cuLaunchKernel_ptsz ||
        cbid == API_CUDA_cuLaunchGrid || cbid == API_CUDA_cuLaunchGridAsync ||
        cbid == API_CUDA_cuLaunchKernel) {
        /* cast params to cuLaunch_params since if we are here we know these are
         * the right parameters type */
        cuLaunch_params *p = (cuLaunch_params *)params;

        if (!is_exit) {
            /* if we are entering in a kernel launch:
             * 1. Lock the mutex to prevent multiple kernels to run concurrently
             * (overriding the counter) in case the user application does that
             * 2. Instrument the function if needed
             * 3. Select if we want to run the instrumented or original
             * version of the kernel
             * 4. Reset the kernel instruction counter */

            pthread_mutex_lock(&mutex);
            //instrument_function_if_needed(ctx, p->f);
						
            if (active_region) {
                nvbit_enable_instrumented(ctx, p->f, true);
            } else {
                nvbit_enable_instrumented(ctx, p->f, false);
            }
						string kname = nvbit_get_func_name(ctx, p->f, mangled);
						uint64_t onrun_rdtsc = myrdtsc();
						if(perf_fs.is_open()) perf_fs << _runCounter << " OnRun " << kname << " TSC " << onrun_rdtsc << endl;
            ++_runCounter;

            //if(cpu_on_kernel_run)
            //{
              //printf("Event: on_run %lx\n", (uint64_t) cpu_on_kernel_run);
              //(*cpu_on_kernel_run)(nvbit_get_func_name(ctx, p->f, mangled));
            //}
        } else {
            /* if we are exiting a kernel launch:
             * 1. Wait until the kernel is completed using
             * cudaDeviceSynchronize()
             * 2. Get number of thread blocks in the kernel
             * 3. Print the thread instruction counters
             * 4. Release the lock*/
            CUDA_SAFECALL(cudaDeviceSynchronize());
            string kname = nvbit_get_func_name(ctx, p->f, mangled);
            uint64_t oncomplete_rdtsc = myrdtsc();
            //printf( "kernel %d - %s\n",
            //   kernel_id++, nvbit_get_func_name(ctx, p->f, mangled));
            if(cpu_on_kernel_complete)
            {
              (*cpu_on_kernel_complete)(kname.c_str());
            }
          if(perf_fs.is_open()) perf_fs << _completeCounter << " OnComplete " << kname << " TSC " << oncomplete_rdtsc << endl;
            ++_completeCounter;
            pthread_mutex_unlock(&mutex);
        }
    } else if (cbid == API_CUDA_cuProfilerStart && is_exit) {
            active_region = true;
    } else if (cbid == API_CUDA_cuProfilerStop && is_exit) {
            active_region = false;
    }
}

void nvbit_at_term() {
    printf(" nvbit_at_term() \n");
    uint64_t fini_rdtsc = myrdtsc();
    if(perf_fs.is_open()) perf_fs << _completeCounter << " GPU_Fini : TSC " << fini_rdtsc << endl;
    if(cpu_on_gpu_fini)
    {
      (*cpu_on_gpu_fini)();
    }
}

//void NVBitShimRegisterCallbacks(void * ptrb, void * ptrr, void * ptrc, void * ptrf)
void NVBitShimRegisterCallbacks(void *ptri, void *ptrc, void * ptrf)
{
    printf("NVBitShimRegisterCallbacks: on_init %lx, on_complete %lx, on_fini %lx\n", (uint64_t) ptri, (uint64_t) ptrc, (uint64_t) ptrf);
    cpu_on_gpu_init = (cpu_on_gpu_init_ptr_t) ptri;
    cpu_on_kernel_complete = (cpu_on_kernel_complete_ptr_t) ptrc;
    cpu_on_gpu_fini = (cpu_on_gpu_fini_ptr_t) ptrf;
    if(cpu_on_gpu_init)
    {
      printf("calling  (*cpu_on_gpu_init)()\n");
      (*cpu_on_gpu_init)();
    }
}
