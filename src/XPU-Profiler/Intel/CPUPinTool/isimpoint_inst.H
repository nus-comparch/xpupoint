/*BEGIN_LEGAL 
BSD License 

Copyright (c)2022 Intel Corporation. All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */

#ifndef ISIMPOINT_INST_H
#define ISIMPOINT_INST_H

#include <map>
#include <unordered_set>
#include <queue>
#include <iostream>
#include <fstream>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>


#include "pin.H"
#include "reuse_distance.H"
#include "filter.mod.H"
using namespace FILTER;


#define ISIMPOINT_MAX_IMAGES 64
#define ADDRESS64_MASK (~63)


/*
    When to emit frequency (bbv/ldv) vectors?
    Two knobs control this:
    1. -emit_vectors
        Default 1.
        If set to 0, some external tool (such as looppoint) will trigger
         vector emission.
    2. -delay_vector_emission
        Added to ease marker management in PC+Count regions. 
          (Iregions behavior un-affected.)
        Default 1 :
            Emission happen at the beginning of the next basic block
            following the end of slice is detected.
        If set to 0, we have the old way of outputting vectors as soon
         as slice end is detected.
*/
class IMG_INFO
{
  public:
    IMG_INFO(IMG img);
    ~IMG_INFO();
    INT32 Id() { return _imgId; }
    CHAR* Name() { return _name; }
    ADDRINT LowAddress() { return _low_address; }

  private:
    CHAR* _name;
    ADDRINT _low_address;
    INT32 _imgId;
};

class IMG_MANAGER
{
  private:
    std::map<INT32, IMG_INFO*> _img_info;
    PIN_LOCK _imagesLock;

  public:
    IMG_MANAGER() { PIN_InitLock(&_imagesLock); }

    VOID AddImage(IMG img)
    {
        PIN_GetLock(&_imagesLock, 1);
        _img_info[IMG_Id(img)] = new IMG_INFO(img);
        PIN_ReleaseLock(&_imagesLock);
    }
    IMG_INFO* GetImageInfo(INT32 id)
    {
        IMG_INFO* imageInfo = NULL;
        PIN_GetLock(&_imagesLock, 1);
        if (_img_info.find(id) != _img_info.end())
            imageInfo = _img_info[id];
        PIN_ReleaseLock(&_imagesLock);
        return imageInfo;
    }
};

class BLOCK_KEY
{
    friend BOOL operator<(const BLOCK_KEY& p1, const BLOCK_KEY& p2);

  public:
    BLOCK_KEY(ADDRINT s, ADDRINT e, USIZE z) : _start(s), _end(e), _size(z) {};
    BOOL IsPoint() const { return (_start - _end) == 1; }
    ADDRINT Start() const { return _start; }
    ADDRINT End() const { return _end; }
    USIZE Size() const { return _size; }
    BOOL Contains(ADDRINT addr) const;

  private:
    const ADDRINT _start;
    const ADDRINT _end;
    const USIZE _size;
};

class PROFILE;
class ISIMPOINT;

typedef std::map<INT32, INT64> BLOCK_COUNT_MAP;
typedef enum
{
    LDV_TYPE_NONE        = 0,
    LDV_TYPE_APPROXIMATE = 1,
    LDV_TYPE_EXACT       = 2
} LDV_TYPE;

class LDV
{
  private:
    static const UINT32 MAX_BINS = 30;
    RD* _rd;
    std::vector<UINT64> _counts;

  public:
    LDV(LDV_TYPE type) : _counts(MAX_BINS + 1, 0)
    {
        if (type == LDV_TYPE_APPROXIMATE)
            _rd = new RD_LogRR();
        else if (type == LDV_TYPE_EXACT)
            _rd = new RD_Treap();
        else
            _rd = NULL;
    }
    ~LDV()
    {
        if (_rd)
            delete _rd;
    }

    VOID emit(std::ofstream& LdvFile)
    {
        for (UINT64 bin = 0; bin <= MAX_BINS; ++bin)
        {
            UINT64 value = _counts[bin];
            if (value)
                LdvFile << ":" << std::dec << bin << ":" << std::dec << value << " ";
            _counts[bin] = 0;
        }
    }

    VOID clear(std::ofstream& LdvFile)
    {
        LdvFile << "# ClearedBBV" << std::endl;
        for (UINT64 bin = 0; bin <= MAX_BINS; ++bin)
        {
            _counts[bin] = 0;
        }
    }

