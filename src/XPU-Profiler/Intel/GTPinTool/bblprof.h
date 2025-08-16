/*========================== begin_copyright_notice ============================
Copyright (C) 2018-2022 Intel Corporation

SPDX-License-Identifier: MIT
============================= end_copyright_notice ===========================*/

/*!
 * @file xpu-sampler tool definitions
 */

#ifndef BBLPROF_H_
#define BBLPROF_H_

#include <map>
#include <vector>
#include <string>
#include <tuple>

#include "gtpin_api.h"
#include "gtpin_tool_utils.h"

using namespace gtpin;

/*!
 * Layout of data records collected by the BBLprof tool for each basic block 
 */
struct BBLprofRecord
{
    uint32_t freq; ///< Total number of BBL executions
};

/* ============================================================================================= */
// Class BBLprofKernelProfile
/* ============================================================================================= */
/*!
 * Aggregated profile of all instrumented kernel dispatches
 */
class BBLprofKernelProfile
{
public:
    BBLprofKernelProfile(const IGtKernel& kernel, const IGtCfg& cfg, const GtProfileArray& profileArray);

    std::string           GetName()             const { return _name; }         ///< @return Kernel's name
    std::string           GetUniqueName()       const { return _uniqueName; }   ///< @return Kernel's unique name
    const GtProfileArray& GetProfileArray()     const { return _profileArray; } ///< @return Profile buffer accessor

    void                  DumpAsm()             const;  ///< Dump kernel's assembly text to file
    void ResetGlobalBBV() ;
    std::string GetGlobalBBVs() const;
    std::string GetSummaryBBVs() const;

    /// Accumulate profile counters collected in the specified BBL
    void Accumulate(const BBLprofRecord& record, BblId bblId);

private:
    std::string                         _name;              ///< Kernel's name
    std::string                         _uniqueName;        ///< Kernel's unique name
    std::string                         _asmText;           ///< Kernel's assembly text
    std::map<InsId, std::string>        _asmLines;          ///< Instrucitons assembly, indexed by instruction ID

    GtProfileArray                      _profileArray;      ///< Profile buffer accessor
    std::vector<uint64_t>               _summary_bblFreq;           ///< BBL execution counters, indexed by BBL ID
    std::vector<uint64_t>               _global_bblFreq;           ///< BBL execution counters, indexed by BBL ID
};

/* ============================================================================================= */
// Class BBLprof
/* ============================================================================================= */
/*!
 * Implementation of the IGtTool interface for the BBLprof tool
 */
class BBLprof : public GtTool
{
public:
    /// Implementation of the IGtTool interface
    const char* Name() const { return "BBLprof"; }

    void OnKernelBuild(IGtKernelInstrument& instrumentor);
    void OnKernelRun(IGtKernelDispatch& dispatcher);
    void OnKernelComplete(IGtKernelDispatch& dispatcher);

public:

    static BBLprof* Instance();               ///< @return Single instance of this class
    static void OnFini() { Instance()->Fini(); } ///< Callback function registered with atexit()

private:
    BBLprof() = default;
    BBLprof(const BBLprof&) = delete;
    BBLprof& operator = (const BBLprof&) = delete;
    ~BBLprof() = default;

    void DumpProfile(); ///< Dump text representation of the profile data
    void DumpAsm()     const; ///< Dump assembly text of profiled kernels to files
    void Fini();              /// Post process and dump profiling data

private:
    std::map<GtKernelId, BBLprofKernelProfile> _kernels;  ///< Collection of kernel profiles
	std::map<std::string, uint64_t> _region_kernel_call; // regionid->kernel,call
    std::ofstream _threadbbv;
    std::ofstream _globalbbv;
	std::ofstream _summarybbv;
};

#endif
