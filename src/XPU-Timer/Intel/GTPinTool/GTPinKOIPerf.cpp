/*========================== begin_copyright_notice ============================
# Copyright (C) 2022 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause
============================= end_copyright_notice ===========================*/

/*!
 * @file A GTPin tool that adds no extra instructions but activates all GTPin flows
 */

#include <fstream>
#include <set>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>



//#include "gtpin.h"
#include "gtpin_api.h"
#include "gtpin_tool_utils.h"
#include "GTPinShim.h"
#include "GTPinKernelControl.H"

using namespace gtpin;
using namespace std;

cpu_on_xpu_event_ptr_t cpu_on_xpu_event = 0;
cpu_on_gpu_init_ptr_t cpu_on_gpu_init = 0;
cpu_on_gpu_fini_ptr_t cpu_on_gpu_fini = 0;
uint64_t init_rdtsc = 0;
bool wend_seen = false;
bool send_seen = false;
bool KOI_seen = false;
bool region_specified = false;
uint64_t wend_rdtsc = 0;
uint64_t send_rdtsc = 0;

uint64_t myrdtsc(){
    unsigned int lo,hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}


KERNEL_CONTROL kcontrol;
std::ofstream perf_fs;

/* ============================================================================================= */
// Configuration
/* ============================================================================================= */

Knob<bool> knobNoOutput("no_output", true, "Do not store profile data in file");
Knob<string> knobGPUperf("gpu_perfout", "", "Output xpu_event RDTSC here");
Knob<bool> knobperfOnKernel("perfOnKernel", false, "Output RDTSC on kernel run/completion");
Knob<string> knobOutDir("gpuoutdir", ".", "Output directory");
Knob<bool> KnobGPUAddTid("gpuaddtid", false, "Add 'tid' suffix to output directory");

/* ============================================================================================= */
// Class GTPinShimTool
/* ============================================================================================= */
/*!
 * A GTPin tool that adds no extra instructions but activates all GTPin flows.
 * The tool verifies that the amounts of OnKernelRun and OnKernelComplete events are equal.
 * Allows a CPU-Pin tool to register callbacks on kernel run and on kernel complete.
 */
class GTPinShimTool : public GtTool
{
public:
    /// Implementation of the IGtTool interface
    const char* Name() const { return "GTPinShimTool"; }

    void OnKernelBuild(IGtKernelInstrument& instrumentor)
    {
      return;
    }

    void OnKernelRun(IGtKernelDispatch& dispatcher)
    {
        const IGtKernel& kernel   = dispatcher.Kernel();
        GtGpuPlatform    platform = kernel.GpuPlatform();
        GtKernelExecDesc execDesc; dispatcher.GetExecDescriptor(execDesc);
        bool isProfileEnabled = IsKernelExecProfileEnabled(execDesc, platform);

        dispatcher.SetProfilingMode(isProfileEnabled);
#if 1 // nested kernel calls can happen 
        string kname = kernel.Name();
        uint64_t onrun_rdtsc = myrdtsc();
       // Since currently regions end OnComplete, skip outputting OnRun records
        if(knobperfOnKernel)
        {
          if(perf_fs.is_open()) perf_fs << _runCounter << " OnRun " << kname << " TSC " << onrun_rdtsc << endl; 
        }
#endif
        ++_runCounter;
    }

