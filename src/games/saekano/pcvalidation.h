#pragma once
#include "pcconfig.h"
#include <atomic>
#include <mutex>
#include <thread>

namespace Impacto::Saekano {
void PcRequireOutsideData(const std::filesystem::path& writable,const std::filesystem::path& usrdir);
struct PcValidationStatus {
  bool Running=false,Passed=false,Cancelled=false,Strict=false;
  size_t FinishedFiles=0,TotalFiles=0;
  uint64_t FinishedBytes=0,TotalBytes=0;
  std::string Current,Message;
  std::filesystem::path Usrdir;
  std::vector<std::string> Errors;
};
class PcDataValidator {
 public:
  ~PcDataValidator();
  void Start(const std::filesystem::path& usrdir,bool strict);
  void Cancel();
  PcValidationStatus Status()const;
  bool Fresh(const std::filesystem::path& usrdir,bool strict)const;
 private:
  struct Stamp {std::filesystem::path Path;uintmax_t Bytes;std::filesystem::file_time_type Time;};
  mutable std::mutex mutex_;PcValidationStatus status_;std::vector<Stamp> stamps_;
  std::jthread worker_;
};
PcDataValidator& RuntimePcDataValidator();
void PcEnsureGameData(const std::filesystem::path& usrdir,bool strict);
int RunPcLauncher(const std::filesystem::path& configPath={},
    const std::filesystem::path& usrdirOverride={},const std::filesystem::path& saveOverride={});
} // namespace Impacto::Saekano
