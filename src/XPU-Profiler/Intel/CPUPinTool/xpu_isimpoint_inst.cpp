// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause
/*BEGIN_LEGAL 
BSD License 

Copyright (c)2022 Intel Corporation. All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */
#include "xpu_isimpoint_inst.H"


#if 0
VOID XPUBLOCK::ExecuteXpu(THREADID tid, const XPUBLOCK* prev_block,
   XPUISIMPOINT *gisimpoint)
{
    ATOMIC::OPS::Increment<INT64>
                (& _sliceBlockCountXpu._count, 1); 
    _sliceBlockCountThreads[tid]++;
    if (IdXpu() == 0)
      ASSERT(0,"IdXpu()==0 in ExecuteXpu() NYT "); 

    // Keep track of previous blocks and their counts only if we 
    // will be outputting them later.
    if (gisimpoint->KnobEmitPrevBlockCounts) {

        // The block "previous to" the first block is denoted by
        // the special ID zero (0).
        // It should always have a count of one (1).
        UINT32 prevBlockId = prev_block ? prev_block->IdXpu() : 0;

        // Automagically add hash keys for this tid and prevBlockID 
        // as needed and increment the counter.
        ATOMIC::OPS::Increment<INT64>
                (&(_blockCountMapXpu[prevBlockId])._count, 1); 
        ATOMIC::OPS::Increment<INT64>
                (&(_blockCountMapThreads[tid][prevBlockId])._count, 1); 
    }
}

VOID XPUBLOCK::EmitSliceEndXpu(XPUPROFILE *gprofile)
{
    if (_sliceBlockCountXpu._count == 0)
        return;
    
    gprofile->BbFile << ":" << std::dec << IdXpu() << ":" << std::dec 
        << SliceInstructionCountXpu() << " ";
    ATOMIC::OPS::Increment<INT64>
                (&_cumulativeBlockCountXpu._count, 
                _sliceBlockCountXpu._count); 
    _sliceBlockCountXpu._count = 0;
}

VOID XPUBLOCK::EmitSliceEndThread(THREADID tid, XPUPROFILE *profile)
{
    if (_sliceBlockCountThreads[tid] == 0)
        return;

    profile->BbFile << ":" << std::dec << IdXpu() << ":" << std::dec
        << SliceInstructionCountThread(tid) << " ";
    _cumulativeBlockCountThreads[tid] += _sliceBlockCountThreads[tid];
    _sliceBlockCountThreads[tid] = 0;
}


VOID XPUBLOCK::EmitProgramEndXpu(const BLOCK_KEY & key, 
    XPUPROFILE *gprofile, const XPUISIMPOINT *gisimpoint) const
{
    // If this block has the start address of the slice we need to emit it
    // even if it was not executed.
    BOOL force_emit = gisimpoint->FoundInStartSlices(key.Start());
    if (_cumulativeBlockCountXpu._count == 0 && !force_emit)
        return;
    
    gprofile->BbFile << "Block id: " << std::dec << IdXpu() << " " << std::hex 
        << key.Start() << ":" << key.End() << std::dec
        << " static instructions: " << StaticInstructionCount()
        << " block count: " << _cumulativeBlockCountXpu._count
        << " block size: " << key.Size();

    // Output previous blocks and their counts only if enabled.
    // Example: previous-block counts: ( 3:1 5:13 7:3 )
    if (gisimpoint->KnobEmitPrevBlockCounts) {
        gprofile->BbFile << " previous-block counts: ( ";

        // output block-id:block-count pairs.
        for (BLOCK_COUNT_MAP_XPU::const_iterator bci = 
              _blockCountMapXpu.begin();
             bci != _blockCountMapXpu.end();
             bci++) {
            gprofile->BbFile << bci->first << ':' << bci->second._count << ' ';
        }
        gprofile->BbFile << ')';
    }
    gprofile->BbFile << std::endl;
}

VOID XPUBLOCK::EmitProgramEndThread(const BLOCK_KEY & key, THREADID tid, 
    XPUPROFILE *gprofile, const XPUISIMPOINT *gisimpoint) const
{
    // If this block has the start address of the slice we need to emit it
    // even if it was not executed.
    BOOL force_emit = gisimpoint->FoundInStartSlices(key.Start());
    if (_cumulativeBlockCountThreads[tid] == 0 && !force_emit)
        return;
    
    gprofile->BbFile << "Block id: " << std::dec << IdXpu() << " " << std::hex 
        << key.Start() << ":" << key.End() << std::dec
        << " static instructions: " << StaticInstructionCount()
        << " block count: " << _cumulativeBlockCountThreads[tid]
        << " block size: " << key.Size();

    // Output previous blocks and their counts only if enabled.
    // Example: previous-block counts: ( 3:1 5:13 7:3 )
    if (gisimpoint->KnobEmitPrevBlockCounts) {
        gprofile->BbFile << " previous-block counts: ( ";

        // output block-id:block-count pairs.
        for (BLOCK_COUNT_MAP_XPU::const_iterator bci = _blockCountMapThreads[tid].begin();
             bci != _blockCountMapThreads[tid].end();
             bci++) {
            gprofile->BbFile << bci->first << ':' << bci->second._count << ' ';
        }
        gprofile->BbFile << ')';
    }
    gprofile->BbFile << std::endl;
}

// Static knobs
KNOB<BOOL> XPUISIMPOINT::KnobXpu (KNOB_MODE_WRITEONCE,  
    "pintool:isimpoint",
    "xpu_profile", "0", "Create a xpu bbv file");
KNOB<INT32> XPUISIMPOINT::KnobThreadProgress(KNOB_MODE_WRITEONCE,  
    "pintool:isimpoint",
    "thread_progress", "0", "N: number of threads: end xpu slice whenever any thread reaches 1/N th slice-size.");
#endif
