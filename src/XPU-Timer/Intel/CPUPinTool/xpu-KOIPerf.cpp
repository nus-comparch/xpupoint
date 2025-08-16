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
#include <limits.h>
#include <sys/types.h>
#include "pin.H"
#include "GTPinLoaderShim.H"
#ifdef SDE_INIT
#  include "sde-init.H"
#  include "sde-control.H"
#endif

#include "tool_macros.h"
#include "lte_perf.h"


KNOB<BOOL> KnobRegionSpecified(KNOB_MODE_WRITEONCE, "pintool", "region_specified", "0",
                  "Region specified to the GPU tool. Use 0 for whole-program evaluation");

KNOB<string> KnobPerfOut(KNOB_MODE_WRITEONCE, "pintool", "perfout", "perf.txt",
                  "output file");

KNOB<BOOL> KnobSliceMode(KNOB_MODE_WRITEONCE, "pintool", "slice_mode", "0",
                  "Print slice number before each record");
KNOB<string> KnobOutDir(KNOB_MODE_WRITEONCE, "pintool", "outdir", ".", "Output directory");
KNOB<BOOL> KnobAddTid(KNOB_MODE_WRITEONCE, "pintool", "addtid", "0", "Add 'tid' suffix to output directory");


void read_environ(); // function to be added by pinball2elf*.sh scripts
//__lte_static int verbose=0;
//__lte_static int counters_started=0;
int out_fd = -1;
uint64_t init_rdtsc = 0;
bool wend_seen = false;
bool send_seen = false;
bool region_specified = false;
uint64_t wend_rdtsc = 0;
uint64_t send_rdtsc = 0;
uint64_t slicecount = 0;

