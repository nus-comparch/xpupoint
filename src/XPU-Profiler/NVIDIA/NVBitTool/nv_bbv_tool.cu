/* BEGIN_LEGAL
 * Copyright (c) 2019, NVIDIA CORPORATION. All rights reserved.
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
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2024, National University of Singapore
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 END_LEGAL */

#include <algorithm>
#include <assert.h>
#include <bitset>
#include <inttypes.h>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <fstream>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_set>
#include <unordered_map>
#include <vector>
/* every tool needs to include this once */
#include "nvbit_tool.h"

/* nvbit interface file */
#include "nvbit.h"

/* for channel */
#include "utils/channel.hpp"

/* contains definition of the inst_trace_t structure */
#include "common.h"

/* nvbit interface file */
#include "NVBitShim.h"

#define MAX_THREADS 512

/* Channel used to communicate from GPU to CPU receiving thread */
#define CHANNEL_SIZE (1l << 24)
static __managed__ ChannelDev channel_dev;
static ChannelHost channel_host;

cpu_on_gpu_fini_ptr_t cpu_on_gpu_fini = 0;
cpu_on_gpu_init_ptr_t cpu_on_gpu_init = 0;
cpu_on_kernel_complete_ptr_t cpu_on_kernel_complete = 0;

/* receiving thread and its control variables */
pthread_t recv_thread;
volatile bool recv_thread_started = false;
volatile bool recv_thread_receiving = false;

/* skip flag used to avoid re-entry on the nvbit_callback when issuing
 * flush_channel kernel call */
bool mangled = false;
bool skip_flag = false;

/* global control variables for this tool */
uint32_t instr_begin_interval = 0;
uint32_t instr_end_interval = UINT32_MAX;
int verbose = 0;
int exclude_pred_off = 1;
int active_from_start = 1;
/* used to select region of interest when active from start is 0 */
bool active_region = true;

/* Should we terminate the program once we are done tracing? */
int terminate_after_limit_number_of_kernels_reached = 0;
int user_defined_directory = 0;

uint64_t kernel_counter = 1;
int max_warpid = 0;
std::vector<std::tuple<std::string, uint64_t>> region_boundary; 
std::map<std::string, uint64_t> curr_kernel_call; // kernel_name->call_num
std::string curr_kernel;
typedef struct
{
  std::map<std::tuple<uint64_t, uint32_t>, uint32_t> curr_bbv;  // store current bbv
  uint64_t insn_count;
} thread_data_t;
thread_data_t td[MAX_THREADS];
uint64_t curr_bbvid = 1;
std::map<std::tuple<uint64_t,uint32_t>, uint64_t> bbvids; // store (block-addr,inst-count)->bbid

/* opcode to id map and reverse map  */
std::map<std::string, int> opcode_to_id_map;
std::map<int, std::string> id_to_opcode_map;

std::string cwd = getcwd(NULL,0);
std::string tidstr = std::to_string(getpid());
std::string traces_location = cwd + "/BasicBlocks." + tidstr;
std::string stats_location = traces_location + "/stats.csv";
std::string global_bbv_location = traces_location + "/global.bbv";
std::string thread_bbv_location = traces_location + "/thread.bbv";

/* kernel instruction counter, updated by the GPU */
uint64_t dynamic_kernel_limit_start = 0; // 0 means start from the first kernel
uint64_t dynamic_kernel_limit_end = 0; // 0 means no limit

static FILE *statsFile = NULL;
std::ofstream globalBbvFile;
std::ofstream threadBbvFile;
static uint64_t kernelid = 1;
static bool first_call = true;

unsigned old_total_insts = 0;
unsigned old_total_reported_insts = 0;