    VOID access(ADDRINT address)
    {
        ASSERTX(_rd);
        UINT32 dist_log2 = _rd->reference(address);
        if (dist_log2 > MAX_BINS)
            dist_log2 = MAX_BINS;
        ++_counts[dist_log2];
    }
};

class BLOCK
{
  public:
    BLOCK(const BLOCK_KEY& key, INT64 instructionCount, INT32 id, INT32 imgId, UINT32 nthreads);
    ~BLOCK();
    INT64 StaticInstructionCount() const { return _staticInstructionCount; }
    VOID Execute(THREADID tid) { _sliceBlockCount[tid]++; }
    VOID Clear(THREADID tid) { _sliceBlockCount[tid] = 0; }
    VOID Execute(THREADID tid, const BLOCK* prev_block, ISIMPOINT* isimpoint);
    VOID EmitSliceEnd(THREADID tid, PROFILE* profile);
    INT64 CumulativeBlockCount(THREADID tid) const
    {
        return _cumulativeBlockCount[tid] + _sliceBlockCount[tid];
    }
    UINT32 ImgId() const { return _imgId; }
    const BLOCK_KEY& Key() const { return _key; }
    INT32 Id() const { return _id; }

  private:
    INT64 SliceInstructionCount(THREADID tid) const
    {
        return _sliceBlockCount[tid] * _staticInstructionCount;
    }

    const INT64 _staticInstructionCount; // number of instrs in this block.
    INT32 _id;
    UINT32 _imgId;
    const BLOCK_KEY _key;

    INT64* _sliceBlockCount;
    // times this block was executed in the current slice.
    INT64* _cumulativeBlockCount;
    // times this block was executed prior to the current slice.
    BLOCK_COUNT_MAP* _blockCountMap;
    // counter for each previous block.
};

typedef std::pair<BLOCK_KEY, BLOCK*> BLOCK_PAIR;
typedef std::map<BLOCK_KEY, BLOCK*> BLOCK_MAP;

class PROFILE
{
  private:
    static const UINT32 BUFSIZE = 100;

  public:
    PROFILE(INT64 slice_size, LDV_TYPE ldv_type) : _ldvState(ldv_type)
    {
        first                      = true;
        last                       = false;
        active                     = false;
        first_eip                  = 0;
        first_eip_imgID            = 0;
        CumulativeInstructionCount = 0;
        SliceTimer                 = slice_size; 
        CurrentSliceSize           = slice_size;
        last_block                 = NULL;
    }
    VOID OpenFile(THREADID tid, UINT32 pid,  std::string output_file, BOOL enable_ldv)
    {
        if (!BbFile.is_open())
        {
            char num[100];
            if (pid)
            {
                sprintf_s(num, sizeof(num), "T.%u.%d", (unsigned)pid, (int)tid);
            }
            else
            {
                sprintf_s(num, sizeof(num), "T.%d", (int)tid);
            }
            std::string tname = num;
            BbFile.open((output_file + tname + ".bb").c_str());
            BbFile.setf(std::ios::showbase);

            if (enable_ldv)
            {
                LdvFile.open((output_file + tname + ".ldv").c_str());
            }
        }
    }
    VOID ExecuteMemory(ADDRINT address) { _ldvState.access(address & ADDRESS64_MASK); }
    VOID EmitLDV() { _ldvState.emit(LdvFile); }
    VOID ClearLDV() { _ldvState.clear(LdvFile); }

    std::ofstream BbFile;
    std::ofstream LdvFile;
    INT64 CumulativeInstructionCount;
    // The first time, we want a marker, but no T vector
    ADDRINT first_eip;
    UINT32 first_eip_imgID;
    BOOL first;
    BOOL last;
    BOOL active;
    // Emit the first marker immediately
    INT64 SliceTimer;
    INT64 CurrentSliceSize;
    BLOCK* last_block;
    LDV _ldvState;
};

class ISIMPOINT
{
    std::string commandLine;
    IMG_MANAGER img_manager;
    BLOCK_MAP block_map;
    THREADID* _currentId;
    FILTER_MOD *_filterptr;

  public:
    ISIMPOINT();

    INT32 Usage()
    {
        std::cerr << "This tool collects frequency vectors  for SimPoint.\n"
                     "\n";
        std::cerr << KNOB_BASE::StringKnobSummary() << std::endl;
        return -1;
    }

    std::string CommandLine() { return commandLine; }

    BLOCK_MAP* BlockMapPtr() { return &block_map; }

    IMG_MANAGER* ImageManager() { return &img_manager; }

    UINT32 GetNumThreads() { return _nthreads; }

