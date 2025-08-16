/*BEGIN_LEGAL 
BSD License 

Copyright (c)2022 Intel Corporation. All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */

#include "isimpoint_inst.H"

IMG_INFO::IMG_INFO(IMG img)
{
    _imgId = IMG_Id(img);
    _name  = (CHAR*)calloc(IMG_Name(img).size() + 1, 1);
    ASSERTX(_name != 0);
    strcpy_s(_name, IMG_Name(img).size() + 1, IMG_Name(img).c_str());
    _low_address = IMG_LowAddress(img);
}

IMG_INFO::~IMG_INFO()
{
    if (_name)
    {
        free(_name);
        _name = nullptr;
    }
}

VOID BLOCK::Execute(THREADID tid, const BLOCK* prev_block, ISIMPOINT* isimpoint)
{
    _sliceBlockCount[tid]++;
}

VOID BLOCK::EmitSliceEnd(THREADID tid, PROFILE* profile)
{
    if (_sliceBlockCount[tid] == 0)
        return;

    profile->BbFile << ":" << std::dec << Id() << ":" << std::dec << SliceInstructionCount(tid)
                    << " ";
    _cumulativeBlockCount[tid] += _sliceBlockCount[tid];
    _sliceBlockCount[tid] = 0;
}

BOOL operator<(const BLOCK_KEY& p1, const BLOCK_KEY& p2)
{
    if (p1.IsPoint())
        return p1._start < p2._start;

    if (p2.IsPoint())
        return p1._end <= p2._start;

    if (p1._start == p2._start)
        return p1._end < p2._end;

    return p1._start < p2._start;
}

BOOL BLOCK_KEY::Contains(ADDRINT address) const
{
    if (address >= _start && address <= _end)
        return true;
    else
        return false;
}

/* ===================================================================== */
BLOCK::BLOCK(const BLOCK_KEY& key, INT64 instructionCount, INT32 id, INT32 imgId,
             UINT32 nthreads)
    : _staticInstructionCount(instructionCount), _id(id), _imgId(imgId), _key(key), _blockCountMap(NULL)
{
    _sliceBlockCount      = new INT64[nthreads];
    _cumulativeBlockCount = new INT64[nthreads];
    for (THREADID tid = 0; tid < nthreads; tid++)
    {
        _sliceBlockCount[tid]      = 0;
        _cumulativeBlockCount[tid] = 0;
    }
}

BLOCK::~BLOCK()
{
    if (_blockCountMap)
    {
        delete[] _blockCountMap;
        _blockCountMap = nullptr;
    }

    if (_sliceBlockCount)
    {
        delete[] _sliceBlockCount;
        _sliceBlockCount = nullptr;
    }

    if (_cumulativeBlockCount)
    {
        delete[] _cumulativeBlockCount;
        _cumulativeBlockCount = nullptr;
    }
}

// Static knobs
KNOB_COMMENT knob_family("pintool:isimpoint", "Basic block profile knobs");
KNOB<BOOL> ISIMPOINT::isimpoint_knob(KNOB_MODE_WRITEONCE, "pintool:isimpoint", "bbprofile",
                                     "0", "Activate bbprofile / isimpoint.");
KNOB<std::string> ISIMPOINT::KnobOutputFile(KNOB_MODE_OVERWRITE, "pintool:isimpoint", "o",
                                            "BasicBlocksCPU/", "specify bb file name");
KNOB<INT64> ISIMPOINT::KnobSliceSize(KNOB_MODE_WRITEONCE, "pintool:isimpoint", "slice_size",
                                     "100000000", "slice size in instructions");
KNOB<BOOL> ISIMPOINT::KnobEmitVectors(
    KNOB_MODE_WRITEONCE, "pintool:isimpoint", "emit_vectors", "1",
    "Emit frequency (bb/reuse-dist) vectors at the end of each slice.");
KNOB<BOOL> ISIMPOINT::KnobEmitFirstSlice(
    KNOB_MODE_WRITEONCE, "pintool:isimpoint", "emit_first", "1",
    "Emit the first interval (higher overhead to find out first IP)");
KNOB<BOOL>
    ISIMPOINT::KnobEmitLastSlice(KNOB_MODE_WRITEONCE, "pintool:isimpoint", "emit_last", "0",
                                 "Emit the last interval even if it is less than slice_size");
KNOB<BOOL> ISIMPOINT::KnobPid(KNOB_MODE_WRITEONCE, "pintool:isimpoint", "pid", "1",
                              "Use PID for naming files.");
KNOB<std::string> ISIMPOINT::KnobLDVType(KNOB_MODE_WRITEONCE, "pintool:isimpoint", "ldv_type",
                                         "none",
                                         "Enable collection of LRU stack distance vectors "
                                         "(none(default), \"approx\", \"exact\" )");
KNOB<UINT32> ISIMPOINT::KnobNumThreads(KNOB_MODE_WRITEONCE, "pintool:isimpoint", "bbthreads",
                                       "512", "Maximal number of threads");
KNOB<INT32> ISIMPOINT::KnobFocusThread(KNOB_MODE_WRITEONCE, "pintool:isimpoint", "bbfocusthread",
                                       "-1", "Only profile this thread (default -1 => all threads");

ISIMPOINT::ISIMPOINT()
{
  _filterptr = NULL;
}