void nvbit_at_init() {
  setenv("CUDA_MANAGED_FORCE_DEVICE_ALLOC", "1", 1);
  GET_VAR_INT(
      instr_begin_interval, "INSTR_BEGIN", 0,
      "Beginning of the instruction interval where to apply instrumentation");
  GET_VAR_INT(instr_end_interval, "INSTR_END", UINT32_MAX,
      "End of the instruction interval where to apply instrumentation");
  GET_VAR_INT(exclude_pred_off, "EXCLUDE_PRED_OFF", 1,
      "Exclude predicated off instruction from count");
  GET_VAR_INT(dynamic_kernel_limit_end, "DYNAMIC_KERNEL_LIMIT_END", 0,
      "Limit of the number kernel to be printed, 0 means no limit");
  GET_VAR_INT(dynamic_kernel_limit_start, "DYNAMIC_KERNEL_LIMIT_START", 0,
      "start to report kernel from this kernel id, 0 means starts from "
      "the beginning, i.e. first kernel");
  GET_VAR_INT(
      active_from_start, "ACTIVE_FROM_START", 1,
      "Start instruction tracing from start or wait for cuProfilerStart "
      "and cuProfilerStop. If set to 0, DYNAMIC_KERNEL_LIMIT options have no effect");
  GET_VAR_INT(verbose, "TOOL_VERBOSE", 0, "Enable verbosity inside the tool");
  GET_VAR_INT(terminate_after_limit_number_of_kernels_reached, "TERMINATE_UPON_LIMIT", 0, 
      "Stop the process once the current kernel > DYNAMIC_KERNEL_LIMIT_END");
  GET_VAR_INT(user_defined_directory, "USER_DEFINED_DIRECTORY", 0, "Uses the user defined "
      "BBV_DIR path environment");

  if (active_from_start == 0) {
    active_region = false;
  }
}

/* Set used to avoid re-instrumenting the same functions multiple times */
std::unordered_set<CUfunction> already_instrumented;

/* instrument each memory instruction adding a call to the above instrumentation
 * function */
void instrument_function_if_needed(CUcontext ctx, CUfunction func) {

  std::vector<CUfunction> related_functions =
    nvbit_get_related_functions(ctx, func);

  /* add kernel itself to the related function vector */
  related_functions.push_back(func);

  /* iterate on function */
  for (auto f : related_functions) {
    /* "recording" function was instrumented, if set insertion failed
     * we have already encountered this function */
    if (!already_instrumented.insert(f).second) {
      continue;
    }

    const std::vector<Instr *> &instrs = nvbit_get_instrs(ctx, f);
    if (verbose) {
      printf("Inspecting function %s at address 0x%lx\n", nvbit_get_func_name(ctx, f), nvbit_get_func_addr(f));
    }

    const CFG_t &cfg = nvbit_get_CFG(ctx, f);
    for (auto &bb : cfg.bbs) {
      Instr *instr = bb->instrs[0];
      auto addr = instr->getOffset();
      auto num_insn = bb->instrs.size();

      if (opcode_to_id_map.find(instr->getOpcode()) == opcode_to_id_map.end()) {
        int opcode_id = opcode_to_id_map.size();
        opcode_to_id_map[instr->getOpcode()] = opcode_id;
        id_to_opcode_map[opcode_id] = instr->getOpcode();
      }

      int opcode_id = opcode_to_id_map[instr->getOpcode()];

      /* insert call to the instrumentation function with its
       * arguments */
      nvbit_insert_call(instr, "instrument_inst", IPOINT_BEFORE);

      /* pass predicate value */
      nvbit_add_call_arg_guard_pred_val(instr);

      /* send opcode and pc */
      nvbit_add_call_arg_const_val32(instr, opcode_id);
      nvbit_add_call_arg_const_val32(instr, (int)instr->getOffset());

      /* add pointer to channel_dev and other counters*/
      nvbit_add_call_arg_const_val64(instr, (uint64_t)&channel_dev);
      nvbit_add_call_arg_const_val64(instr,
          (uint64_t)&total_dynamic_instr_counter);
      nvbit_add_call_arg_const_val64(instr,
          (uint64_t)&reported_dynamic_instr_counter);
      nvbit_add_call_arg_const_val64(instr, (uint64_t)&stop_report);
      nvbit_add_call_arg_const_val32(instr, (uint32_t)bb->instrs.size());
    }
  }
}

__global__ void flush_channel() {
  /* push memory access with negative cta id to communicate the kernel is
   * completed */
  inst_trace_t ma;
  ma.cta_id_x = -1;
  channel_dev.push(&ma, sizeof(inst_trace_t));

  /* flush channel */
  channel_dev.flush();
}