    BOOL IsThreadOfInterest(THREADID tid) { return ((KnobFocusThread.Value() == -1) || (KnobFocusThread.Value() == (INT32) tid)); }

    VOID EmitBBV(THREADID tid, std::string kname, uint64_t call_count)
    {   
        if(!IsThreadOfInterest(tid)) return;
        if (profiles[tid]->first == true)
        {
            // Input merging will change the name of the input
            profiles[tid]->BbFile << "I: 0" << std::endl;
            profiles[tid]->BbFile << "P: " << std::dec << tid << std::endl;
            profiles[tid]->BbFile << "C: sum:dummy Command:" << commandLine << std::endl;
            profiles[tid]->BbFile << "# Program Start" << std::endl;
        }

        if (!profiles[tid]->first || KnobEmitFirstSlice)
        {
            profiles[tid]->BbFile << "# Slice ending at kernel: " << kname << " call: " << call_count << "\n";
            profiles[tid]->BbFile << "T";
        }

        for (BLOCK_MAP::const_iterator bi = BlockMapPtr()->begin(); bi != BlockMapPtr()->end();
             bi++)
        {
            BLOCK* block         = bi->second;
            if (!profiles[tid]->first || KnobEmitFirstSlice)
                block->EmitSliceEnd(tid, profiles[tid]);
        }

        if (!profiles[tid]->first || KnobEmitFirstSlice)
            profiles[tid]->BbFile << std::endl;

        if (_ldv_type != LDV_TYPE_NONE)
        {
            if (!profiles[tid]->first || KnobEmitFirstSlice)
            {
                profiles[tid]->LdvFile << "T";
                profiles[tid]->EmitLDV();
                profiles[tid]->LdvFile << std::endl;
            }
        }
        profiles[tid]->BbFile.flush();
        profiles[tid]->first = false;
    }


    VOID ClearBBV(THREADID tid, std::string kname)
    {   
        if(!IsThreadOfInterest(tid)) return;
        profiles[tid]->BbFile << "# ClearedBBV first on_run :"<< kname << std::endl;
        for (BLOCK_MAP::const_iterator bi = BlockMapPtr()->begin(); bi != BlockMapPtr()->end();
             bi++)
        {
            BLOCK* block         = bi->second;
            block->Clear(tid);
        }
        if (_ldv_type != LDV_TYPE_NONE)
        {
            if (!profiles[tid]->first || KnobEmitFirstSlice)
            {
                profiles[tid]->ClearLDV();
            }
        }
        profiles[tid]->BbFile.flush();
    }

    static ADDRINT CountBlock_If(BLOCK* block, THREADID tid, ISIMPOINT* isimpoint)
    {
        block->Execute(tid);

        isimpoint->profiles[tid]->SliceTimer -= block->StaticInstructionCount();
        isimpoint->profiles[tid]->last_block = block;

        return (isimpoint->profiles[tid]->SliceTimer < (INT64)0);
    }

    static ADDRINT CountBlockAndTrackPrevious_If(BLOCK* block, THREADID tid,
                                                 ISIMPOINT* isimpoint)
    {
        block->Execute(tid, isimpoint->profiles[tid]->last_block, isimpoint);

        isimpoint->profiles[tid]->SliceTimer -= block->StaticInstructionCount();
        isimpoint->profiles[tid]->last_block = block;

        return (isimpoint->profiles[tid]->SliceTimer < 0);
    }

    static VOID ResetSliceTimer(THREADID tid, ISIMPOINT* isimpoint)
    {
        if(!isimpoint->IsThreadOfInterest(tid)) return;
        isimpoint->profiles[tid]->CumulativeInstructionCount +=
                (isimpoint->profiles[tid]->CurrentSliceSize -
                 isimpoint->profiles[tid]->SliceTimer);
        isimpoint->profiles[tid]->SliceTimer       = isimpoint->KnobSliceSize;
        isimpoint->profiles[tid]->CurrentSliceSize = isimpoint->profiles[tid]->SliceTimer;
    }

    static VOID CountBlock_Then(BLOCK* block, THREADID tid, ISIMPOINT* isimpoint)
    {
        if (!isimpoint->KnobEmitVectors)
        {
            // do not output frequency vector but set a flag indicating
            // vector output is pending. The vector output will be
            // triggered by another class.
            //isimpoint->_vectorPending[tid] = TRUE;
        }
        else
        {
            isimpoint->ResetSliceTimer(tid, isimpoint);
        }
    }

