#pragma once
#include "systemsave.h"
#include "slotdata.h"
#include "savemanager.h"
#include <future>
#include <memory>

namespace Impacto::Saekano {
struct ProgressPair {
  NativeSystemSave System;
  NativeSlotData Slots;
  std::string Generation;
};
// Files retain native names/encoding inside immutable generation directories.
// A single flushed index replacement publishes both files together.
std::optional<ProgressPair> ReadProgressStore(const std::filesystem::path& root);
ProgressPair ReadProgressPairFiles(const std::filesystem::path& system,const std::filesystem::path& slots);
class ProgressTransaction {
 public:
  ProgressTransaction(const std::filesystem::path& root,const NativeSystemSave& system);
  ~ProgressTransaction();
  ProgressTransaction(const ProgressTransaction&)=delete;
  ProgressTransaction& operator=(const ProgressTransaction&)=delete;
  const std::string& Generation()const;
  void CommitSlots(const NativeSlotData& slots);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
// Snapshot on the owning thread; immutable payload I/O runs on a background job.
// Poll returns completion only after the real write/flush/publication finishes.
class SessionSaveWrites final:public SaveWriteServices {
 public:
  SessionSaveWrites(std::filesystem::path root,const NativeSlotData& slots,
                    std::function<NativeSystemSave()> snapshotSystem);
  ~SessionSaveWrites();
  void Snapshot(SaveWriteStage stage)override;
  void BeginWrite(SaveWriteStage stage)override;
  SaveWriteStatus PollWrite()override;
  const std::string& LastError()const{return error_;}
 private:
  std::filesystem::path root_;
  const NativeSlotData& slots_;
  std::function<NativeSystemSave()> snapshotSystem_;
  std::optional<NativeSystemSave> system_;
  std::optional<NativeSlotData> slotSnapshot_;
  std::unique_ptr<ProgressTransaction> transaction_;
  SaveWriteStatus status_;
  std::string error_;
  std::future<void> job_;
};
}
