#include "progressstore.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <chrono>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace Impacto::Saekano {
namespace {
Bytes ReadExact(const std::filesystem::path& path,size_t extent) {
  std::ifstream f(path,std::ios::binary|std::ios::ate);
  if(!f || f.tellg()!=std::streampos(extent))throw FormatError("invalid progress-store file extent");
  Bytes b(extent);f.seekg(0);f.read(reinterpret_cast<char*>(b.data()),std::streamsize(b.size()));
  if(!f)throw FormatError("cannot read progress-store file");return b;
}
void WriteExclusiveFlushed(const std::filesystem::path& path,std::span<const uint8_t> bytes) {
#ifdef _WIN32
  const auto handle=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(handle==INVALID_HANDLE_VALUE)throw FormatError("cannot create progress-store file");
  DWORD written=0;const bool ok=WriteFile(handle,bytes.data(),DWORD(bytes.size()),&written,nullptr)
      && written==bytes.size() && FlushFileBuffers(handle);CloseHandle(handle);
  if(!ok)throw FormatError("progress-store write or flush failed");
#else
  const auto fd=open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
  if(fd<0)throw FormatError("cannot create progress-store file");
  size_t done=0;while(done<bytes.size()){const auto n=write(fd,bytes.data()+done,bytes.size()-done);if(n<=0)break;done+=size_t(n);}
  const bool ok=done==bytes.size() && fsync(fd)==0;close(fd);
  if(!ok)throw FormatError("progress-store write or flush failed");
#endif
}
void Publish(const std::filesystem::path& from,const std::filesystem::path& to) {
#ifdef _WIN32
  if(!MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
    throw FormatError("progress-store publication failed; prior generation retained");
#else
  std::error_code error;std::filesystem::rename(from,to,error);
  if(error)throw FormatError("progress-store publication failed; prior generation retained");
#endif
}
std::string ReadGeneration(const std::filesystem::path& root) {
  const auto b=ReadExact(root/"current",19);const std::string s(b.begin(),b.end());
  if(s.substr(0,2)!="g-" || s.back()!='\n')throw FormatError("invalid progress-store index");
  for(size_t i=2;i<18;++i)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))
    throw FormatError("invalid progress-store generation name");
  return s.substr(0,18);
}
struct WriterLock {
#ifdef _WIN32
  HANDLE Handle=INVALID_HANDLE_VALUE;
  explicit WriterLock(const std::filesystem::path& root) {
    Handle=CreateFileW((root/".writer.lock").c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
    if(Handle==INVALID_HANDLE_VALUE)throw FormatError("progress-store writer is already active or unavailable");
  }
  ~WriterLock(){if(Handle!=INVALID_HANDLE_VALUE)CloseHandle(Handle);}
#else
  int Handle=-1;std::filesystem::path Path;
  explicit WriterLock(const std::filesystem::path& root):Path(root/".writer.lock") {
    Handle=open(Path.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
    if(Handle<0)throw FormatError("progress-store writer is already active or unavailable");
  }
  ~WriterLock(){if(Handle>=0){close(Handle);std::error_code e;std::filesystem::remove(Path,e);}}
#endif
};
}
ProgressPair ReadProgressPairFiles(const std::filesystem::path& system,const std::filesystem::path& slots) {
  return {ReadSystemSaveFile(system),NativeSlotData::Decode(ReadExact(slots,NativeSlotData::FileBytes)),{}};
}
std::optional<ProgressPair> ReadProgressStore(const std::filesystem::path& root) {
  if(!std::filesystem::exists(root/"current"))return {};
  const auto generation=ReadGeneration(root);
  auto pair=ReadProgressPairFiles(root/generation/"system.bin",root/generation/"data.bin");
  pair.Generation=generation;return pair;
}
struct ProgressTransaction::Impl {
  std::filesystem::path Root,Directory;
  std::string Name;
  std::unique_ptr<WriterLock> Lock;
  bool Committed=false;
  explicit Impl(const std::filesystem::path& root):Root(root) {
    std::filesystem::create_directories(Root);Lock=std::make_unique<WriterLock>(Root);
    uint64_t index=0;
    if(std::filesystem::exists(Root/"current")) {
      const auto previous=ReadGeneration(Root);index=std::stoull(previous.substr(2),nullptr,16);
    }
    for(size_t attempts=0;attempts<1000000;++attempts) {
      if(index==UINT64_MAX)throw FormatError("progress-store generation space exhausted");
      ++index;std::ostringstream s;s << "g-" << std::hex << std::setw(16) << std::setfill('0') << index;
      Name=s.str();Directory=Root/Name;
      if(std::filesystem::create_directory(Directory))return;
    }
    throw FormatError("progress-store contains too many abandoned generations");
  }
};
ProgressTransaction::ProgressTransaction(const std::filesystem::path& root,const NativeSystemSave& system) {
  const auto bytes=system.Encode();impl_=std::make_unique<Impl>(root);
  WriteExclusiveFlushed(impl_->Directory/"system.bin",bytes);
}
ProgressTransaction::~ProgressTransaction()=default;
const std::string& ProgressTransaction::Generation()const{return impl_->Name;}
void ProgressTransaction::CommitSlots(const NativeSlotData& slots) {
  if(impl_->Committed)throw FormatError("progress transaction is already committed");
  const auto bytes=slots.Encode();WriteExclusiveFlushed(impl_->Directory/"data.bin",bytes);
  const auto index=impl_->Name+'\n';const auto pending=impl_->Directory/"commit.pending";
  WriteExclusiveFlushed(pending,std::span(reinterpret_cast<const uint8_t*>(index.data()),index.size()));
  Publish(pending,impl_->Root/"current");impl_->Committed=true;impl_->Lock.reset();
}
ProgressPair OpenProgressStore(const std::filesystem::path& root,const NativeSystemSave& fresh,
                              const std::filesystem::path& systemImport) {
  if(root.empty())throw FormatError("progress store path is empty");
  if(auto existing=ReadProgressStore(root)) {
    if(!systemImport.empty())throw FormatError("cannot combine a committed progress store with an explicit system import");
    return std::move(*existing);
  }
  auto pair=systemImport.empty()?ProgressPair{fresh,NativeSlotData::Fresh(),{},true}
                               :ReadProgressPairFiles(systemImport,systemImport.parent_path()/"data.bin");
  ProgressTransaction initial(root,pair.System);initial.CommitSlots(pair.Slots);
  pair.Generation=initial.Generation();return pair;
}
SessionSaveWrites::SessionSaveWrites(std::filesystem::path root,const NativeSlotData& slots,
    std::function<NativeSystemSave()> snapshotSystem):root_(std::move(root)),slots_(slots),snapshotSystem_(std::move(snapshotSystem)) {
  if(!snapshotSystem_)throw FormatError("missing system snapshot service");
}
SessionSaveWrites::~SessionSaveWrites(){if(job_.valid())job_.wait();}
void SessionSaveWrites::Snapshot(SaveWriteStage stage) {
  if(job_.valid())throw FormatError("snapshot requested while save I/O is outstanding");
  if(stage==SaveWriteStage::System){transaction_.reset();system_.reset();slotSnapshot_.reset();status_={};error_.clear();}
  try {
    if(stage==SaveWriteStage::System)system_=snapshotSystem_();
    else slotSnapshot_=slots_;
  }catch(const std::exception& e){status_={true,1};error_=e.what();}
}
void SessionSaveWrites::BeginWrite(SaveWriteStage stage) {
  if(job_.valid())throw FormatError("save I/O is already outstanding");
  if(status_.Error)return;
  status_={false,0};
  try {
    if(stage==SaveWriteStage::System) {
      if(!system_)throw FormatError("system snapshot is unavailable");
      job_=std::async(std::launch::async,[this]{transaction_=std::make_unique<ProgressTransaction>(root_,*system_);});
    } else {
      if(!slotSnapshot_ || !transaction_)throw FormatError("slot snapshot or transaction is unavailable");
      job_=std::async(std::launch::async,[this]{transaction_->CommitSlots(*slotSnapshot_);});
    }
  }catch(const std::exception& e){status_={true,1};error_=e.what();transaction_.reset();}
}
SaveWriteStatus SessionSaveWrites::PollWrite() {
  if(job_.valid() && job_.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
    try{job_.get();status_={true,0};}
    catch(const std::exception& e){status_={true,1};error_=e.what();transaction_.reset();}
  }
  return status_;
}
}