    VOID EmitVectorForFriend(THREADID tid, ISIMPOINT* isimpoint, std::string kname, uint64_t call_count)
    {
        isimpoint->EmitBBV(tid, kname, call_count);
        isimpoint->ResetSliceTimer(tid, isimpoint);
    }

    // Lookup a block by its id.
    // Return block_map.end() if not found.
    BLOCK_MAP::const_iterator LookupBlock(INT32 id)
    {
        BLOCK_MAP::const_iterator bi = BlockMapPtr()->begin();
        for (; bi != BlockMapPtr()->end(); bi++)
        {
            if (bi->second->Id() == id)
                return bi;
        }
        return bi;
    }

    // Lookup a block by its BBL key.
    // Create a new one and return it if it doesn't already exist.
    BLOCK* LookupBlock(BBL bbl)
    {
        BLOCK_KEY key(INS_Address(BBL_InsHead(bbl)), INS_Address(BBL_InsTail(bbl)),
                      BBL_Size(bbl));
        BLOCK_MAP::const_iterator bi = BlockMapPtr()->find(key);

        if (bi == BlockMapPtr()->end())
        {
            // Block not there, add it
            RTN rtn = INS_Rtn(BBL_InsHead(bbl));
            SEC sec = SEC_Invalid();
            IMG img = IMG_Invalid();
            if (RTN_Valid(rtn))
                sec = RTN_Sec(rtn);
            if (SEC_Valid(sec))
                img = SEC_Img(sec);

            INT32 id = 0;
            id = _currentId[0]++;
            BLOCK* block = new BLOCK(key, BBL_NumIns(bbl), id, IMG_Id(img), GetNumThreads());

            BlockMapPtr()->insert(BLOCK_PAIR(key, block));

            return block;
        }
        else
        {
            return bi->second;
        }
    }

    static VOID CountMemory(ADDRINT address, THREADID tid, ISIMPOINT* isimpoint)
    {
        isimpoint->profiles[tid]->ExecuteMemory(address);
    }

