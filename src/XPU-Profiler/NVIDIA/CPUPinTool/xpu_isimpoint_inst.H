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
#ifndef XPU_ISIMPOINT_INST_H
#define XPU_ISIMPOINT_INST_H

using namespace std;
#include "isimpoint_inst.H"
#include "atomic.hpp"

#define LOCALTYPE 


class XPUPROFILE;
class XPUISIMPOINT;


class XPUPROFILE : public PROFILE
{
    public: 
    XPUPROFILE(INT64 slice_size, LDV_TYPE ldv_type)
      : PROFILE(slice_size, ldv_type)
    {
    }

    // This the xpu version
    VOID OpenFileXpu(UINT32 pid, std::string output_file, BOOL enable_ldv)
    {
        if ( !BbFile.is_open() )
        {
            char gnum[500];
            if (pid)
            {
                sprintf(gnum, ".xpu.%u", (unsigned)pid);
            }
            else
            {
                sprintf(gnum, ".xpu");
            }
            std::string tname = gnum;
            BbFile.open((output_file+tname+".bb").c_str());
            BbFile.setf(std::ios::showbase);

        }
    }
};

class XPUISIMPOINT : public ISIMPOINT
{
  public:
   XPUISIMPOINT() : ISIMPOINT()
    {
    }

    VOID EmitSliceStartInfo(ADDRINT endMarker, INT64 markerCount, 
      UINT32 imgId)
    {
      cerr << "EmitSliceStartInfo() NA" << endl;
    }

    VOID EmitSliceEnd(ADDRINT endMarker, UINT32 imgId, THREADID tid,
            UINT32 markerCountOffset=0)
    {
      cerr << "EmitSliceEnd() NA" << endl;
    }

    VOID activate(int argc, char** argv)
    {
        //if (isimpoint_knob)
        {
            if (KnobLDVType.Value() == "none")
                _ldv_type = LDV_TYPE_NONE;
            else if (KnobLDVType.Value() == "approx")
                _ldv_type = LDV_TYPE_APPROXIMATE;
            else if (KnobLDVType.Value() == "exact")
                _ldv_type = LDV_TYPE_EXACT;
            else
                ASSERT(0, "Invalid ldv_type: " + KnobLDVType.Value());
            AddInstrumentation(argc, argv);
        }
    }
};
#endif
