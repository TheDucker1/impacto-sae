#pragma once
#include "profilevoiceoutput.h"
#include <array>
namespace Impacto::Saekano {
class GameResources;
// Startup8002a41a's S+dc/e0 descriptors: root14/4, native order0..3.
// The supplied output is the shared physical7 owner, including gain/clock policy.
class OptionsPreviewVoices {
 public:
  OptionsPreviewVoices(GameResources& resources,ProfileVoiceOutput& output);
  void Prepare(int32_t mode);
  void Start(uint64_t now);
 private:
  std::array<Bytes,4> clips_;
  ProfileVoiceOutput& output_;
  bool prepared_=false;
};
}