    static VOID Trace(TRACE trace, VOID* v)
    {
        ISIMPOINT* isimpoint = reinterpret_cast<ISIMPOINT*>(v);

        if ( getpid() != (pid_t)isimpoint->RootPid) return; // only instrument the main process

        if(isimpoint->_filterptr)
        {
          if(!isimpoint->_filterptr->SelectTrace(trace)) return;
        }


        for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
        {
              // find the block in the map or add it if new.
              BLOCK* block = isimpoint->LookupBlock(bbl);

              INS_InsertIfCall(BBL_InsTail(bbl), IPOINT_BEFORE, (AFUNPTR)CountBlock_If,
                                 IARG_PTR, block, IARG_THREAD_ID, IARG_PTR, isimpoint,
                                 IARG_END);
              INS_InsertThenCall(BBL_InsTail(bbl), IPOINT_BEFORE, (AFUNPTR)CountBlock_Then,
                               IARG_PTR, block, IARG_THREAD_ID, IARG_PTR, isimpoint, IARG_END);

              ISIMPOINT* isimpoint = reinterpret_cast<ISIMPOINT*>(v);
              if (isimpoint->_ldv_type != LDV_TYPE_NONE)
              {
                for (INS ins = BBL_InsHead(bbl);; ins = INS_Next(ins))
                {
                    if (INS_IsMemoryRead(ins) || INS_IsMemoryWrite(ins))
                    {
                        for (UINT32 i = 0; i < INS_MemoryOperandCount(ins); i++)
                            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)CountMemory,
                                           IARG_MEMORYOP_EA, i, IARG_THREAD_ID, IARG_PTR,
                                           isimpoint, IARG_END);
                    }

                    if (ins == BBL_InsTail(bbl))
                        break;
                }
              }
        }
    }

    static VOID Image(IMG img, VOID* v)
    {
        ISIMPOINT* isimpoint = reinterpret_cast<ISIMPOINT*>(v);

        if ( getpid() != (pid_t)isimpoint->RootPid) return; // only instrument the main process
        isimpoint->profiles[0]->OpenFile(0, isimpoint->Pid, isimpoint->KnobOutputFile.Value(),
                                         isimpoint->_ldv_type != LDV_TYPE_NONE);
        isimpoint->img_manager.AddImage(img);
        isimpoint->profiles[0]->BbFile << "G: " << IMG_Name(img) << " LowAddress: " << std::hex
                                       << IMG_LowAddress(img) << " LoadOffset: " << std::hex
                                       << IMG_LoadOffset(img) << std::endl;

    }


    static VOID AfterForkInChild(THREADID threadid, const CONTEXT* ctxt, VOID* v)
    {
        ISIMPOINT* isimpoint = reinterpret_cast<ISIMPOINT*>(v);
        if (KnobPid)
        {
            isimpoint->Pid = getpid();
        }
        /*
        std::cerr << "TOOL: After fork in child: threadid " << threadid 
             << " PIN_GetTid() " << PIN_GetTid()
             << " gitpid() " << isimpoint->Pid
             << std::endl;
        */
// The fork subroutine duplicates the parent process, but duplicates only the 
// calling thread; the child process is a single-threaded process. The calling thread 
// of the parent process becomes the initial thread of the child process; it may not 
// be the initial thread of the parent process.

//Close all parent's thread files
        for (UINT32 i = 0; i < isimpoint->_nthreads; i++)
        {
            isimpoint->profiles[i]->BbFile.close();
            isimpoint->profiles[threadid]->active = false;
        }
#if 0
//Open for this thread in  the child
        isimpoint->profiles[threadid]->OpenFile(threadid, isimpoint->Pid, isimpoint->KnobOutputFile.Value(),
                                           isimpoint->_ldv_type != LDV_TYPE_NONE);
        isimpoint->profiles[threadid]->active = true;
#endif 
      // we have set 'active' flag to false for all threads. This should disable all instrumentaion after the call below
        PIN_RemoveInstrumentation();
    }

    static BOOL FollowChildProcess(CHILD_PROCESS cProcess, VOID *v )
    {
        //ISIMPOINT* isimpoint = reinterpret_cast<ISIMPOINT*>(v);
        INT cargc;
        CHAR **cargv;
        CHILD_PROCESS_GetCommandLine( cProcess, &cargc, (const CHAR* const**)&cargv);

        std::cerr << "TOOL:  FollowChildProcess  " << cargv[0] 
             << "PIN_GetTid() " << PIN_GetTid()
             << "gitpid() " <<  getpid()
             << std::endl;
         // We are *not* instrumenting the execed execution.
        return FALSE;
    }


    static VOID ThreadStart(THREADID tid, CONTEXT* ctxt, INT32 flags, VOID* v)
    {
        ISIMPOINT* isimpoint = reinterpret_cast<ISIMPOINT*>(v);
        if ( getpid() != (pid_t)isimpoint->RootPid) return; // only instrument the main process
        if(!isimpoint->IsThreadOfInterest(tid)) return;
        if(!isimpoint->profiles[tid]) return;
        ASSERT(tid < isimpoint->GetNumThreads(),
               "Use knob -bbthreads to increase number of threads");

        isimpoint->profiles[tid]->OpenFile(tid, isimpoint->Pid, isimpoint->KnobOutputFile.Value(),
                                           isimpoint->_ldv_type != LDV_TYPE_NONE);
        isimpoint->profiles[tid]->active = true;
        PIN_RemoveInstrumentation();
    }

    static VOID ThreadFini(UINT32 tid, const CONTEXT* ctxt, INT32 code, VOID* v)
    {
        ISIMPOINT* isimpoint = reinterpret_cast<ISIMPOINT*>(v);
        if ( getpid() != (pid_t)isimpoint->RootPid) return; // only instrument the main process
        if(!isimpoint->IsThreadOfInterest(tid)) return;
        if(!isimpoint->profiles[tid]) return;
        if(!isimpoint->profiles[tid]->active) return;

        if (isimpoint->KnobEmitLastSlice &&
            isimpoint->profiles[tid]->SliceTimer != isimpoint->profiles[tid]->CurrentSliceSize)
        {
            if (isimpoint->KnobEmitVectors)
            {
                isimpoint->profiles[tid]->last = true; // this is the last slice
                isimpoint->ResetSliceTimer(tid, isimpoint);
            }
        }
        isimpoint->EmitProgramEnd(tid, isimpoint);
        isimpoint->profiles[tid]->active = false;
        isimpoint->profiles[tid]->BbFile.close();
    }

    VOID GetCommand(int argc, char* argv[])
    {
        for (INT32 i = 0; i < argc; i++)
        {
            commandLine += " ";
            commandLine += argv[i];
        }
    }

    virtual VOID activate(int argc, char** argv, FILTER_MOD *filter)
    {
        _filterptr = filter;
        if (isimpoint_knob)
        {
            if (KnobLDVType.Value() == "none")
                _ldv_type = LDV_TYPE_NONE;
            else if (KnobLDVType.Value() == "approx")
                _ldv_type = LDV_TYPE_APPROXIMATE;
            else if (KnobLDVType.Value() == "exact")
                _ldv_type = LDV_TYPE_EXACT;
            else
                ASSERT(0, "Invalid ldv_type: " + KnobLDVType.Value());
            Pid = getpid();
            RootPid = getpid();
            std::cerr << "[XPU_TRACER]["<< __FUNCTION__ <<" Only instrumenting CPU Pid " << RootPid << std::endl;
            if (KnobFocusThread.Value() != -1)
              std::cerr << "[XPU_TRACER]["<< __FUNCTION__ <<" Only instrumenting CPU tid " << KnobFocusThread << std::endl;
            AddInstrumentation(argc, argv);
            std::string outdir = KnobOutputFile.Value();
            if (mkdir(outdir.c_str(), 0777) == -1)
            {
              std::cerr << "Unable to create directory " << outdir << std::endl;
              if(errno == EEXIST)
              {
                      std::cerr << ": directory " << outdir << " already exists" << std::endl;
              }
            }
        }
    }

    BOOL ParseFilenameTid(const std::string& str, std::string* fn, UINT32* tidp)
    {
        size_t tidpos = str.find(":tid");
        if (tidpos == std::string::npos)
            return FALSE;
        std::string tidstr = str.substr(tidpos + 4);
        *fn                = str.substr(0, tidpos);
        *tidp              = Uint32FromString(tidstr);
        return TRUE;
    }

    VOID AddInstrumentation(int argc, char* argv[])
    {
        GetCommand(argc, argv);

        // forks a new process.
        PIN_AddForkFunction(FPOINT_AFTER_IN_CHILD, AfterForkInChild, this);

        // excec in  a new process.
        PIN_AddFollowChildProcessFunction(FollowChildProcess, this);

        PIN_AddThreadStartFunction(ThreadStart, this);
        PIN_AddThreadFiniFunction(ThreadFini, this);

        // Cannot read knobs value in the constructor because it is static object
        // and the constructor is called before parsing the command line
        // TODO: In replay we can get the number of threads from the replayer
        _nthreads = KnobNumThreads.Value();
        ASSERTX(_nthreads < PIN_MAX_THREADS);

        profiles = new PROFILE*[_nthreads];
        memset(profiles, 0, _nthreads * sizeof(profiles[0]));

        for (THREADID tid = 0; tid < _nthreads; tid++)
        {
            profiles[tid] = new PROFILE(KnobSliceSize, _ldv_type);
        }

        _currentId = new THREADID[_nthreads];
        for (UINT32 i = 0; i < _nthreads; i++)
            _currentId[i] = 1;

        TRACE_AddInstrumentFunction(Trace, this);
        IMG_AddInstrumentFunction(Image, this);
    }

    VOID EmitProgramEnd(THREADID tid, ISIMPOINT* isimpoint)
    {
        if(!isimpoint->IsThreadOfInterest(tid)) return;
        if(!isimpoint->profiles[tid]) return;
        if(!isimpoint->profiles[tid]->active) return;
        isimpoint->EmitBBV(tid, "SYS_exit", 1);
         isimpoint->ResetSliceTimer(tid, isimpoint);
         isimpoint->profiles[tid]->BbFile << "# Program End\n ";
         isimpoint->profiles[tid]->BbFile.close();
         //std::cerr << "[XPU_TRACER]["<< __FUNCTION__ <<" Dynamic instruction count on CPU Pid " << isimpoint->Pid << " tid " << tid << ":" <<  isimpoint->profiles[tid]->CumulativeInstructionCount << std::endl;
    }

    // read-only accessor.
    THREADID getCurrentId(THREADID tid) const { return _currentId[tid]; }

    static KNOB_COMMENT knob_family;
    static KNOB<BOOL> isimpoint_knob;
    static KNOB<std::string> KnobOutputFile;
    static KNOB<INT64> KnobSliceSize;
    static KNOB<BOOL> KnobEmitVectors;
    static KNOB<BOOL> KnobEmitFirstSlice;
    static KNOB<BOOL> KnobEmitLastSlice;
    static KNOB<BOOL> KnobPid;
    static KNOB<std::string> KnobLDVType;
    static KNOB<UINT32> KnobNumThreads;
    static KNOB<INT32> KnobFocusThread;
    LDV_TYPE _ldv_type;
    UINT32 _nthreads;
    UINT32 Pid;
    UINT32 RootPid;
    PROFILE** profiles;
};

#endif
