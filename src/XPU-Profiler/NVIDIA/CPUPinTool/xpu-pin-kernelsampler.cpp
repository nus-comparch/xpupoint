/*BEGIN_LEGAL 
BSD License 

Copyright (c)2022 Intel Corporation. All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <list>
#include <unistd.h>
#include <sys/syscall.h>
#include <sched.h>
#include <sys/stat.h>
#include <limits>
#include <sys/types.h>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>
#include <cmath>
#include "pin.H"
#include "NVBitShim.h"
#include "NVBitHandler.H"
#ifdef SDE_INIT
#  include "sde-init.H"
#  include "sde-control.H"
#  include "sde-pinplay-supp.H"
#else
#  include "isimpoint_inst.H"
#endif
#include "bbv_count.h"
#include "tuple_hash.h"
#include <wait.h>
#include <features.h>
#include <sys/stat.h>

#define tuple pair
#define MAX_THREADS 512

using namespace std;
NVBIT_HANDLER nvbit_handler;

static ISIMPOINT pp_isimpoint;
static ISIMPOINT* isimpoint;
static FILTER_MOD filter;

vector<PIN_MUTEX> thread_mutex(MAX_THREADS);
uint64_t regions_seen = 0;
uint64_t gpu_regions_seen = 0;
std::vector<std::tuple<std::string, uint64_t>> region_boundary; // [(kernel_name, call_num)]
std::unordered_map<std::string, uint64_t> curr_kernel_call; // kernel_name->call_num
uint64_t curr_bbvid = 1;
std::unordered_map<std::tuple<uint64_t, uint32_t>, uint64_t> bbvids;
std::ofstream fp[MAX_THREADS];
std::unordered_map<std::string, bool> fileDict;
//std::string outdir;

//KNOB<string> KnobBBDir(KNOB_MODE_WRITEONCE, "pintool", "bbdir", "BasicBlocks", "Output bbv directory");
KNOB<BOOL> KnobBBVerbose(KNOB_MODE_WRITEONCE, "pintool", "bbverbose", "0", "Output verbose messages");
KNOB<BOOL> KnobBBNoCrud(KNOB_MODE_WRITEONCE, "pintool", "bbno_crud", "1", "Reset profiles on the first on_kernel_run");

typedef struct
{
  std::unordered_map<std::tuple<uint64_t, uint32_t>, uint64_t> curr_bbv;  // store current bbv
  uint64_t insn_count;
} thread_data_t;
thread_data_t td[MAX_THREADS];

uint64_t tot_insn_count = 0;

typedef struct
{
  volatile UINT64 counter;
  //int _dummy[15];
} atomic_t;

atomic_t max_thread_id = {0};

static inline UINT64 atomic_add_return(UINT64 i, atomic_t *v)
{
  return __sync_fetch_and_add(&(v->counter), i);
}

#define atomic_inc_return(v)  (atomic_add_return(1, v))

static inline UINT64 atomic_set(atomic_t *v, UINT64 i)
{
  return __sync_lock_test_and_set(&(v->counter), i);
}

static inline UINT64 atomic_get(atomic_t *v)
{
  return v->counter;
}

void CPU_on_gpu_init()
{
  if(KnobBBVerbose) std::cerr << "[XPU_TRACER]["<< __FUNCTION__ <<"] Pid " << getpid() << " \t->CPU_on_gpu_init()" << std::endl;
  return;
}

void CPU_on_gpu_fini()
{
  std::cerr << "[XPU_TRACER][" << __FUNCTION__ << "]\t->CPU_on_gpu_fini()" << std::endl;
  return;
}

void CPU_on_kernel_complete(const char * kname)
{
  if(KnobBBVerbose) std::cerr << "[XPU_TRACER]["<< __FUNCTION__ <<"] Pid " << getpid() << " \t->CPU_on_kernel_complete() : kernel: " << PIN_UndecorateSymbolName(kname, UNDECORATION_COMPLETE) << std::endl;
  if(KnobBBVerbose) std::cerr << "[XPU_TRACER]["<< __FUNCTION__ <<"] Pid " << getpid() << " Ending GPU region " << gpu_regions_seen << std::endl;
  gpu_regions_seen += 1;
  std::string kernel_name = PIN_UndecorateSymbolName(kname, UNDECORATION_COMPLETE);
  if(KnobBBVerbose) std::cerr << "[XPU_TRACER]["<< __FUNCTION__ <<"] Pid " << getpid() << " Kernel end " << kname << std::endl;
  if (curr_kernel_call.find(kname) == curr_kernel_call.end()) {
    curr_kernel_call[kname] = 1;
  }
  else {
    curr_kernel_call[kname]++;
  }
  region_boundary.push_back(std::make_pair(kname, curr_kernel_call[kname]));
#if 0
  std::cerr << "[XPU_TRACER]["<< __FUNCTION__ <<"] Dynamic instruction count on CPU: " << tot_insn_count << std::endl;
  if(KnobBBVerbose){
    std::cerr << "[XPU_TRACER]["<< __FUNCTION__ <<"] Dynamic instruction count on CPU per-thread: [ ";
    for (uint64_t i = 0 ; i <= atomic_get(&max_thread_id) ; i++) {
      std::cerr << td[i].insn_count << " ";
    }
    std::cerr << "]" << std::endl;
  }
#endif

  for (uint64_t tid = 0 ; tid <= atomic_get(&max_thread_id) ; tid++) {
    if(isimpoint->IsThreadOfInterest(tid)) 
    {
      auto last_el = region_boundary.back();
      isimpoint->EmitVectorForFriend(tid, isimpoint, last_el.first, last_el.second);
    }
    if (tid == 0) { // Assuming tid 0 is the dominant thread
      if(KnobBBVerbose) std::cerr << "[XPU_TRACER]["<< __FUNCTION__ <<"] Pid " << getpid() << " Ending CPU region " << regions_seen << std::endl;
      regions_seen += 1;
    }
  }

  if(KnobBBVerbose) std::cerr << "[XPU_TRACER]["<< __FUNCTION__ <<"] Pid " << getpid() << " \t->CPU_on_kernel_run() : kernel: " << PIN_UndecorateSymbolName(kname, UNDECORATION_COMPLETE) << std::endl;
  if((gpu_regions_seen==0)&& KnobBBNoCrud)
  {
    for (uint64_t tid = 0 ; tid <= atomic_get(&max_thread_id) ; tid++) {
      if(isimpoint->IsThreadOfInterest(tid)) 
      {
        std::cerr << "[XPU_TRACER]["<< __FUNCTION__ <<"] Resetting BBV for Pid " << getpid() << " tid " << tid <<  " \t->CPU_on_kernel_run() : kernel: " << PIN_UndecorateSymbolName(kname, UNDECORATION_COMPLETE) << std::endl;
        isimpoint->ClearBBV(tid, PIN_UndecorateSymbolName(kname, UNDECORATION_COMPLETE));
        isimpoint->ResetSliceTimer(tid, isimpoint);
      }
    }
  }
  return;
}

INT32 Usage()
{
  std::cerr << KNOB_BASE::StringKnobSummary() << endl;
  return -1;
}

VOID captureBBVs(THREADID thread_id, ADDRINT address, INT32 count)
{
  PIN_MutexLock(&thread_mutex[thread_id]);
  float count_f = static_cast<float>(count);
  if (std::isnan(count_f)) {
    std::cerr << "[XPU_TRACER]["<< __FUNCTION__ <<"]\tInvalid BB count encountered; replacing with zero." << std::endl;
    count = 0;
  }
  td[thread_id].curr_bbv[std::tuple<uint64_t, uint32_t>(address, count)] +=  count;
  td[thread_id].insn_count += count;
  tot_insn_count += count;
  PIN_MutexUnlock(&thread_mutex[thread_id]);
}

#if 0
VOID traceCallback(TRACE trace, VOID* v)
{
  BBL bbl_head = TRACE_BblHead(trace);

  RTN rtn = TRACE_Rtn(trace);
  if (!RTN_Valid(rtn))  return;

  SEC sec = RTN_Sec(rtn);
  if (!SEC_Valid(sec))  return;

  IMG img = SEC_Img(sec);
  if (!IMG_Valid(img))  return;

  for (BBL bbl = bbl_head; BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
    INS_InsertCall(BBL_InsTail(bbl),
        IPOINT_BEFORE,
        (AFUNPTR)captureBBVs,
        IARG_THREAD_ID,
        IARG_ADDRINT,
        BBL_Address(bbl),
        IARG_UINT32,
        BBL_NumIns(bbl),
        IARG_END);
  }
}

VOID OpenFile (THREADID thread_id)
{
  auto bb_filename = outdir + "/T." + std::to_string(thread_id) + ".bb";
  if (fileDict.find(bb_filename) == fileDict.end()) {
    fp[thread_id].open(bb_filename, std::ios_base::out | std::ios_base::trunc);
    if (fp[thread_id].is_open()) {
      fileDict[bb_filename] = true;
      std::cerr << "[XPU_TRACER][" << __FUNCTION__ << "] Opened " << bb_filename << std::endl;
    }
    else {
      std::cerr << "[XPU_TRACER][" << __FUNCTION__ << "] Failed to open " << bb_filename << std::endl;
    }
  }
}
#endif

VOID threadStart(THREADID thread_id, CONTEXT *ctxt, INT32 flags, VOID *v)
{
  if (thread_id >= MAX_THREADS)
  {
    std::cerr << "[XPU_TRACER]["<< __FUNCTION__ <<"] Pid " << getpid() << " Error: More threads requested than we have allocated space for (MAX=" << MAX_THREADS << ", id=" << thread_id << ")" << std::endl;
    PIN_ExitApplication(1);
  }
  if (thread_id > 0)
  { 
    while (atomic_set(&max_thread_id, thread_id) < thread_id) {}
    std::cerr << "[XPU_TRACER]["<< __FUNCTION__ <<"] Max Thread ID = " << atomic_get(&max_thread_id) << std::endl;
  }
#if 0
  OpenFile(thread_id);
#endif
}

VOID programStart(VOID *v)
{
  regions_seen = 0;
  gpu_regions_seen = 0;
#if 0
  outdir = KnobBBDir.Value();
  string tidstr = std::to_string(PIN_GetTid());
  outdir = outdir + "." + tidstr;
  if (mkdir(outdir.c_str(), 0777) == -1)
  {
    cerr << "Unable to create directory " << outdir << endl;
    if(errno == EEXIST)
    {
      cerr << "Error: directory " << outdir << " already exists" << endl;
    }
  }
#endif
}

VOID programEnd(INT32, VOID *v)
{
  for (unsigned int tid = 0 ; tid < MAX_THREADS ; tid++) {
    if (!td[tid].curr_bbv.empty()) {
      if (tid == 0) {
        std::cerr << "[XPU_TRACER]["<< __FUNCTION__ <<"] Pid " << getpid() << " Ending CPU region " << regions_seen << std::endl;
        regions_seen += 1;
        std::cerr << "[XPU_TRACER]["<< __FUNCTION__ <<"] Pid " << getpid() << " Total CPU regions found: " << regions_seen << std::endl;
        std::cerr << "[XPU_TRACER]["<< __FUNCTION__ <<"] Pid " << getpid() << " Total GPU regions found: " << gpu_regions_seen << std::endl;
      }
    }
  }

#if 0
  for (uint64_t i = 0 ; i <= atomic_get(&max_thread_id) ; i++)
  {
    for (auto &v : td[i].curr_bbv) {
      if (bbvids.count(v.first) == 0) {
        bbvids[v.first] = curr_bbvid;
        curr_bbvid++;
      }
    }
  }

  for (uint64_t i = 0 ; i <= atomic_get(&max_thread_id) ; i++) {
    fp[i] << "\n";
    fp[i] << "# Slice ending at kernel: SYS_exit call: 1" << "\n";
    fp[i] << "T";
    for (auto &m: td[i].curr_bbv) {
      uint64_t bb = bbvids[m.first];
      float bb_f = static_cast<float>(bb);
      if (std::isnan(bb_f)) {
        std::cerr << "[XPU_TRACER]["<< __FUNCTION__ <<"]\tInvalid BB encountered; ignoring." << std::endl;
      }
      else {
        fp[i] << ":" << bb << ":" << m.second << " ";
      }
    }
    fp[i] << "\n# Program End\n";
  }
  std::cerr << "[XPU_TRACER]["<< __FUNCTION__ <<"] Dynamic instruction count on CPU: " << tot_insn_count << std::endl;
  if (KnobBBVerbose) {
    std::cerr << "[XPU_TRACER]["<< __FUNCTION__ <<"] Dynamic instruction count on CPU per-thread: [ ";
    for (uint64_t i = 0 ; i <= atomic_get(&max_thread_id) ; i++) {
      std::cerr << td[i].insn_count << " ";
    }
    std::cerr << "]" << std::endl;
  }
  for (uint64_t i = 0 ; i <= atomic_get(&max_thread_id) ; i++) {
    fp[i].flush();
    fp[i].close();
  }
#endif 
}

VOID programDetach(VOID *v)
{
  programEnd(0, v);
}

// argc, argv are the entire command line, including pin -t <toolname> -- ...
int main(int argc, char * argv[])
{
#if defined(SDE_INIT)
  sde_pin_init(argc,argv);
  sde_init();
#else
  if( PIN_Init(argc,argv) )
  {
    return Usage();
  }
#endif
  PIN_InitSymbols();
  nvbit_handler.Activate(CPU_on_gpu_init,CPU_on_kernel_complete,CPU_on_gpu_fini);

  filter.Activate();
  PIN_AddApplicationStartFunction(programStart, 0);
  PIN_AddFiniFunction(programEnd, 0);
  PIN_AddDetachFunction(programDetach, 0);
  PIN_AddThreadStartFunction(threadStart, 0);

#if defined(SDE_INIT)
  isimpoint      = sde_tracing_get_isimpoint();
  cerr << "isimpoint " << isimpoint << endl;
#else
  isimpoint = &pp_isimpoint;
  isimpoint->activate(argc, argv, &filter);
#endif


  if(nvbit_handler.KnobProbe)
    PIN_StartProgramProbed();
  else
    PIN_StartProgram();

  return 0;
}
