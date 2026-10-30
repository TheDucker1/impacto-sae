#include "systemsave.h"
#include "commands.h"
#include <bit>
#include <fstream>
#include <limits>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace Impacto::Saekano {
NativeSystemSave NativeSystemSave::Decode(std::span<const uint8_t> bytes){
  if(bytes.size()!=FileBytes)throw FormatError("invalid native system-save extent");
  PayloadReader r(bytes);NativeSystemSave s;
  const auto wide=[&](){const auto lo=r.U32(),hi=r.U32();return uint64_t(lo)|(uint64_t(hi)<<32);};
  const auto count=[&](size_t stride){const auto n=r.U32();r.CheckCount(n,stride);return n;};
  const auto history=[&](SystemSaveHistory& h){const auto n=count(4);for(uint32_t i=0;i<n;++i)h.Values.push_back(r.I32());h.Fallback=r.I32();};
  s.Word60148=wide();if(count(2)!=s.Bank1.size())throw FormatError("invalid saved bank1 count");
  for(auto& v:s.Bank1)v=std::bit_cast<int16_t>(r.U16());
  if(count(1)!=s.Bytes60150.size())throw FormatError("invalid saved slot-byte count");
  for(auto& v:s.Bytes60150)v=r.U8();s.Word60140=wide();
  const auto rows=count(4);
  for(uint32_t i=0;i<rows;++i){const auto n=count(2);auto& row=s.Rows60244.emplace_back();for(uint32_t j=0;j<n;++j)row.push_back(r.U16());}
  const auto choices=count(4);
  for(uint32_t i=0;i<choices;++i){const auto n=count(6);auto& row=s.Rows60254.emplace_back();
    for(uint32_t j=0;j<n;++j){auto& item=row.emplace_back();item.Id=std::bit_cast<int16_t>(r.U16());const auto length=count(1);for(uint32_t k=0;k<length;++k)item.Values.push_back(r.U8());}}
  for(size_t i=0;i<7;++i)history(s.Histories[i]);s.Word60318=r.U32();
  for(auto& v:s.Bytes6022c)v=r.U8();
  if(count(1)!=s.Bytes6023a.size())throw FormatError("invalid saved option-byte count");
  for(auto& v:s.Bytes6023a)v=r.U8();s.Byte60239=r.U8();
  for(size_t i=7;i<9;++i)history(s.Histories[i]);s.UsedBytes=FileBytes-r.Remaining();
  while(r.Remaining())if(r.U8())throw FormatError("nonzero native system-save padding");
  return s;
}
Bytes NativeSystemSave::Encode()const{
  Bytes b;const auto put=[&](uint64_t v,size_t n){if(n>FileBytes-b.size())throw FormatError("system-save exceeds native buffer");for(size_t i=0;i<n;++i)b.push_back(uint8_t(v>>(i*8)));};
  const auto count=[&](size_t n){if(n>FileBytes)throw FormatError("system-save collection exceeds budget");put(n,4);};
  const auto history=[&](const SystemSaveHistory& h){count(h.Values.size());for(auto v:h.Values)put(uint32_t(v),4);put(uint32_t(h.Fallback),4);};
  put(Word60148,8);count(Bank1.size());for(auto v:Bank1)put(uint16_t(v),2);
  count(Bytes60150.size());for(auto v:Bytes60150)put(v,1);put(Word60140,8);
  count(Rows60244.size());for(const auto& row:Rows60244){count(row.size());for(auto v:row)put(v,2);}
  count(Rows60254.size());for(const auto& row:Rows60254){count(row.size());for(const auto& item:row){put(uint16_t(item.Id),2);count(item.Values.size());for(auto v:item.Values)put(v,1);}}
  for(size_t i=0;i<7;++i)history(Histories[i]);put(Word60318,4);
  for(auto v:Bytes6022c)put(v,1);count(Bytes6023a.size());for(auto v:Bytes6023a)put(v,1);put(Byte60239,1);
  for(size_t i=7;i<9;++i)history(Histories[i]);b.resize(FileBytes,0);return b;
}
NativeSystemSave ReadSystemSaveFile(const std::filesystem::path& path){
  std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f || f.tellg()!=std::streampos(NativeSystemSave::FileBytes))throw FormatError("cannot read native system save");
  Bytes b(NativeSystemSave::FileBytes);f.seekg(0);f.read(reinterpret_cast<char*>(b.data()),std::streamsize(b.size()));if(!f)throw FormatError("truncated system-save file");return NativeSystemSave::Decode(b);
}
void WriteSystemSaveFile(const std::filesystem::path& path,const NativeSystemSave& state){
  const auto bytes=state.Encode();auto pending=path;pending+=".pending";
#ifdef _WIN32
  const auto handle=CreateFileW(pending.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(handle==INVALID_HANDLE_VALUE)throw FormatError("cannot create system-save pending file");
  DWORD written=0;const bool complete=WriteFile(handle,bytes.data(),DWORD(bytes.size()),&written,nullptr) && written==bytes.size() && FlushFileBuffers(handle);
  CloseHandle(handle);
  if(!complete)throw FormatError("system-save write or flush failed; pending file retained");
  if(!MoveFileExW(pending.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw FormatError("system-save replacement failed; pending file retained");
#else
  const auto fd=open(pending.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)throw FormatError("cannot create system-save pending file");
  size_t done=0;while(done<bytes.size()){const auto n=write(fd,bytes.data()+done,bytes.size()-done);if(n<=0)break;done+=size_t(n);}
  const bool complete=done==bytes.size() && fsync(fd)==0;close(fd);
  if(!complete)throw FormatError("system-save write or flush failed; pending file retained");
  std::error_code error;std::filesystem::rename(pending,path,error);if(error)throw FormatError("system-save replacement failed; pending file retained");
#endif
}
}
