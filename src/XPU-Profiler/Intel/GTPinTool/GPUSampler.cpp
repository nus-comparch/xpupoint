/*========================== begin_copyright_notice ============================
Copyright (C) 2018-2022 Intel Corporation

SPDX-License-Identifier: MIT
============================= end_copyright_notice ===========================*/

/*!
 * @file Implementation of the XPU-Sampler tool
 */

#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <functional>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>


#include "GTPinShim.h"
#include "bblprof.h"

using namespace gtpin;

cpu_on_kernel_build_ptr_t cpu_on_kernel_build = 0;
cpu_on_kernel_run_ptr_t cpu_on_kernel_run = 0;
cpu_on_kernel_complete_ptr_t cpu_on_kernel_complete = 0;
cpu_on_gpu_fini_ptr_t cpu_on_gpu_fini = 0;

/* ============================================================================================= */
// Configuration
/* ============================================================================================= */
Knob<bool> knobTotalOnly("total_only", false, "bblprof: provide only aggregated data over all kernels over entire workload");
Knob<int>  knobNumThreadBuckets("num_thread_buckets", 32, "Number of thread buckets. 0 - maximum thread buckets");
Knob<std::string> knobBBDir("gpubbdir", "BasicBlocks", "Output directory");

/* ============================================================================================= */
// BBLprofKernelProfile implementation
/* ============================================================================================= */
BBLprofKernelProfile::BBLprofKernelProfile(const IGtKernel& kernel, const IGtCfg& cfg, const GtProfileArray& profileArray) :
    _name(GlueString(kernel.Name())), _uniqueName(kernel.UniqueName()), 
    _asmText(CfgAsmText(cfg)), _profileArray(profileArray), _summary_bblFreq(cfg.NumBbls(), 0), _global_bblFreq(cfg.NumBbls(),0)
{
}

std::string BBLprofKernelProfile::GetSummaryBBVs() const
{
    std::ostringstream ostr;

    for (uint32_t bblId = 0; bblId != _summary_bblFreq.size(); bblId++)
    {
       ostr << bblId << ":" << _summary_bblFreq[bblId] << " ";
    }
    ostr << std::endl;
    return ostr.str();
}

std::string BBLprofKernelProfile::GetGlobalBBVs() const
{
    std::ostringstream ostr;

    for (uint32_t bblId = 0; bblId != _global_bblFreq.size(); bblId++)
    {
       ostr << ":" << bblId+1 << ":" << _global_bblFreq[bblId] << " ";
    }
    ostr << std::endl;
    return ostr.str();
}

void BBLprofKernelProfile::DumpAsm() const
{
    DumpKernelAsmText(_name, _uniqueName, _asmText);
}

void BBLprofKernelProfile::ResetGlobalBBV()
{
    for (uint32_t bblId = 0; bblId != _global_bblFreq.size(); bblId++)
      _global_bblFreq[bblId] = 0;
}

void BBLprofKernelProfile::Accumulate(const BBLprofRecord& record, BblId bblId)
{
    GTPIN_ASSERT(bblId < _summary_bblFreq.size());
    _summary_bblFreq[bblId] += record.freq;
    _global_bblFreq[bblId] += record.freq;
}

/* ============================================================================================= */
// BBLprof implementation
/* ============================================================================================= */

BBLprof* BBLprof::Instance()
{
    static BBLprof instance;
    return &instance;
}