uint64_t myrdtsc(){
    unsigned int lo,hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

void perf_activate()
{
   const char* fname = KnobPerfOut.Value().c_str();
   string outdir = KnobOutDir.Value();
   if(KnobAddTid)
   {
    string tidstr = std::to_string(PIN_GetTid());
    outdir=outdir+"."+tidstr;
   }
   mkdir(outdir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
   string outfile=outdir+"/"+ fname;
   out_fd = lte_open(outfile.c_str(), O_WRONLY | O_CREAT | O_TRUNC , S_IRUSR|S_IWUSR|S_IRGRP);
}


using namespace std;
GTPIN_LOADER gtpin_loader;

void CPU_on_xpu_event(const char * kname, UINT32 iteration, XPU_EVENT e)
{
  //cerr << "\t->CPU_on_kenel_run() : kerenel: " << PIN_UndecorateSymbolName(kname, UNDECORATION_NAME_ONLY) << endl;
  //if(! KnobSliceMode) cerr << "\t->CPU_on_xpu_event() : kerenel: " << PIN_UndecorateSymbolName(kname, UNDECORATION_COMPLETE) << " iteration: " << iteration << endl;
  switch(e)
  {
    case REGION_START:
      ASSERTX(region_specified);
      wend_rdtsc = myrdtsc();
      cerr << "\t\t REGION_START"<< endl;
      lte_write(out_fd, "Warmup end: TSC ", lte_strlen("Warmup end: TSC ")-1);
      lte_diprintfe(out_fd, wend_rdtsc, '\n');
      wend_seen = true;
      //lte_write(out_fd, "\tWarmup-end-icount (input)", lte_strlen("\tWarmup-end-icount (input)")-1);
      break;
    case REGION_STOP:
      ASSERTX(region_specified);
      cerr << "\t\t REGION_STOP"<< endl;
      if (!wend_seen)
      {
        // warmup not present, use init_rdtsc
        cerr << "\t\t WARNING: Warmup end was not seen, using init_rdtsc"<< endl;
        lte_write(out_fd, "Warmup end: TSC ", lte_strlen("Warmup end: TSC ")-1);
        lte_diprintfe(out_fd, init_rdtsc, '\n');
      }
      send_rdtsc = myrdtsc();
      lte_write(out_fd, "Simulation end: TSC ", lte_strlen("Simulation end: TSC ")-1);
      lte_diprintfe(out_fd, send_rdtsc, '\n');
      send_seen = true;
      break;
    case KOI_START:
      //cerr << "\t\t KOI_START"<< endl;
      lte_write(out_fd, "KOI_START: TSC ", lte_strlen("KOI_START: TSC ")-1);
      lte_diprintfe(out_fd, myrdtsc(), '\n');
      break;
    case KOI_STOP:
      if(KnobSliceMode)
      {
        lte_diprintfe(out_fd, slicecount, ' ');
        lte_write(out_fd, "OnComplete ", lte_strlen("OnComplete ")-1);
        lte_write(out_fd, kname, lte_strlen(kname)-1);
        lte_write(out_fd, " TSC ", lte_strlen(" TSC ")-1);
        lte_diprintfe(out_fd, myrdtsc(), '\n');
        slicecount++;
      }
      else
      {
        //cerr << "\t\t KOI_STOP"<< endl;
        lte_write(out_fd, "KOI_STOP: TSC ", lte_strlen("KOI_STOP: TSC ")-1);
        lte_diprintfe(out_fd, myrdtsc(), '\n');
      }
      break;
    default:
      break;
  }
}

void CPU_on_gpu_init()
{
  cerr << "\t->CPU_on_gpu_init()" << endl;
  if(KnobSliceMode) lte_diprintfe(out_fd, slicecount, ' ');
  lte_write(out_fd, "GPU_Init : TSC ", lte_strlen("GPU_Init : TSC ")-1);
  init_rdtsc = myrdtsc();
  lte_diprintfe(out_fd, init_rdtsc, '\n');
}

void CPU_on_gpu_fini()
{
  uint64_t fini_rdtsc = myrdtsc();
 
  if(!region_specified) // whole-program evaluation
  {
    if(KnobSliceMode) lte_diprintfe(out_fd, slicecount, ' ');
    lte_write(out_fd, "GPU_Fini : TSC ", lte_strlen("GPU_Fini : TSC ")-1);
    lte_diprintfe(out_fd, fini_rdtsc, '\n');
    return;
  }

  bool region_missed = (!send_seen) && (!wend_seen);
  if (region_missed)
  {
     // Here, we want all the RDTSC deltas to be zero. Use init_rdtsc everywhere
      cerr << "\t\t WARNING: region missed using init_rdtsc everywhere"<< endl;
      // warmup not present, use init_rdtsc
      lte_write(out_fd, "Warmup end: TSC ", lte_strlen("Warmup end: TSC ")-1);
      lte_diprintfe(out_fd, init_rdtsc, '\n');
      // simulation not present, use init_rdtsc
      lte_write(out_fd, "Simulation end: TSC ", lte_strlen("Simulation end: TSC ")-1);
      lte_diprintfe(out_fd, init_rdtsc, '\n');
      if(KnobSliceMode) lte_diprintfe(out_fd, slicecount, ' ');
      lte_write(out_fd, "GPU_Fini : TSC ", lte_strlen("GPU_Fini : TSC ")-1);
      lte_diprintfe(out_fd, init_rdtsc, '\n');
      lte_write(out_fd, "#WARNING: region missed \n", lte_strlen("#WARNING: region missed \n")-1);
  }
  else
  {
    if (!send_seen) // wend_seen must be true
    {
      ASSERTX(wend_seen);
      cerr << "\t\t WARNING: Simulation end was not seen, using fini_rdtsc"<< endl;
      // simulation not present, use fini_rdtsc
      lte_write(out_fd, "Simulation end: TSC ", lte_strlen("Simulation end: TSC ")-1);
      lte_diprintfe(out_fd, fini_rdtsc, '\n');
    }
    if (!wend_seen) // send_seen must be true
    {
      ASSERTX(send_seen);
      // REGION_STOP handled outputting of a dummy warmup end record
    }
    lte_write(out_fd, "GPU_Fini : TSC ", lte_strlen("GPU_Fini : TSC ")-1);
    fini_rdtsc = myrdtsc(); // Reading again for more precision
    lte_diprintfe(out_fd, fini_rdtsc, '\n');
    if (!send_seen)
    {
    lte_write(out_fd, "#WARNING simend missed \n", lte_strlen("#WARNING simend missed \n")-1);
    }
    if (!wend_seen)
    {
    lte_write(out_fd, "#WARNING warmupend missed \n", lte_strlen("#WARNING warmupend missed \n")-1);
    }
  }
  cerr << "\t->CPU_on_gpu_fini()" << endl;
}

INT32 Usage()
{
    cerr << KNOB_BASE::StringKnobSummary() << endl;

    return -1;
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
    region_specified = KnobRegionSpecified.Value();
    gtpin_loader.Activate(CPU_on_xpu_event, CPU_on_gpu_init, CPU_on_gpu_fini);
    perf_activate();

    if(gtpin_loader.KnobProbe)
      PIN_StartProgramProbed();
    else
      PIN_StartProgram();

    return 0;
}