    void OnKernelComplete(IGtKernelDispatch& dispatcher)
    {
       const IGtKernel& kernel = dispatcher.Kernel();
        string kname = kernel.Name();
        XPU_EVENT e = XPU_EVENT_INVALID;
        //first check --kstart/--kstop
        uint32_t iteration = kcontrol.CountKernelStartStop(kname, &e);
        uint64_t oncomplete_rdtsc = myrdtsc();
        if (iteration)
        {
  switch(e)
  {
    case REGION_START:
      //ASSERTX(region_specified);
      wend_rdtsc = oncomplete_rdtsc;
      if(perf_fs.is_open()) perf_fs << "Warmup end: TSC " << wend_rdtsc << endl; 
      wend_seen = true;
      break;
    case REGION_STOP:
      //ASSERTX(region_specified);
      if (!wend_seen)
      {
        // warmup not present, use init_rdtsc
        //cerr << "\t\t WARNING: Warmup end was not seen, using init_rdtsc"<< endl;
        if(perf_fs.is_open()) perf_fs << "Warmup end: TSC " << init_rdtsc << endl; 
      }
      send_rdtsc = oncomplete_rdtsc;
      //lte_write(out_fd, "Simulation end: TSC ", lte_strlen("Simulation end: TSC ")-1);
      if(perf_fs.is_open()) perf_fs << "Simulation end: TSC " << send_rdtsc << endl; 
      send_seen = true;
      break;
    default:
      break;
  }
          if(cpu_on_xpu_event)
          {
            (*cpu_on_xpu_event)(kname.c_str(), iteration, e);
          }
        }
        //then check kspec.in
        iteration = kcontrol.CountKernelGeneral(kname);
        oncomplete_rdtsc = myrdtsc(); // refresh
        if (iteration)
        {
          KOI_seen = true;
          if(perf_fs.is_open()) perf_fs << "  --> KOI_START :" << kname << ":" << iteration << " : TSC " << oncomplete_rdtsc  << endl; 
          if(cpu_on_xpu_event)
          {
            (*cpu_on_xpu_event)(kname.c_str(), iteration, KOI_START);
          }
        }
        if(knobperfOnKernel)
        {
          if(cpu_on_xpu_event)
          {
            // NOTE: passing 0 as 'iteration'
            (*cpu_on_xpu_event)(kname.c_str(), 0, KOI_STOP);
          }
          if(perf_fs.is_open()) perf_fs << _completeCounter << " OnComplete " << kernel.Name() << " TSC " << oncomplete_rdtsc << endl; 
        }
        ++_completeCounter;
    }

    /// @return Single instance of this class
    static GTPinShimTool* Instance()
    {
        static GTPinShimTool instance;
        return &instance;
    }

    /// Callback function registered with atexit()
    static void OnFiniPerfoOnKernel()
    {
        GTPinShimTool& me = *Instance();
        uint64_t fini_rdtsc = myrdtsc();

        if(perf_fs.is_open()) perf_fs << me._completeCounter << " GPU_Fini : TSC " << fini_rdtsc << endl; 
        if(cpu_on_gpu_fini)
        {
           (*cpu_on_gpu_fini)();
        }
        return;
    }