void BBLprof::OnKernelBuild(IGtKernelInstrument& instrumentor)
{
    const IGtKernel&           kernel    = instrumentor.Kernel();
    const IGtCfg&              cfg       = instrumentor.Cfg();
    const IGtGenCoder&         coder     = instrumentor.Coder();
    const IGtGenModel&         genModel  = kernel.GenModel();
    IGtProfileBufferAllocator& allocator = instrumentor.ProfileBufferAllocator();
    IGtVregFactory&            vregs     = coder.VregFactory();
    IGtInsFactory&             insF      = coder.InstructionFactory();

    if (!_threadbbv.is_open())
    {
      std::string outdir = knobBBDir;
      std::string tidstr = std::to_string(getpid());
      outdir=outdir+"."+tidstr;
      mkdir(outdir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
      _threadbbv.open(JoinPath(outdir, "thread.bbv"));
			_threadbbv << "M: SYS_init 1" << std::endl;
    }
    if (!_globalbbv.is_open())
    {
      std::string outdir = knobBBDir;
      std::string tidstr = std::to_string(getpid());
      outdir=outdir+"."+tidstr;
      mkdir(outdir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
      _globalbbv.open(JoinPath(outdir, "global.bbv"));
			_globalbbv	<< "M: SYS_init 1" << std::endl;
    }
	if (!_summarybbv.is_open())
	{
      std::string outdir = knobBBDir;
      std::string tidstr = std::to_string(getpid());
      outdir=outdir+"."+tidstr;
      mkdir(outdir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	  _summarybbv.open(JoinPath(outdir, "summary.bbv"));
	}

    if(cpu_on_kernel_build)
     {
      (*cpu_on_kernel_build)(kernel.Name().Get());
     }

    // Initialize virtual registers
    GtReg addrReg = vregs.MakeMsgAddrScratch(); ///< Virtual register that holds address within profile buffer

    // Allocate the profile buffer. It will hold single BBLprofRecord per each basic block in each thread bucket
    uint32_t numThreadBuckets = (knobNumThreadBuckets == 0) ? genModel.MaxThreadBuckets() : knobNumThreadBuckets;
    uint32_t numRecords = cfg.NumBbls();
    GtProfileArray profileArray(sizeof(BBLprofRecord), numRecords, numThreadBuckets);
    profileArray.Allocate(allocator);

    // Instrument basic blocks
    for (auto bblPtr : cfg.Bbls())
    {
        if (!bblPtr->IsEmpty())
        {
            GtGenProcedure proc;
            uint32_t recordNum = bblPtr->Id();

            // addrReg =  address of the current thread's BBLprofRecord in the profile buffer
            profileArray.ComputeAddress(coder, proc, addrReg, recordNum);

            // [addrReg].freq++
            proc += insF.MakeAtomicInc(NullReg(), addrReg, GED_DATA_TYPE_ud);

            if (!proc.empty()) { proc.front()->AppendAnnotation(__func__); }
            instrumentor.InstrumentBbl(*bblPtr, GtIpoint::Before(), proc);
        }
    }

    // Create BBLprofKernelProfile object that represents profile of this kernel
    _kernels.emplace(kernel.Id(), BBLprofKernelProfile(kernel, cfg, profileArray));
}

void BBLprof::OnKernelRun(IGtKernelDispatch& dispatcher)
{
    bool isProfileEnabled = false;

    const IGtKernel& kernel = dispatcher.Kernel();
    if(cpu_on_kernel_run)
    {
      (*cpu_on_kernel_run)(kernel.Name().Get());
    }

    GtKernelExecDesc execDesc; dispatcher.GetExecDescriptor(execDesc);
    if (IsKernelExecProfileEnabled(execDesc, kernel.GpuPlatform()))
    {
        auto it = _kernels.find(kernel.Id());

        if (it != _kernels.end())
        {
            IGtProfileBuffer*          buffer        = dispatcher.CreateProfileBuffer(); GTPIN_ASSERT(buffer);
            BBLprofKernelProfile&   kernelProfile = it->second;
            const GtProfileArray&      profileArray  = kernelProfile.GetProfileArray();
            if (profileArray.Initialize(*buffer))
            {
                isProfileEnabled = true;
            }
            else
            {
                GTPIN_ERROR_MSG("BBLPROF: " + std::string(kernel.Name()) + " : Failed to write into memory buffer");
            }
        }
    }
    dispatcher.SetProfilingMode(isProfileEnabled);
}

void BBLprof::OnKernelComplete(IGtKernelDispatch& dispatcher)
{
    const IGtKernel& kernel = dispatcher.Kernel();
	std::string kernel_name = kernel.Name().Get();
	if (_region_kernel_call.find(kernel_name) != _region_kernel_call.end()) {
		_region_kernel_call[kernel_name]++;
	}
	else {
		_region_kernel_call[kernel_name] =1;
	}

    if (!dispatcher.IsProfilingEnabled())
    {
        return; // Do nothing with unprofiled kernel dispatches
    }

    if(cpu_on_kernel_complete)
    {
      (*cpu_on_kernel_complete)(kernel.Name().Get());
    }

    auto it = _kernels.find(kernel.Id());

    if (it != _kernels.end())
    {
        const IGtProfileBuffer*  buffer        = dispatcher.GetProfileBuffer();
        GTPIN_ASSERT(buffer);
        BBLprofKernelProfile& kernelProfile = it->second;
        const GtProfileArray&  profileArray  = kernelProfile.GetProfileArray();

        kernelProfile.ResetGlobalBBV(); // zero out the global bbv counters
        if (_threadbbv.is_open())
        {
             //_threadbbv << "#Kernel: " << kernel.Name().Get() <<  std::endl;
						 _threadbbv << "# Slice ending at kernel: " << kernel.Name().Get() << " call: " << _region_kernel_call[kernel.Name().Get()] << std::endl;
        }
        for (uint32_t threadBucket = 0; threadBucket < profileArray.NumThreadBuckets(); ++threadBucket)
         {
           if (_threadbbv.is_open())
           {
             _threadbbv << "tid" << std::dec << threadBucket << ": T";
           }
          for (uint32_t recordNum = 0; recordNum != profileArray.NumRecords(); ++recordNum)
          {
            BBLprofRecord record;
            if (!profileArray.Read(*buffer, &record, recordNum, 1, threadBucket))
            {
              GTPIN_ERROR_MSG("BBLPROF: " + std::string(kernel.Name()) + " : Failed to read from memory buffer");
            }
          else
            {
              // Dump total profile data and assembly texts for all kernels
              if (_threadbbv.is_open())
              {
                _threadbbv << ":" << std::dec << recordNum + 1 << ":" << record.freq << " ";
              }
            kernelProfile.Accumulate(record, (BblId)recordNum); // will update both _summary_bbl_freq and _global_bbl_freq counters
            }
          }
           if (_threadbbv.is_open())
           {
             _threadbbv << std::endl;
           }
         }
				if (_threadbbv.is_open())
				{
					_threadbbv  << "M: " << std::string(kernel.Name()) << " " << std::to_string(_region_kernel_call[kernel.Name().Get()]) << std::endl;
				}

        // We have updated  _global_bbl_freq, output a global bbv now
        std::string asmText = kernelProfile.GetGlobalBBVs();
        asmText = "# Slice ending at kernel: " + std::string(kernel.Name()) + " call: " + std::to_string(_region_kernel_call[kernel.Name().Get()]) + "\nT" + asmText; // Assembly text of the current kernel
        if (_globalbbv.is_open())
        {
            _globalbbv	<< asmText;
						_globalbbv	<< "M: " << std::string(kernel.Name()) << " " << std::to_string(_region_kernel_call[kernel.Name().Get()]) << std::endl;
        }
   }
}

void BBLprof::DumpProfile() 
{
    std::ostringstream os;

    // Dynamic insruction counters of all kernels
    //BBLprofHistogram totalDynamicHistogram;
    std::string totalAsmText;           // Assembly texts of all kernels
    uint32_t    numKernels  = 0;        // Number of executed kernels

                                        // Dump per-kernel profile data and assembly text
    for (auto& k : _kernels)
    {
        const BBLprofKernelProfile& kernel = k.second;

        std::string asmText = kernel.GetSummaryBBVs();
        asmText = "# Summary: "+std::string(kernel.GetName())+"\n" + asmText; // Assembly text of the current kernel

        ++numKernels;
        totalAsmText           += asmText;
    }

    if (_summarybbv.is_open())
    {
        _summarybbv << "# Summary : Total number of kernels: " << std::dec << numKernels << std::endl;
        _summarybbv << std::endl << std::endl << totalAsmText;
    }
}

void BBLprof::DumpAsm() const
{
    for (auto& kernel : _kernels)
    {
        kernel.second.DumpAsm();
    }
}

void BBLprof::Fini()
{
   if(cpu_on_gpu_fini)
   {
    (*cpu_on_gpu_fini)();
   }

    DumpProfile();
    //DumpAsm();
}

/* ============================================================================================= */
// GTPin_Entry
/* ============================================================================================= */
EXPORT_C_FUNC void GTPin_Entry(int argc, const char* argv[])
{
    ConfigureGTPin(argc, argv);
    BBLprof::Instance()->Register();
    atexit(BBLprof::OnFini);
}

EXPORT_C_FUNC void GTPinShimRegisterCallbacks(void * ptrb, void * ptrr, void * ptrc, void * ptrf)
{
    cpu_on_kernel_build = (cpu_on_kernel_build_ptr_t) ptrb;
    cpu_on_kernel_run = (cpu_on_kernel_run_ptr_t) ptrr;
    cpu_on_kernel_complete = (cpu_on_kernel_complete_ptr_t) ptrc;
    cpu_on_gpu_fini = (cpu_on_gpu_fini_ptr_t) ptrf;
}

