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
#include "NVBitShim.h"
#include "NVBitHandler.H"
#ifdef SDE_INIT
#  include "sde-init.H"
#  include "sde-control.H"
#endif

#include "tool_macros.h"
#include "lte_perf.h"

NVBIT_HANDLER nvbit_handler;

KNOB<string> KnobPerfOut(KNOB_MODE_WRITEONCE, "pintool", "perfout", "cpu_perfout.txt",
                  "output file");
KNOB<BOOL> KnobSliceMode(KNOB_MODE_WRITEONCE, "pintool", "slice_mode", "0",
                  "Print slice number before each record");
KNOB<string> KnobOutDir(KNOB_MODE_WRITEONCE, "pintool", "outdir", "./cpuperfdir", "Output directory");


void read_environ(); // function to be added by pinball2elf*.sh scripts
//__lte_static int verbose=0;
//__lte_static int counters_started=0;
int out_fd = -1;
uint64_t init_rdtsc = 0;
bool wend_seen = false;
bool send_seen = false;
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
   string tidstr = std::to_string(PIN_GetTid());
   outdir=outdir+"."+tidstr;
   mkdir(outdir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
   string outfile=outdir+"/"+ fname;
   out_fd = lte_open(outfile.c_str(), O_WRONLY | O_CREAT | O_TRUNC , S_IRUSR|S_IWUSR|S_IRGRP);
}


using namespace std;


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
    if(KnobSliceMode) lte_diprintfe(out_fd, slicecount, ' '); 
    lte_write(out_fd, "GPU_Fini : TSC ", lte_strlen("GPU_Fini : TSC ")-1);
    lte_diprintfe(out_fd, fini_rdtsc, '\n');
    return;
}

void CPU_on_kernel_complete(const char * kname)
{
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
    perf_activate();
    nvbit_handler.Activate(CPU_on_gpu_init,CPU_on_kernel_complete,CPU_on_gpu_fini);

    if(nvbit_handler.KnobProbe)
      PIN_StartProgramProbed();
    else
      PIN_StartProgram();

    return 0;
}
