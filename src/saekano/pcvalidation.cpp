#include "pcvalidation.h"
#include <algorithm>
#include <array>
#include <cwctype>
#include <fstream>
#include <functional>
#include <cctype>
#include <stdexcept>
extern "C" {
#include <libavutil/sha.h>
#include <libavutil/mem.h>
}
namespace Impacto::Saekano {
const std::vector<PcRequiredFile>& PcRequiredFiles(){
  static const std::vector<PcRequiredFile> files={
#include "pcdata_manifest.inc"
  };return files;
}
namespace {
bool Inside(const std::filesystem::path& file,const std::filesystem::path& directory){
  const auto p=std::filesystem::weakly_canonical(file),base=std::filesystem::weakly_canonical(directory);
  auto f=p.begin();for(auto d=base.begin();d!=base.end();++d,++f){
    if(f==p.end())return false;auto left=f->native(),right=d->native();
#ifdef _WIN32
    std::transform(left.begin(),left.end(),left.begin(),[](wchar_t c){return wchar_t(std::towlower(c));});
    std::transform(right.begin(),right.end(),right.begin(),[](wchar_t c){return wchar_t(std::towlower(c));});
#endif
    if(left!=right)return false;
  }return true;
}
std::string HashFile(const std::filesystem::path& path,std::stop_token stop,const std::function<void(size_t)>& progress){
  std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("unreadable file");
  auto* sha=av_sha_alloc();if(!sha)throw std::runtime_error("cannot allocate SHA256 state");
  struct Free{AVSHA* Value;~Free(){av_free(Value);}} cleanup{sha};
  if(av_sha_init(sha,256)<0)throw std::runtime_error("cannot initialize SHA256");
  std::vector<char> buffer(1024*1024);
  while(file){if(stop.stop_requested())return {};file.read(buffer.data(),std::streamsize(buffer.size()));const auto n=file.gcount();
    if(n>0){av_sha_update(sha,reinterpret_cast<const uint8_t*>(buffer.data()),size_t(n));progress(size_t(n));}}
  if(!file.eof())throw std::runtime_error("error while reading file");
  std::array<uint8_t,32> digest{};av_sha_final(sha,digest.data());const char* digits="0123456789abcdef";std::string value;
  for(auto byte:digest){value+=digits[byte>>4];value+=digits[byte&15];}return value;
}
}
void PcRequireOutsideData(const std::filesystem::path& writable,const std::filesystem::path& usrdir){
  if(usrdir.empty())return;
  if(Inside(writable,usrdir))throw std::runtime_error("Configuration and saves must be outside the original USRDIR folder.");
  auto parent=usrdir.parent_path();auto name=PcPathUtf8(parent.filename());std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});
  if(name=="gamefile" && Inside(writable,parent))throw std::runtime_error("Configuration and saves must be outside the original GAMEFILE folder.");
}
PcDataValidator::~PcDataValidator(){Cancel();}
void PcDataValidator::Cancel(){if(worker_.joinable())worker_.request_stop();}
PcValidationStatus PcDataValidator::Status()const{std::lock_guard lock(mutex_);return status_;}
void PcDataValidator::Start(const std::filesystem::path& usrdir,bool strict){
  if(Status().Running)throw std::runtime_error("Wait for the current data check to finish or cancel it first.");
  if(worker_.joinable())worker_.join();
  {std::lock_guard lock(mutex_);status_={};status_.Running=true;status_.Strict=strict;status_.Usrdir=usrdir;status_.TotalFiles=PcRequiredFiles().size();stamps_.clear();
    for(const auto& f:PcRequiredFiles())status_.TotalBytes+=f.Bytes;status_.Message=strict?"Checking supported PCSG00543 reference hashes":"Checking required filenames";}
  worker_=std::jthread([this,usrdir,strict](std::stop_token stop){
    std::vector<Stamp> stamps;
    const auto error=[&](const std::string& text){std::lock_guard lock(mutex_);status_.Errors.push_back(text);};
    try{
      if(usrdir.empty() || !std::filesystem::is_directory(usrdir))throw std::runtime_error("Choose an existing USRDIR folder.");
      for(const auto& f:PcRequiredFiles()){
        if(stop.stop_requested())break;
        {std::lock_guard lock(mutex_);status_.Current=f.Path;}
        const auto path=usrdir/PcPathFromUtf8(f.Path);
        try{
          if(!std::filesystem::is_regular_file(path))error(std::string(f.Path)+": missing required file");
          else{
            if(strict){
              const auto bytes=std::filesystem::file_size(path);const auto time=std::filesystem::last_write_time(path);
              if(bytes!=f.Bytes)error(std::string(f.Path)+": size mismatch; unsupported or modified data");
              else{
                const auto digest=HashFile(path,stop,[this](size_t n){std::lock_guard lock(mutex_);status_.FinishedBytes+=n;});
                if(!stop.stop_requested() && digest!=f.Sha256)error(std::string(f.Path)+": SHA256 mismatch; does not match the supported reference revision");
                if(!stop.stop_requested() && (std::filesystem::file_size(path)!=bytes || std::filesystem::last_write_time(path)!=time))error(std::string(f.Path)+": changed while checking; run validation again");
              }
              stamps.push_back({path,bytes,time});
            }else{std::ifstream readable(path,std::ios::binary);if(!readable)error(std::string(f.Path)+": unreadable file");}
          }
        }catch(const std::exception& e){error(std::string(f.Path)+": "+e.what());}
        {std::lock_guard lock(mutex_);++status_.FinishedFiles;}
      }
    }catch(const std::exception& e){error(e.what());}
    std::lock_guard lock(mutex_);stamps_=std::move(stamps);status_.Cancelled=stop.stop_requested();status_.Running=false;
    status_.Passed=!status_.Cancelled && status_.Errors.empty();status_.Current.clear();
    status_.Message=status_.Cancelled?"Validation cancelled":status_.Passed?(strict?"Reference hashes match":"Required files are present"):"Data validation failed; see file errors";
  });
}
bool PcDataValidator::Fresh(const std::filesystem::path& usrdir,bool strict)const{
  std::lock_guard lock(mutex_);if(status_.Running || !status_.Passed || strict!=status_.Strict || usrdir!=status_.Usrdir)return false;
  try{
    if(strict){for(const auto& s:stamps_)if(std::filesystem::file_size(s.Path)!=s.Bytes || std::filesystem::last_write_time(s.Path)!=s.Time)return false;}
    else for(const auto& f:PcRequiredFiles())if(!std::filesystem::is_regular_file(usrdir/PcPathFromUtf8(f.Path)))return false;
  }catch(...){return false;}return true;
}
PcDataValidator& RuntimePcDataValidator(){static PcDataValidator validator;return validator;}
void PcEnsureGameData(const std::filesystem::path& usrdir,bool strict){
  auto& validator=RuntimePcDataValidator();if(validator.Fresh(usrdir,strict))return;
  while(validator.Status().Running)SDL_Delay(10);
  validator.Start(usrdir,strict);while(validator.Status().Running)SDL_Delay(10);
  const auto result=validator.Status();if(!result.Passed){std::string error="Original game data could not be validated.\n";
    for(size_t i=0;i<std::min(size_t(8),result.Errors.size());++i)error+=result.Errors[i]+"\n";
    if(result.Errors.size()>8)error+="Additional errors are listed in the launcher.\n";throw std::runtime_error(error);}
}
} // namespace Impacto::Saekano
