/*BEGIN_LEGAL 
BSD License 

Copyright (c)2023 Intel Corporation. All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */

#ifndef KERNEL_CONTROL_H
#define  KERNEL_CONTROL_H

#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <map>
#include <list>

using namespace std;

static const uint32_t BUFSIZE=2000;
typedef list<uint32_t> ILIST;
typedef ILIST::iterator ILISTITER;
typedef struct _KINFO
{
 string name;
 uint32_t count;
 ILIST itlist;
} KINFO;

typedef map<string, KINFO *> KINFOMAP;

class KERNEL_CONTROL
{
public:
  KINFOMAP _kinfo;
  KINFO *_rstart;
  KINFO *_rstop;
  static Knob<string> KnobKernelSpec;
  static Knob<string> KnobKernelStart;
  static Knob<string> KnobKernelStop;
  bool ParseStartStop()
  {
    bool retval = false;
    uint32_t iter=0;
    string kname;
    string itno;
    string field;

    _rstart = new KINFO;
    _rstop = new KINFO;
    string startspec = KnobKernelStart.GetValue().c_str();
    string stopspec = KnobKernelStop.GetValue().c_str();

    string itspec = startspec;
    KINFO *tkinfo = _rstart;
    for(int i=0; i<=1; i++)
    {
      istringstream s(itspec);
      //cerr << ((i==0)?"start":"stop") << " itspec "  << itspec << endl;
      // Get kernel-name field
      field.clear();
      getline(s, field, ':');
      if (!field.empty())
      {
        kname = field;
        //cerr << " kname " << kname << endl;
        tkinfo->name = kname;
        tkinfo->count = 0;
        getline(s, itno);
        //cerr << " itno " << itno << endl;
        iter=atoi(itno.c_str());
        tkinfo->itlist.push_back(iter);
        retval = true;
      }
      itspec = stopspec;
      tkinfo = _rstop;
    }
  return retval;
  }

  bool ParseKernelSpec()
  {
    bool retval = ParseStartStop();
    if (strcmp(KnobKernelSpec.GetValue().c_str(),"") == 0)
    {
      return retval;
    }
    string filename = KnobKernelSpec.GetValue().c_str();
    ifstream rfile(filename.c_str());
    if (!rfile.is_open())
    {
      cerr << "Could not open Kernel specification file '" <<
      KnobKernelSpec.GetValue().c_str() << "'" << endl;
      return false;
    }
    // Read records from the file
    uint32_t lineNum = 0;
    string kname;
    string itspec;
    string specitem;
    string itrange;
    string itno;
    KINFO *tkinfo = NULL;
    while(true)
    {
      if( rfile.eof() )
      {
        break;
      }

      uint32_t recordLen = 0;
      char record[BUFSIZE+1];
      string field;


      // Get the next line
      rfile.getline(record, BUFSIZE);
      lineNum++;

      // Ignore empty lines
      recordLen = strnlen(record, BUFSIZE+1);
      if (recordLen == 0)
        continue;

      // first letter '#' : this is a comment 
      if(record[0]=='#')
        continue;

      istringstream s(record);
      tkinfo = new KINFO;

      // Get kernel-name field
      field.clear();
      getline(s, field, ':');
      GTPIN_ASSERT(!field.empty());
      kname = field;
      tkinfo->name = kname;
      tkinfo->count = 0;
      _kinfo[kname]=tkinfo;
      //cerr << "kernel-name " << kname << endl;

      while(true)
      {
        uint32_t low=0;
        uint32_t high=0;
        string subfield;

        field.clear();
        getline(s, field, ',');
        if(field.empty()) break;
        itspec = field;
        //cerr << "itspec " << itspec << endl;
        subfield.clear();
        istringstream t(field);
        getline(t, subfield, '-');
        low=atoi(subfield.c_str());
        subfield.clear();
        getline(t, subfield, '-');
        if(subfield.empty()) high=low;
        else
        {
          high=atoi(subfield.c_str());
          if(low>high)
          {
            cerr << "kernel specifiction for '" << kname << "'";
            cerr << " high: " << high << " is lower than low: " << low << endl;
            GTPIN_ASSERT(0);
          }
        }
        if(low <= 0)
        {
            cerr << "kernel specifiction for '" << kname << "'";
            cerr << " Itertion count should  non-zero " << low << endl;
            GTPIN_ASSERT(0);
        }
        //cerr << " low " << low << " high " << high << endl;
        for(uint32_t i=low; i<=high; i++)
        {
          //cerr << "    adding " << i << endl;
          tkinfo->itlist.push_back(i);
        }
        tkinfo->itlist.sort();
        tkinfo->itlist.unique();
        //PrintKinfo(tkinfo);
      }
    }
    rfile.close();

    return true;
  }

  uint32_t CountKernelStartStop(string kname, XPU_EVENT *ep)
  {
    uint32_t retval=0;
    if (_rstart->name == kname)
    {
      _rstart->count++;
      if((!_rstart->itlist.empty()) && (_rstart->count == _rstart->itlist.front()))
      {
        retval = _rstart->itlist.front();
        _rstart->itlist.pop_front();
        *ep = REGION_START;
        return retval;
      }
    }

    if (_rstop->name == kname)
    {
      _rstop->count++;
      if((!_rstop->itlist.empty()) && (_rstop->count == _rstop->itlist.front()))
      {
        retval = _rstop->itlist.front();
        _rstop->itlist.pop_front();
        *ep = REGION_STOP;
        return retval;
      }
    }
    return retval;
  }

  uint32_t CountKernelGeneral(string kname)
  {
    auto it=_kinfo.find(kname);
    if ( it != _kinfo.end())
    {
      uint32_t retval=0;
      KINFO *tkinfo = it->second;
      tkinfo->count++;
      //cerr << "\tKernel " << tkinfo->name;
      //cerr << " iteration " << tkinfo->count << endl;
      if(tkinfo->itlist.empty()) return 0;
      //cerr << " \t front " << tkinfo->itlist.front() << endl;
      if(tkinfo->count != tkinfo->itlist.front()) return 0;
      retval = tkinfo->itlist.front();
      tkinfo->itlist.pop_front();
      return retval;
    }
    return 0;
  }
};
// static Knobs
Knob<string> KERNEL_CONTROL::KnobKernelSpec("kspec", "", "File with kernel specification (one per line):\n\t<name>:<itspec>\n\titspec: <specitem>(,<specitem>)\n\tspecitem: <iteration_number>|<iteration_range>\n\titeration_range: <iteration_number>-<iteration_number>");
Knob<string> KERNEL_CONTROL::KnobKernelStart("rstart", "", "kernel specification <name>:<itspec>");
Knob<string> KERNEL_CONTROL::KnobKernelStop("rstop", "", "kernel specification <name>:<itspec>");
#endif  //  KERNEL_CONTROL_H