    /// Callback function registered with atexit()
    static void OnFini()
    {
      uint64_t fini_rdtsc = myrdtsc();
    if(!region_specified) // whole-program evaluation
    {
        if(perf_fs.is_open()) perf_fs << "GPU_Fini : TSC " << fini_rdtsc << endl; 
    }
    else
    {
  bool region_missed = (!send_seen) && (!wend_seen) &&(!KOI_seen);
  if (region_missed)
  {
     // Here, we want all the RDTSC deltas to be zero. Use init_rdtsc everywhere
      // warmup not present, use init_rdtsc
        if(perf_fs.is_open()) perf_fs << "Warmup end: TSC " << init_rdtsc << endl; 
      // simulation not present, use init_rdtsc
        if(perf_fs.is_open()) perf_fs << "Simulation end: TSC " << init_rdtsc << endl; 
        if(perf_fs.is_open()) perf_fs << "GPU_Fini : TSC " << init_rdtsc << endl; 
        if(perf_fs.is_open()) perf_fs << "#WARNING: region missed " << endl;
  }
  else
  {
    if (!send_seen) // wend_seen must be true
    {
      //ASSERTX(wend_seen);
      // simulation not present, use fini_rdtsc
        if(perf_fs.is_open()) perf_fs << "Simulation end: TSC " << fini_rdtsc << endl; 
    }
    if (!wend_seen) // send_seen must be true
    {
      //ASSERTX(send_seen);
      // REGION_STOP handled outputting of a dummy warmup end record
    }
    fini_rdtsc = myrdtsc(); // Reading again for more precision
    if(perf_fs.is_open()) perf_fs << "GPU_Fini : TSC " << fini_rdtsc << endl; 
    if (!send_seen)
    {
      if(perf_fs.is_open()) perf_fs << "#WARNING simend missed " << endl;
    }
    if (!wend_seen)
    {
      if(perf_fs.is_open()) perf_fs << "#WARNING warmupend missed " << endl;
    }
  }
      }

        if(cpu_on_gpu_fini)
        {
           (*cpu_on_gpu_fini)();
        }
        if (knobNoOutput)
        {
            return;
        }

        IGtCore* gtpinCore = GTPin_GetCore();
        GTPinShimTool& me = *Instance();

        gtpinCore->CreateProfileDir();
        std::ofstream fs(JoinPath(gtpinCore->ProfileDir(), "report.txt"));
        //fs  << " GTPin_Entry called. CPU_var " << CPU_var << endl;
        fs << "OnKernelRun calls:      " << me._runCounter << std::endl;
        fs << "OnKernelComplete calls: " << me._completeCounter << std::endl;

        bool success = true;
        if (me._completeCounter != me._runCounter)
        {
            fs << "Number of OnKernelComplete callbacks mismatched the number of OnKernelRun callbacks" << std::endl;
            success = false;
        }

        fs << (success ? "PASSED" : "FAILED") << std::endl;
    }

private:
    uint64_t                _runCounter = 0;
    uint64_t                _completeCounter = 0;
};

/* ============================================================================================= */
// GTPin_Entry
/* ============================================================================================= */
EXPORT_C_FUNC void GTPin_Entry(int argc, const char *argv[])
{
    SetKnobValue<bool>(true, "always_allocate_buffers"); // Enforce profile buffer allocation to check the buffer-dependent flows
    SetKnobValue<bool>(true, "no_empty_profile_dir");    // Do not create empty profile directory
    ConfigureGTPin(argc, argv);

   if(knobGPUperf != "" ) 
   {
      string fname = knobGPUperf;
      string outdir = knobOutDir;
     if(KnobGPUAddTid)
     {
      string tidstr = std::to_string(getpid());
      outdir=outdir+"."+tidstr;
     }
      mkdir(outdir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
      string outfile=outdir+"/"+ fname;
      perf_fs.open(outfile);
   }
    if (!kcontrol.ParseKernelSpec())
    {
      cerr << "WARNING: no kernel specification file provided." << endl;
    }
    else
      region_specified = true; 

    GTPinShimTool::Instance()->Register();
    if(cpu_on_gpu_init)
    {
      (*cpu_on_gpu_init)();
    }
    init_rdtsc = myrdtsc();
    if(knobperfOnKernel)
    {
       atexit(GTPinShimTool::OnFiniPerfoOnKernel);
       // begining of slice/interval 0
      if(perf_fs.is_open()) perf_fs << "0 GPU_Init : TSC " << init_rdtsc << endl; 
    }
    else
    {
       atexit(GTPinShimTool::OnFini);
      if(perf_fs.is_open()) perf_fs << "GPU_Init : TSC " << init_rdtsc << endl; 
    }
}

EXPORT_C_FUNC void GTPinShimRegisterCallbacks(void * ptrk, void * ptri, void * ptrf) 
{
    cpu_on_xpu_event = (cpu_on_xpu_event_ptr_t) ptrk;
    cpu_on_gpu_init = (cpu_on_gpu_init_ptr_t) ptri;
    cpu_on_gpu_fini = (cpu_on_gpu_fini_ptr_t) ptrf;
}