void nvbit_at_cuda_event(CUcontext ctx, int is_exit, nvbit_api_cuda_t cbid,
    const char *name, void *params, CUresult *pStatus) {

  if (skip_flag)
    return;

  if (first_call == true) {

    first_call = false;

    if (active_from_start && !dynamic_kernel_limit_start || dynamic_kernel_limit_start == 1)
      active_region = true;
    else {
      if (active_from_start)
        active_region = false;
    }

    if(user_defined_directory == 1)
    {
      std::string usr_dir = std::getenv("BBV_DIR");
      std::string temp_traces_location = usr_dir + "." + tidstr;
      std::string temp_stats_location = temp_traces_location + "/stats.csv";
      std::string temp_global_bbv_location = temp_traces_location + "/global.bbv";
      std::string temp_thread_bbv_location = temp_traces_location + "/thread.bbv";
      traces_location = temp_traces_location;
      stats_location = temp_stats_location;
      global_bbv_location = temp_global_bbv_location;
      thread_bbv_location = temp_thread_bbv_location;
      std::cerr << "[XPU_TRACER][" << __FUNCTION__ << "Traces location is " << traces_location << std::endl;
    }

    if (mkdir(traces_location.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
      if (errno == EEXIST) {
        std::cerr << "[XPU_TRACER][" << __FUNCTION__ << "] Directory " << traces_location << " exists" << std::endl;
      } else {
        std::cerr << "[XPU_TRACER][" << __FUNCTION__ << "] Cannot create directory " << traces_location << ". Error: " << strerror(errno) << std::endl;
        return;
      }
    }

    statsFile = fopen(stats_location.c_str(), "w+");
    fprintf(statsFile,
        "kernel id, kernel mangled name, grid_dimX, grid_dimY, grid_dimZ, "
        "#blocks, block_dimX, block_dimY, block_dimZ, #threads, "
        "total_insts, total_reported_insts\n");
    fclose(statsFile);
    if (!globalBbvFile.is_open()) {
      globalBbvFile.open(global_bbv_location, std::ios_base::out | std::ios_base::trunc);
      std::cout << "[XPU_TRACER][" << __FUNCTION__ << "] Opened " << global_bbv_location << std::endl;
      globalBbvFile << "M: SYS_init 1" << std::endl;
    }
    if (!threadBbvFile.is_open()) {
      threadBbvFile.open(thread_bbv_location, std::ios_base::out | std::ios_base::trunc);
      std::cout << "[XPU_TRACER][" << __FUNCTION__ << "] Opened " << thread_bbv_location << std::endl;
      threadBbvFile << "M: SYS_init 1" << std::endl;
    }
  }

  if (cbid == API_CUDA_cuLaunchKernel_ptsz ||
      cbid == API_CUDA_cuLaunchKernel) {
    cuLaunchKernel_params *p = (cuLaunchKernel_params *)params;

    if (!is_exit) {

      if (active_from_start && dynamic_kernel_limit_start && kernelid == dynamic_kernel_limit_start)
        active_region = true;

      if (terminate_after_limit_number_of_kernels_reached && dynamic_kernel_limit_end != 0 && kernelid > dynamic_kernel_limit_end)
      {
        exit(0);
      }

      const char* kernel_name = nvbit_get_func_name(ctx, p->f, true);
      std::string kname(kernel_name);
      if (curr_kernel_call.find(kname) == curr_kernel_call.end()) {
        curr_kernel_call[kname] = 1;
      }
      else {
        curr_kernel_call[kname]++;
      }
      region_boundary.push_back(std::make_pair(kname, curr_kernel_call[kname]));
      curr_kernel = kname;
      int nregs;
      CUDA_SAFECALL(
          cuFuncGetAttribute(&nregs, CU_FUNC_ATTRIBUTE_NUM_REGS, p->f));

      int shmem_static_nbytes;
      CUDA_SAFECALL(cuFuncGetAttribute(
            &shmem_static_nbytes, CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES, p->f));

      int binary_version;
      CUDA_SAFECALL(cuFuncGetAttribute(&binary_version,
            CU_FUNC_ATTRIBUTE_BINARY_VERSION, p->f));

      instrument_function_if_needed(ctx, p->f);

      if (active_region) {
        nvbit_enable_instrumented(ctx, p->f, true);
        stop_report = false;
      } else {
        nvbit_enable_instrumented(ctx, p->f, false);
        stop_report = true;
      }

      statsFile = fopen(stats_location.c_str(), "a");
      unsigned blocks = p->gridDimX * p->gridDimY * p->gridDimZ;
      unsigned threads = p->blockDimX * p->blockDimY * p->blockDimZ;

      fprintf(statsFile, "%ld, %s, %d, %d, %d, %d, %d, %d, %d, %d, ", kernelid,
          nvbit_get_func_name(ctx, p->f, true), p->gridDimX, p->gridDimY,
          p->gridDimZ, blocks, p->blockDimX, p->blockDimY, p->blockDimZ,
          threads);

      fclose(statsFile);
      kernelid++;
      recv_thread_receiving = true;

    } else {
      /* make sure current kernel is completed */
      if (verbose)
        std::cerr << "[XPU_TRACER][" << __FUNCTION__ << "] Kernel completed." << std::endl;
      cudaDeviceSynchronize();
      assert(cudaGetLastError() == cudaSuccess);

      /* make sure we prevent re-entry on the nvbit_callback when issuing
       * the flush_channel kernel */
      skip_flag = true;

      /* issue flush of channel so we are sure all the memory accesses
       * have been pushed */
      flush_channel<<<1, 1>>>();
      cudaDeviceSynchronize();
      assert(cudaGetLastError() == cudaSuccess);
      if (verbose)
        std::cerr << "Kernel exit counter: " << kernel_counter << std::endl;
      kernel_counter++;
      /* unset the skip flag */
      skip_flag = false;

      if(cpu_on_kernel_complete)
      {
        if(verbose) printf("Event: on_complete %lx\n", (uint64_t) cpu_on_kernel_complete);
        (*cpu_on_kernel_complete)(curr_kernel.c_str());
        if(verbose) printf("\n");
      }

      /* wait here until the receiving thread has not finished with the
       * current kernel */
      while (recv_thread_receiving) {
        pthread_yield();
      }

      if (verbose) {
        std::cerr  << "[XPU_TRACER][" << __FUNCTION__ << "] Ending region for kernel: " << curr_kernel << " call: " << curr_kernel_call[curr_kernel] << std::endl;
        std::cerr  << "[XPU_TRACER][" << __FUNCTION__ << "] Dynamic instruction count on GPU: " << total_dynamic_instr_counter << std::endl;
      }
      for (auto i = 0; i <= max_warpid; i++) {
        for (auto &v: td[i].curr_bbv) {
          if (bbvids.find(v.first) == bbvids.end()) {
            bbvids[v.first] = curr_bbvid;
            curr_bbvid++;
          }
        }
      }
      globalBbvFile << "# Slice ending at kernel: " << curr_kernel << " call: " << curr_kernel_call[curr_kernel] << std::endl;
      threadBbvFile << "# Slice ending at kernel: " << curr_kernel << " call: " << curr_kernel_call[curr_kernel] << std::endl;

      std::map<uint64_t, uint64_t> bbid_global;
      for (auto i = 0; i <= max_warpid; i++) {
        if (td[i].curr_bbv.empty())
          continue;
        threadBbvFile << "tid" << i << ": T";
        for (auto &m: td[i].curr_bbv) {
          auto bb = bbvids[m.first];
          bbid_global[bb] += m.second;
          threadBbvFile << ":" << bb << ":" << m.second << " ";
        }
        threadBbvFile << std::endl;
      }
      globalBbvFile << "T";
      for (auto& el:bbid_global) {
        globalBbvFile << ":" << el.first << ":" << el.second << " ";
      }
      globalBbvFile << std::endl;

      globalBbvFile << "M: " << curr_kernel << " " << curr_kernel_call[curr_kernel] << std::endl;
      threadBbvFile << "M: " << curr_kernel << " " << curr_kernel_call[curr_kernel] << std::endl;

      for (auto tid = 0; tid <= max_warpid; tid++) {
        if (!td[tid].curr_bbv.empty()) {
          td[tid].curr_bbv.clear();
        }
      }

      unsigned total_insts_per_kernel = total_dynamic_instr_counter - old_total_insts;
      old_total_insts = total_dynamic_instr_counter;

      unsigned reported_insts_per_kernel = reported_dynamic_instr_counter - old_total_reported_insts;
      old_total_reported_insts = reported_dynamic_instr_counter;

      statsFile = fopen(stats_location.c_str(), "a");
      fprintf(statsFile, "%d, %d", total_insts_per_kernel,	reported_insts_per_kernel);
      fprintf(statsFile, "\n");
      fclose(statsFile);

      if (active_from_start && dynamic_kernel_limit_end && kernelid > dynamic_kernel_limit_end)
        active_region = false;
    }
  } else if (cbid == API_CUDA_cuProfilerStart && is_exit) {
    if (!active_from_start) {
      active_region = true;
    }
  } else if (cbid == API_CUDA_cuProfilerStop && is_exit) {
    if (!active_from_start) {
      active_region = false;
    }
  }
}

void *recv_thread_fun(void *) {
  bool new_kernel = true;
  char *recv_buffer = (char *)malloc(CHANNEL_SIZE);
  while (recv_thread_started) {
    uint32_t num_recv_bytes = 0;

    if (recv_thread_receiving &&
        (num_recv_bytes = channel_host.recv(recv_buffer, CHANNEL_SIZE)) > 0) {
      if (new_kernel) {
        new_kernel = false;
        if (verbose >= 6) {
          std::cerr << "bb-ins, bb-vpc, warp-id-tb, warp-id-sm, thread-id, sm-id" << std::endl;
        }
      }
      uint32_t num_processed_bytes = 0;
      while (num_processed_bytes < num_recv_bytes) {
        inst_trace_t *ma = (inst_trace_t *)&recv_buffer[num_processed_bytes];

        /* when we get this cta_id_x it means the kernel has completed
         */
        if (ma->cta_id_x == -1) {
          recv_thread_receiving = false;
          new_kernel = true;
          break;
        }
        if (verbose >= 6) {
          std::cerr << ma->num_insn << ", " << ma->vpc << ", " << ma->warpid_tb << ", ";
          std::cerr << ma->warpid_sm << ", " << ma->thread_id << ", " << ma->sm_id << std::endl;
        }

        td[ma->warpid_sm].curr_bbv[std::tuple<uint64_t, uint32_t>(ma->vpc, ma->num_insn)] +=  ma->num_insn;
        td[ma->warpid_sm].insn_count += ma->num_insn;
        if (ma->warpid_sm > max_warpid)
          max_warpid = ma->warpid_sm;
        if (max_warpid >= MAX_THREADS) {
          std::cerr << "[XPU_TRACER][" << __FUNCTION__ << "] Error: Unable to handle the large number of warps." << std::endl;
        }
        num_processed_bytes += sizeof(inst_trace_t);
      }
    }
  }
  free(recv_buffer);
  return NULL;
}

void nvbit_at_ctx_init(CUcontext ctx) {
  recv_thread_started = true;
  channel_host.init(0, CHANNEL_SIZE, &channel_dev, NULL);
  pthread_create(&recv_thread, NULL, recv_thread_fun, NULL);
}

void nvbit_at_ctx_term(CUcontext ctx) {
  if (recv_thread_started) {
    recv_thread_started = false;
    pthread_join(recv_thread, NULL);
  }
}

void nvbit_at_term() {
  if (verbose)
    printf(" nvbit_at_term() \n");
  if(cpu_on_gpu_fini)
  {
    (*cpu_on_gpu_fini)();
  }
  for (auto tid = 0; tid <= max_warpid; tid++) {
    if (!td[tid].curr_bbv.empty()) {
      std::cerr << "[XPU_TRACER][" << __FUNCTION__ << "] Non-empty BBV for thread/warp " << tid << " at the end of program." << std::endl;
      td[tid].curr_bbv.clear();
    }
  }
  if (threadBbvFile.is_open()) {
    threadBbvFile.close();
  }
  if (globalBbvFile.is_open()) {
    globalBbvFile.close();
  }
}

void NVBitShimRegisterCallbacks(void *ptri, void *ptrc, void * ptrf)
{
  if (verbose)
    printf("NVBitShimRegisterCallbacks: on_init %lx, on_complete %lx, on_fini %lx\n", (uint64_t) ptri, (uint64_t) ptrc, (uint64_t) ptrf);
  cpu_on_gpu_init = (cpu_on_gpu_init_ptr_t) ptri;
  cpu_on_kernel_complete = (cpu_on_kernel_complete_ptr_t) ptrc;
  cpu_on_gpu_fini = (cpu_on_gpu_fini_ptr_t) ptrf;
  if(cpu_on_gpu_init)
  {
    if (verbose)
      printf("calling  (*cpu_on_gpu_init)()\n");
    (*cpu_on_gpu_init)();
  }
}
