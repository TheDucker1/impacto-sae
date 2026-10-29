#include "musicplayback.h"

#include "headlessbackend.h"
#include "resources.h"
#include "profilearchive.h"
#include "../audio/audiosystem.h"
#include "../audio/openal/audiobackend.h"
#include "../audio/openal/openalaudiochannel.h"
#include "../io/dummystream.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <iterator>

namespace Impacto::Saekano {
namespace {
class HcaPlaybackStream final : public Audio::AudioStream {
 public:
  HcaPlaybackStream(ArchiveBgmResources& resources, int32_t track)
      : HcaPlaybackStream(resources.Open(track), track, "bgm.awb") {}
  HcaPlaybackStream(ArchiveSeResources& resources, int32_t track)
      : HcaPlaybackStream(resources.Open(track), track, "se.awb") {}
  explicit HcaPlaybackStream(Bytes encoded)
      : HcaPlaybackStream(std::make_unique<HcaWaveReader>(std::move(encoded)),
                          0, "profile.hca") {}
  HcaPlaybackStream(std::unique_ptr<HcaWaveReader> reader, int32_t track,
                    const char* bank)
      : reader_(std::move(reader)) {
    const auto& info = reader_->Info();
    if (info.Frames > INT32_MAX || info.LoopEnd > INT32_MAX)
      throw FormatError("HCA exceeds Impacto channel sample range");
    ChannelCount = int(info.Channels);
    SampleRate = int(info.SampleRate);
    BitDepth = 16;
    Duration = int(info.Frames);
    LoopStart = info.Loop ? int(info.LoopStart) : 0;
    LoopEnd = info.Loop ? int(info.LoopEnd) : Duration;
    Io::DummyStream::Create(bank, &BaseStream);
    BaseStream->Meta.Id = uint32_t(track);
    BaseStream->Meta.Size =
        int64_t(info.HeaderSize) + int64_t(info.BlockCount) * info.BlockSize;
    BaseStream->IsSeekSlow = true;
  }
  int Read(void* buffer, int frames) override {
    if (frames < 0 || (!buffer && frames))
      throw FormatError("invalid PCM buffer");
    const auto read = reader_->Read(std::span(static_cast<int16_t*>(buffer),
                                              size_t(frames) * ChannelCount));
    ReadPosition = int(reader_->Position());
    return int(read);
  }
  void Seek(int frame) override {
    if (frame < 0) throw FormatError("negative HCA seek");
    reader_->Seek(uint32_t(frame));
    ReadPosition = int(reader_->Position());
  }

 private:
  std::unique_ptr<HcaWaveReader> reader_;
};
void AlCheck(const char* operation) {
  if (alGetError() != AL_NO_ERROR)
    throw FormatError(std::string("OpenAL failure: ") + operation);
}
void Word(Bytes& bytes, uint32_t value, size_t width = 4) {
  for (size_t n = 0; n < width; ++n) bytes.push_back(uint8_t(value >> (8 * n)));
}
class SessionScript final : public ScriptSource {
 public:
  Bytes Data;
  explicit SessionScript(uint32_t track, std::optional<uint32_t> soundChannel) {
    Word(Data, soundChannel ? 7 : 3);
    Word(Data, 8);
    auto record = [&](uint16_t opcode, const Bytes& payload) {
      Word(Data, uint32_t(payload.size() + 6));
      Word(Data, opcode, 2);
      Data.insert(Data.end(), payload.begin(), payload.end());
    };
    Bytes play;
    if (soundChannel) {
      for (auto value : {0u, track, *soundChannel, 100u, 1u, 0u}) Word(play, value);
      record(0x25a, play);
      auto wait = [&] {
        Bytes payload; Word(payload, 500); Word(payload, 0);
        record(0x2c0, payload);
      };
      auto stop = [&](uint32_t channel) {
        Bytes payload; Word(payload, channel); Word(payload, 0);
        record(0x25b, payload);
      };
      wait();
      play.clear();
      const uint32_t other = (*soundChannel + 1) % 4;
      for (auto value : {0u, track, other, 50u, 0u, 0u}) Word(play, value);
      record(0x25a, play);
      wait(); stop(*soundChannel); wait(); stop(other);
      Data.resize(Data.size() + 28);
      return;
    } else {
      const auto text = std::to_string(track);
      Word(play, uint32_t(text.size()));
      for (auto c : text) Word(play, uint8_t(c), 2);
      Word(play, 100);
      record(0x258, play);
    }
    Bytes wait;
    Word(wait, 2000);
    Word(wait, 0);
    record(0x2c0, wait);
    Bytes stop;
    if (soundChannel) Word(stop, *soundChannel);
    Word(stop, 0);
    record(soundChannel ? 0x25b : 0x259, stop);
    Data.resize(Data.size() + 12);
  }
  Bytes Load(int32_t id) override {
    if (id) throw FormatError("audio session requested another script");
    return Data;
  }
};
void SaveWav(const std::filesystem::path& path,
             std::span<const int16_t> samples) {
  if (samples.size() > (UINT32_MAX - 36) / 2)
    throw FormatError("WAV exceeds size limit");
  const uint32_t size = uint32_t(samples.size()) * 2;
  Bytes header{'R', 'I', 'F', 'F'};
  Word(header, size + 36);
  for (char c : std::string_view("WAVEfmt ")) header.push_back(uint8_t(c));
  Word(header, 16);
  Word(header, 1, 2);
  Word(header, 2, 2);
  Word(header, 48000);
  Word(header, 48000 * 4);
  Word(header, 4, 2);
  Word(header, 16, 2);
  for (char c : std::string_view("data")) header.push_back(uint8_t(c));
  Word(header, size);
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(header.data()), header.size());
  for (auto sample : samples) {
    out.put(char(uint16_t(sample)));
    out.put(char(uint16_t(sample) >> 8));
  }
  if (!out) throw FormatError("cannot write captured WAV");
}
void CreateCaptureDirectory(const std::filesystem::path& usrdir,
                            const std::filesystem::path& captureDirectory) {
  const auto output = std::filesystem::weakly_canonical(captureDirectory);
  const auto data = std::filesystem::weakly_canonical(usrdir);
  for (auto parent = output; !parent.empty(); parent = parent.parent_path()) {
    if (parent == data ||
        (data.filename() == "USRDIR" && parent == data.parent_path()))
      throw FormatError("audio capture overlaps original game data");
    if (parent == parent.parent_path()) break;
  }
  if (std::filesystem::exists(output))
    throw FormatError("choose a fresh audio capture directory");
  std::filesystem::create_directories(output);
}
}  // namespace

struct MusicPlayback::Impl {
  ArchiveBgmResources& Resources;
  ArchiveSeResources* Sounds;
  std::unique_ptr<Audio::OpenAL::AudioBackend> Backend;
  ALCdevice* Device = nullptr;
  ALCcontext* Context = nullptr;
  LPALCRENDERSAMPLESSOFT RenderSamples = nullptr;
  std::unique_ptr<Audio::OpenAL::OpenALAudioChannel> Channel;
  std::unique_ptr<Audio::OpenAL::OpenALAudioChannel> OutgoingBgm;
  uint64_t BgmSerial=0,CurrentBgmSerial=0,OutgoingBgmSerial=0;
  uint32_t BgmFadeOutDuration=500;
  std::vector<uint64_t> RetiredBgm;
  std::array<std::unique_ptr<Audio::OpenAL::OpenALAudioChannel>,4> SeChannels;
  std::unique_ptr<Audio::OpenAL::OpenALAudioChannel> ProfileVoice;
  std::unique_ptr<Audio::OpenAL::OpenALAudioChannel> SecondaryVoice;
  std::unique_ptr<Audio::OpenAL::OpenALAudioChannel> MissionVoice;
  bool HasMissionVoice=false;
  std::unique_ptr<HcaPlaybackStream> PreparedMissionVoice;
  std::unique_ptr<Audio::OpenAL::OpenALAudioChannel> UiSound;
  SystemSoundAssets* UiSounds = nullptr;
  float UiSoundGain = 0;
  BgmStopFade BgmFade;
  float BgmGain = 1.0f;
  double BgmFadeFraction = 0;
  uint32_t BgmFadeFrameRemainder = 0;
  void RetireOutgoingBgm() {
    if(OutgoingBgmSerial)RetiredBgm.push_back(OutgoingBgmSerial);
    OutgoingBgmSerial=0;OutgoingBgm->Stop(0);
  }
  void BeginOutgoingFade(uint32_t duration) {
    BgmFade={};BgmFade.Request(int32_t(duration));BgmFade.Tick(0);BgmFade.Tick(0);
    BgmFadeFraction=0;BgmFadeFrameRemainder=0;
    OutgoingBgm->SetVolume(BgmGain*BgmFade.Weight);
  }
  void AdvanceBgmFade(uint32_t milliseconds) {
    if (!BgmFade.Active()) return;
    BgmFade.Tick(milliseconds);
    OutgoingBgm->SetVolume(BgmGain * BgmFade.Weight);
    if (!BgmFade.Active()) RetireOutgoingBgm();
  }
  std::unique_ptr<HcaPlaybackStream> PreparedProfileVoice;
  std::array<std::unique_ptr<HcaPlaybackStream>, 2> PreparedOrdinaryVoices;
  std::array<bool, 2> HasOrdinaryVoice{};
  bool HasProfileVoice = false;
  float OldMaster = Audio::MasterVolume;
  float OldGroup = Audio::GroupVolumes[Audio::ACG_BGM];
  float OldSeGroup = Audio::GroupVolumes[Audio::ACG_SE];
  float OldVoiceGroup = Audio::GroupVolumes[Audio::ACG_Voice];
  explicit Impl(ArchiveBgmResources& resources, bool loopback, ArchiveSeResources* sounds)
      : Resources(resources), Sounds(sounds) {
    try {
      if (loopback) {
        const auto open = reinterpret_cast<LPALCLOOPBACKOPENDEVICESOFT>(
            alcGetProcAddress(nullptr, "alcLoopbackOpenDeviceSOFT"));
        RenderSamples = reinterpret_cast<LPALCRENDERSAMPLESSOFT>(
            alcGetProcAddress(nullptr, "alcRenderSamplesSOFT"));
        if (!open || !RenderSamples)
          throw UnsupportedCommand(
              "OpenAL software output capture is unavailable");
        Device = open(nullptr);
        if (!Device) throw FormatError("cannot open OpenAL capture device");
        const ALCint attributes[] = {ALC_FREQUENCY,
                                     48000,
                                     ALC_FORMAT_CHANNELS_SOFT,
                                     ALC_STEREO_SOFT,
                                     ALC_FORMAT_TYPE_SOFT,
                                     ALC_SHORT_SOFT,
                                     ALC_OUTPUT_LIMITER_SOFT,
                                     ALC_FALSE,
                                     0};
        Context = alcCreateContext(Device, attributes);
        if (!Context || !alcMakeContextCurrent(Context))
          throw FormatError("cannot create OpenAL capture context");
      } else {
        auto backend = std::make_unique<Audio::OpenAL::AudioBackend>();
        if (!backend->Init())
          throw FormatError("cannot initialize Impacto audio output");
        Backend = std::move(backend);
      }
      Audio::MasterVolume = 1;
      Audio::GroupVolumes[Audio::ACG_BGM] = 1;
      Audio::GroupVolumes[Audio::ACG_SE] = 1;
      Audio::GroupVolumes[Audio::ACG_Voice] = 1;
      Channel = std::make_unique<Audio::OpenAL::OpenALAudioChannel>(
          Audio::AC_BGM0, Audio::ACG_BGM);
      OutgoingBgm = std::make_unique<Audio::OpenAL::OpenALAudioChannel>(
          Audio::AC_BGM1, Audio::ACG_BGM);
      ProfileVoice = std::make_unique<Audio::OpenAL::OpenALAudioChannel>(
          Audio::AC_VOICE0, Audio::ACG_Voice);
      SecondaryVoice = std::make_unique<Audio::OpenAL::OpenALAudioChannel>(
          Audio::AC_VOICE1, Audio::ACG_Voice);
      MissionVoice = std::make_unique<Audio::OpenAL::OpenALAudioChannel>(
          Audio::AC_VOICE2, Audio::ACG_Voice);
      UiSound = std::make_unique<Audio::OpenAL::OpenALAudioChannel>(
          Audio::AC_SE0, Audio::ACG_SE);
      if (Sounds) for (size_t i=0; i<SeChannels.size(); ++i)
        SeChannels[i] = std::make_unique<Audio::OpenAL::OpenALAudioChannel>(
            Audio::AudioChannelId(int(Audio::AC_SSE0) + i), Audio::ACG_SE);
      AlCheck("channel creation");
    } catch (...) {
      Cleanup();
      throw;
    }
  }
  void Cleanup() {
    Channel.reset();
    OutgoingBgm.reset();
    ProfileVoice.reset();
    SecondaryVoice.reset();
    MissionVoice.reset();
    PreparedMissionVoice.reset();
    UiSound.reset();
    PreparedProfileVoice.reset();
    for (auto& channel : SeChannels) channel.reset();
    alcMakeContextCurrent(nullptr);
    if (Backend) {
      Backend->Shutdown();
      Backend.reset();
    }
    if (Context) {
      alcDestroyContext(Context);
      Context = nullptr;
    }
    if (Device) {
      alcCloseDevice(Device);
      Device = nullptr;
    }
    Audio::MasterVolume = OldMaster;
    Audio::GroupVolumes[Audio::ACG_BGM] = OldGroup;
    Audio::GroupVolumes[Audio::ACG_SE] = OldSeGroup;
    Audio::GroupVolumes[Audio::ACG_Voice] = OldVoiceGroup;
  }
  ~Impl() { Cleanup(); }
};
MusicPlayback::MusicPlayback(ArchiveBgmResources& resources, bool loopback, ArchiveSeResources* sounds)
    : impl_(std::make_unique<Impl>(resources, loopback, sounds)) {}
MusicPlayback::~MusicPlayback() = default;
void MusicPlayback::Submit(const GameEvent& event) {
  if (event.Kind == GameEventKind::PlayBgm) {
    if (!event.Bgm || event.Bgm->Track != event.Id)
      throw FormatError("invalid typed BGM request");
    const auto& request = *event.Bgm;
    if (!request.Volume)
      throw UnsupportedCommand("native BGM volume state is unknown");
    if (!std::isfinite(request.Volume->Gain) || request.Volume->Gain < 0 ||
        request.Volume->Gain > 1)
      throw UnsupportedCommand(
          "BGM output gain outside authored settings range");
    auto stream =
        std::make_unique<HcaPlaybackStream>(impl_->Resources, request.Track);
    const bool loop = impl_->Resources.Info(request.Track).Loop;
    // Native cf002 resets an active fader and retires its outgoing voice,
    // then rotates the current voice into that slot. Fade-in is authored0.
    impl_->RetiredBgm.clear();impl_->RetireOutgoingBgm();
    impl_->BgmFade = {};
    impl_->BgmFadeFraction = 0;
    impl_->BgmFadeFrameRemainder = 0;
    impl_->BgmGain = request.Volume->Gain;
    if(impl_->Channel->GetState()!=Audio::ACS_Stopped) {
      std::swap(impl_->Channel,impl_->OutgoingBgm);
      impl_->OutgoingBgmSerial=impl_->CurrentBgmSerial;impl_->CurrentBgmSerial=0;
      impl_->BeginOutgoingFade(impl_->BgmFadeOutDuration);
    }
    impl_->Channel->SetVolume(request.Volume->Gain);
    impl_->Channel->Play(std::move(stream), loop, 0);
    impl_->CurrentBgmSerial=++impl_->BgmSerial;
  } else if (event.Kind == GameEventKind::StopBgm) {
    if (event.Parameter < 0)
      throw UnsupportedCommand("negative BGM fade duration is not bound");
    impl_->BgmFadeOutDuration=uint32_t(event.Parameter);impl_->RetiredBgm.clear();
    if(impl_->BgmFade.Active()) {
      impl_->BgmFade.Request(event.Parameter); // Existing host terminal phase3.
      impl_->OutgoingBgm->SetVolume(0);impl_->RetireOutgoingBgm();
    }
    if(impl_->Channel->GetState()!=Audio::ACS_Stopped) {
      std::swap(impl_->Channel,impl_->OutgoingBgm);
      impl_->OutgoingBgmSerial=impl_->CurrentBgmSerial;impl_->CurrentBgmSerial=0;
      impl_->BeginOutgoingFade(uint32_t(event.Parameter));
    }
  } else if (event.Kind == GameEventKind::PlaySe) {
    if (!impl_->Sounds || !event.Se || event.Se->Track != event.Id)
      throw FormatError("invalid typed SE request or missing SE bank");
    const auto& request = *event.Se;
    if (request.Channel < 2 || request.Channel > 5)
      throw UnsupportedCommand("SE playback outside the four native SE channels");
    if (!request.Volume)
      throw UnsupportedCommand("native SE service master setting is unknown");
    const float gain = request.Volume->Gain;
    if (!std::isfinite(gain) || gain < 0 || gain > 1)
      throw UnsupportedCommand("SE output gain outside authored settings range");
    auto stream = std::make_unique<HcaPlaybackStream>(*impl_->Sounds, request.Track);
    // The final service refresh writes the same SE gain to backend2..5.
    // BGM is recomputed from unchanged service percentages, preserving its
    // applied gain here. The voice consumer remains separate. Remember is
    // never a looping flag.
    for (auto& channel : impl_->SeChannels) channel->SetVolume(gain);
    impl_->SeChannels[size_t(request.Channel - 2)]->Play(std::move(stream),
        impl_->Sounds->Info(request.Track).Loop, 0);
  } else if (event.Kind == GameEventKind::Sound) {
    if (!impl_->UiSounds)
      throw UnsupportedCommand("UI sound bank/gain is not bound");
    if (event.Id < 0) throw FormatError("negative UI sound ID");
    auto asset = impl_->UiSounds->Load(size_t(event.Id));
    auto reader = std::make_unique<HcaWaveReader>(std::move(asset.Data));
    const bool loop = reader->Info().Loop;
    auto stream = std::make_unique<HcaPlaybackStream>(
        std::move(reader), event.Id, "first.dat/systemse");
    impl_->UiSound->SetVolume(impl_->UiSoundGain);
    impl_->UiSound->Play(std::move(stream), loop, 0);
  } else if (event.Kind == GameEventKind::StopSe) {
    if (!impl_->Sounds || event.Id < 2 || event.Id > 5)
      throw UnsupportedCommand("SE stop outside the four native SE channels");
    if (event.Parameter != 0)
      throw UnsupportedCommand("native SE fade-time conversion is not verified");
    impl_->SeChannels[size_t(event.Id - 2)]->Stop(0);
  } else if (event.Kind == GameEventKind::StopChannel) {
    if (event.Parameter || (event.Id != 7 && event.Id != 8))
      throw UnsupportedCommand(
          "native channel stop outside bound voice7/8 or nonzero fade");
    StopOrdinaryVoice(uint32_t(event.Id));
  } else
    throw UnsupportedCommand("Saekano audio host cannot consume event kind " +
                             std::to_string(int(event.Kind)) + " id " +
                             std::to_string(event.Id));
  AlCheck("BGM request");
}
void MusicPlayback::Update(float seconds) {
  if (!std::isfinite(seconds) || seconds < 0)
    throw FormatError("invalid audio update interval");
  if (impl_->BgmFade.Active() && !impl_->RenderSamples) {
    impl_->BgmFadeFraction += double(seconds) * 1000.0;
    const double whole = std::floor(impl_->BgmFadeFraction);
    impl_->BgmFadeFraction -= whole;
    impl_->AdvanceBgmFade(uint32_t(std::min(whole, 67.0)));
  }
  impl_->Channel->Update(seconds);
  impl_->OutgoingBgm->Update(seconds);
  for (auto& channel : impl_->SeChannels) if (channel) channel->Update(seconds);
  impl_->ProfileVoice->Update(seconds);
  impl_->SecondaryVoice->Update(seconds);
  impl_->MissionVoice->Update(seconds);
  impl_->UiSound->Update(seconds);
  AlCheck("BGM frame");
}
bool MusicPlayback::Playing() const {
  if (impl_->Channel->GetState() != Audio::ACS_Stopped) return true;
  if (impl_->OutgoingBgm->GetState() != Audio::ACS_Stopped) return true;
  if (impl_->ProfileVoice->GetState() != Audio::ACS_Stopped) return true;
  if (impl_->SecondaryVoice->GetState() != Audio::ACS_Stopped) return true;
  if (MissionVoicePlaying()) return true;
  if (UiSoundPlaying()) return true;
  for (auto& channel : impl_->SeChannels)
    if (channel && channel->GetState() != Audio::ACS_Stopped) return true;
  return false;
}
bool MusicPlayback::SoundPlaying(size_t logicalChannel) const {
  const auto& channel = impl_->SeChannels.at(logicalChannel);
  return channel && channel->GetState() != Audio::ACS_Stopped;
}
BgmStopFade MusicPlayback::BgmFadeState() const { return impl_->BgmFade; }
float MusicPlayback::BgmOutputGain() const {
  return impl_->Channel->GetState()!=Audio::ACS_Stopped || impl_->BgmFade.Phase==0 ?
      impl_->Channel->GetVolume():impl_->OutgoingBgm->GetVolume();
}
BgmPlaybackSnapshot MusicPlayback::BgmVoices() const {
  const bool current=impl_->Channel->GetState()!=Audio::ACS_Stopped;
  const bool outgoing=impl_->OutgoingBgm->GetState()!=Audio::ACS_Stopped;
  return {current?impl_->CurrentBgmSerial:0,outgoing?impl_->OutgoingBgmSerial:0,
      current?impl_->Channel->GetVolume():0,outgoing?impl_->OutgoingBgm->GetVolume():0,
      impl_->BgmFadeOutDuration,impl_->RetiredBgm};
}
void MusicPlayback::BindUiSounds(SystemSoundAssets& resources, float gain) {
  if (!std::isfinite(gain) || gain < 0 || gain > 1)
    throw FormatError("UI sound gain outside supported range");
  impl_->UiSounds = &resources;
  impl_->UiSoundGain = gain;
}
bool MusicPlayback::UiSoundPlaying() const {
  return impl_->UiSound->GetState() != Audio::ACS_Stopped;
}
void MusicPlayback::PrepareProfileVoice(Bytes encoded, float gain) {
  if (!std::isfinite(gain) || gain < 0 || gain > 1)
    throw FormatError("profile voice gain outside supported range");
  auto stream = std::make_unique<HcaPlaybackStream>(std::move(encoded));
  impl_->PreparedOrdinaryVoices[0].reset();
  impl_->HasOrdinaryVoice[0] = false;
  impl_->ProfileVoice->Stop(0);
  impl_->ProfileVoice->SetVolume(gain);
  impl_->PreparedProfileVoice = std::move(stream);
  impl_->HasProfileVoice = true;
  AlCheck("profile voice preparation");
}
void MusicPlayback::StartProfileVoice() {
  if (!impl_->PreparedProfileVoice)
    throw UnsupportedCommand(
        "profile voice must be prepared before each start");
  impl_->ProfileVoice->Play(std::move(impl_->PreparedProfileVoice), false, 0);
  AlCheck("profile voice start");
}
void MusicPlayback::StopProfileVoice() {
  impl_->ProfileVoice->Stop(0);
  AlCheck("profile voice stop");
}
void MusicPlayback::ResetProfileVoice() {
  StopProfileVoice();
  impl_->PreparedProfileVoice.reset();
  impl_->HasProfileVoice = false;
}
bool MusicPlayback::ProfileVoiceStopped() const {
  return impl_->HasProfileVoice &&
         impl_->ProfileVoice->GetState() == Audio::ACS_Stopped;
}
std::optional<float> MusicPlayback::ProfileVoicePosition() const {
  if (!impl_->HasProfileVoice) return std::nullopt;
  return impl_->ProfileVoice->PositionInSeconds();
}
namespace {
size_t OrdinaryVoiceSlot(uint32_t channel) {
  if (channel != 7 && channel != 8)
    throw UnsupportedCommand("ordinary voice channel outside7/8");
  return size_t(channel - 7);
}
}  // namespace
void MusicPlayback::PrepareOrdinaryVoice(
    uint32_t channel, uint32_t voiceId,
    std::unique_ptr<HcaWaveReader> reader, float gain) {
  const auto slot = OrdinaryVoiceSlot(channel);
  if (!reader || !std::isfinite(gain) || gain < 0 || gain > 1)
    throw FormatError("invalid ordinary voice preparation");
  if (voiceId > uint32_t(INT16_MAX)) throw FormatError("ordinary voice ID exceeds slot catalog");
  auto stream = std::make_unique<HcaPlaybackStream>(std::move(reader), int32_t(voiceId), "voice.awb");
  auto& output = slot ? impl_->SecondaryVoice : impl_->ProfileVoice;
  output->Stop(0);
  output->SetVolume(gain);
  if (!slot) {
    impl_->PreparedProfileVoice.reset();
    impl_->HasProfileVoice = false;
  }
  impl_->PreparedOrdinaryVoices[slot] = std::move(stream);
  impl_->HasOrdinaryVoice[slot] = true;
  AlCheck("ordinary voice preparation");
}
void MusicPlayback::StartOrdinaryVoice(uint32_t channel) {
  const auto slot = OrdinaryVoiceSlot(channel);
  auto& output = slot ? impl_->SecondaryVoice : impl_->ProfileVoice;
  if (impl_->PreparedOrdinaryVoices[slot])
    output->Play(std::move(impl_->PreparedOrdinaryVoices[slot]), false, 0);
  else if (impl_->HasOrdinaryVoice[slot]) output->Resume();
  AlCheck("ordinary voice start");
}
void MusicPlayback::StopOrdinaryVoice(uint32_t channel) {
  const auto slot = OrdinaryVoiceSlot(channel);
  (slot ? impl_->SecondaryVoice : impl_->ProfileVoice)->Stop(0);
  impl_->PreparedOrdinaryVoices[slot].reset();
  impl_->HasOrdinaryVoice[slot] = false;
  if (!slot) impl_->PreparedProfileVoice.reset();
  AlCheck("ordinary voice stop");
}
bool MusicPlayback::OrdinaryVoiceReady(uint32_t channel) const {
  return bool(impl_->PreparedOrdinaryVoices[OrdinaryVoiceSlot(channel)]);
}
bool MusicPlayback::OrdinaryVoicePlaying(uint32_t channel) const {
  const auto slot = OrdinaryVoiceSlot(channel);
  return impl_->HasOrdinaryVoice[slot] &&
         (slot ? impl_->SecondaryVoice : impl_->ProfileVoice)->GetState() == Audio::ACS_Playing;
}
int32_t MusicPlayback::OrdinaryVoicePosition(uint32_t channel) const {
  const auto slot = OrdinaryVoiceSlot(channel);
  if (!OrdinaryVoicePlaying(channel)) return -1;
  const auto seconds =
      (slot ? impl_->SecondaryVoice : impl_->ProfileVoice)->PositionInSeconds();
  if (!std::isfinite(seconds) || seconds < 0 || seconds >= 2147483.0f)
    throw FormatError("ordinary voice position outside supported range");
  return int32_t(seconds * 1000.0f);
}
PlaybackOrdinaryVoice::PlaybackOrdinaryVoice(MusicPlayback& playback,
    const std::filesystem::path& usrdir, uint8_t volume, float coefficient)
    : playback_(playback), bank_(std::make_shared<AwbBank>(usrdir / "voice.awb")),
      gain_(ProfileVoiceGain(volume, coefficient)) {
  if (!std::isfinite(gain_) || gain_ < 0 || gain_ > 1)
    throw FormatError("ordinary voice gain outside supported range");
}
void PlaybackOrdinaryVoice::Prepare(uint32_t channel, int16_t voiceId) {
  if (voiceId < 0) throw FormatError("negative ordinary voice ID");
  auto reader = std::make_unique<HcaWaveReader>(bank_, uint32_t(voiceId));
  playback_.PrepareOrdinaryVoice(channel, uint32_t(voiceId), std::move(reader), gain_);
}
void PlaybackOrdinaryVoice::Start(uint32_t channel) {
  playback_.StartOrdinaryVoice(channel);
}
void PlaybackOrdinaryVoice::Stop(uint32_t channel) {
  playback_.StopOrdinaryVoice(channel);
}
bool PlaybackOrdinaryVoice::Ready(uint32_t channel) const {
  return playback_.OrdinaryVoiceReady(channel);
}
bool PlaybackOrdinaryVoice::Playing(uint32_t channel) const {
  return playback_.OrdinaryVoicePlaying(channel);
}
int32_t PlaybackOrdinaryVoice::Position(uint32_t channel) const {
  return playback_.OrdinaryVoicePosition(channel);
}
void MusicPlayback::PlayMissionVoice(uint32_t id,std::unique_ptr<HcaWaveReader> reader,float gain) {
  PrepareMissionVoice(id,std::move(reader),gain);StartMissionVoice();
}
void MusicPlayback::PrepareMissionVoice(uint32_t id,std::unique_ptr<HcaWaveReader> reader,float gain) {
  if(!reader || id>uint32_t(INT16_MAX) || !std::isfinite(gain) || gain<0 || gain>1)
    throw FormatError("invalid mission voice preparation");
  auto stream=std::make_unique<HcaPlaybackStream>(std::move(reader),int32_t(id),"voice.awb/mission11");
  impl_->MissionVoice->Stop(0);impl_->MissionVoice->SetVolume(gain);
  impl_->PreparedMissionVoice=std::move(stream);impl_->HasMissionVoice=false;
  AlCheck("mission voice prepare");
}
void MusicPlayback::StartMissionVoice() {
  if(impl_->PreparedMissionVoice) {
    impl_->MissionVoice->Play(std::move(impl_->PreparedMissionVoice),false,0);impl_->HasMissionVoice=true;
  } else if(impl_->HasMissionVoice)impl_->MissionVoice->Resume();
  AlCheck("mission voice play");
}
bool MusicPlayback::MissionVoiceReady() const { return bool(impl_->PreparedMissionVoice); }
void MusicPlayback::ResetMissionVoice() {
  impl_->MissionVoice->Stop(0);impl_->PreparedMissionVoice.reset();impl_->HasMissionVoice=false;AlCheck("mission voice reset");
}
bool MusicPlayback::MissionVoicePlaying() const {
  return impl_->HasMissionVoice && impl_->MissionVoice->GetState()!=Audio::ACS_Stopped;
}
int32_t MusicPlayback::MissionVoicePosition() const {
  if(!impl_->HasMissionVoice)return -1;
  const auto seconds=impl_->MissionVoice->PositionInSeconds();
  return std::isfinite(seconds) && seconds>=0 ? int32_t(seconds*1000) : -1;
}
PlaybackMissionVoice::PlaybackMissionVoice(MusicPlayback& playback,
    const std::filesystem::path& usrdir,uint8_t volume,float coefficient)
    : playback_(playback),bank_(std::make_shared<AwbBank>(usrdir/"voice.awb")),
      gain_(ProfileVoiceGain(volume,coefficient)) {
  if(!std::isfinite(gain_) || gain_<0 || gain_>1)throw FormatError("mission voice gain outside supported range");
}
void PlaybackMissionVoice::Register(std::span<const int32_t> ids) {
  std::set<int32_t> prepared;
  for(const auto id:ids) {
    if(id<0)continue;
    if(id>INT16_MAX)throw FormatError("mission voice ID exceeds verified signed16 range");
    (void)bank_->Index().Find(uint32_t(id));prepared.insert(id);
  }
  catalog_=std::move(prepared);
}
void PlaybackMissionVoice::Begin(int32_t id,uint64_t now) { Prepare(id);Start(now); }
void PlaybackMissionVoice::Prepare(int32_t id) {
  if(!catalog_.contains(id))throw UnsupportedCommand("mission voice was not registered at script load");
  auto reader=std::make_unique<HcaWaveReader>(bank_,uint32_t(id));
  playback_.PrepareMissionVoice(uint32_t(id),std::move(reader),gain_);
}
void PlaybackMissionVoice::Start(uint64_t) { playback_.StartMissionVoice(); }
void PlaybackMissionVoice::Disable() { playback_.ResetMissionVoice(); }
PlaybackProfileVoice::PlaybackProfileVoice(MusicPlayback& playback,
                                           uint8_t option,
                                           float channelCoefficient)
    : playback_(playback), gain_(ProfileVoiceGain(option, channelCoefficient)) {
  if (!std::isfinite(gain_) || gain_ < 0 || gain_ > 1)
    throw FormatError("profile voice effective gain outside PC output range");
}
void PlaybackProfileVoice::Reset() {
  if (present_) playback_.ResetProfileVoice();
  present_ = false;
}
void PlaybackProfileVoice::Prepare(Bytes encoded) {
  playback_.PrepareProfileVoice(std::move(encoded), gain_);
  present_ = true;
}
void PlaybackProfileVoice::Start(uint64_t) {
  if (!present_)
    throw UnsupportedCommand("profile voice output is not prepared");
  playback_.StartProfileVoice();
}
void PlaybackProfileVoice::Stop() {
  if (!present_)
    throw FormatError("profile voice output has no stop acknowledgement");
  playback_.StopProfileVoice();
}
bool PlaybackProfileVoice::Stopped() const {
  return present_ && playback_.ProfileVoiceStopped();
}
void MusicPlayback::Render(std::span<int16_t> output) {
  if (!impl_->RenderSamples || output.size() % 2)
    throw FormatError("invalid stereo capture request");
  size_t frame = 0;
  while (frame < output.size() / 2) {
    const auto count = std::min<size_t>(480, output.size() / 2 - frame);
    if (impl_->BgmFade.Active()) {
      impl_->BgmFadeFrameRemainder += uint32_t(count);
      impl_->AdvanceBgmFade(impl_->BgmFadeFrameRemainder / 48);
      impl_->BgmFadeFrameRemainder %= 48;
    }
    Update(float(count) / 48000);
    impl_->RenderSamples(impl_->Device, output.data() + frame * 2,
                         ALCsizei(count));
    if (alcGetError(impl_->Device) != ALC_NO_ERROR)
      throw FormatError("OpenAL capture failed");
    frame += count;
  }
}

int RunMissionVoicePrepareSession(const std::filesystem::path& usrdir,
                                  const std::filesystem::path& captureDirectory) {
  bool created=false;
  try {
    CreateCaptureDirectory(usrdir,captureDirectory);created=true;
    if(!SDL_Init(SDL_INIT_EVENTS))throw FormatError(SDL_GetError());
    struct Cleanup { ~Cleanup(){SDL_Quit();} } cleanup;
    ArchiveBgmResources music(usrdir);MusicPlayback playback(music,true);
    PlaybackMissionVoice voice(playback,usrdir,10,1.0f);
    const std::array<int32_t,1> ids{10580};voice.Register(ids);voice.Start(0);
    if(playback.MissionVoicePlaying() || playback.MissionVoicePosition()!=-1)
      throw FormatError("empty PC mission Start mutated the channel");
    voice.Prepare(10580);
    std::vector<int16_t> before(9600*2);playback.Render(before);
    if(!playback.MissionVoiceReady() || playback.MissionVoicePlaying() || playback.MissionVoicePosition()!=-1)
      throw FormatError("PC mission preparation started playback");
    bool rejected=false;try{voice.Prepare(10581);}catch(const UnsupportedCommand&){rejected=true;}
    if(!rejected || !playback.MissionVoiceReady())throw FormatError("invalid preparation disturbed PC ready voice");
    voice.Start(200);
    if(playback.MissionVoiceReady() || !playback.MissionVoicePlaying())throw FormatError("PC mission Start failed");
    std::vector<int16_t> body(24000*2);playback.Render(body);
    const auto position=playback.MissionVoicePosition();
    voice.Disable();voice.Start(700);
    std::vector<int16_t> tail(9600*2);playback.Render(tail);
    if(playback.MissionVoiceReady() || playback.MissionVoicePlaying() || playback.MissionVoicePosition()!=-1)
      throw FormatError("disabled PC mission retained a source");
    const auto peak=[](std::span<const int16_t> pcm) {
      int32_t result=0;for(const auto v:pcm)result=std::max(result,std::abs(int32_t(v)));return result;
    };
    const auto beforePeak=peak(before),bodyPeak=peak(body),tailPeak=peak(std::span(tail).last(9600));
    if(beforePeak>1 || bodyPeak<=32 || tailPeak>1 || position<450 || position>550)
      throw FormatError("mission prepare/start PCM or position check failed");
    std::vector<int16_t> pcm;pcm.insert(pcm.end(),before.begin(),before.end());
    pcm.insert(pcm.end(),body.begin(),body.end());pcm.insert(pcm.end(),tail.begin(),tail.end());
    SaveWav(captureDirectory/"prepare-start-stop.wav",pcm);
    std::ofstream report(captureDirectory/"mission-prepare.json");
    report << "{\"voice\":10580,\"prepared_output_frames\":9600,\"started_output_frames\":24000,"
           << "\"tail_output_frames\":9600,\"prepared_peak\":" << beforePeak
           << ",\"playing_peak\":" << bodyPeak << ",\"settled_tail_peak\":" << tailPeak
           << ",\"started_position_ms\":" << position
           << ",\"unregistered_preserves_ready\":true,\"disabled_start_noop\":true}\n";
    return 0;
  }catch(const std::exception& e) {
    if(created){std::ofstream error(captureDirectory/"error.txt");error << std::string(e.what()).substr(0,2048);}
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Saekano mission voice preparation: %s",e.what());return 1;
  }
}
int RunMissionVoiceSession(const std::filesystem::path& usrdir,
                           const std::filesystem::path& captureDirectory,bool lifecycleOnly) {
  bool created=false;
  try {
    CreateCaptureDirectory(usrdir,captureDirectory);created=true;
    if(!SDL_Init(SDL_INIT_EVENTS))throw FormatError(SDL_GetError());
    struct Cleanup { ~Cleanup(){SDL_Quit();} } cleanup;
    ArchiveBgmResources music(usrdir);MusicPlayback playback(music,true);
    auto bank=std::make_shared<AwbBank>(usrdir/"voice.awb");
    const auto info=HcaWaveReader(bank,10579).Info();
    std::ofstream report(captureDirectory/"mission.json");
    report << "{\"voice\":10579,\"source_rate\":" << info.SampleRate
           << ",\"source_frames\":" << info.Frames << ",\"gains\":[";
    const std::array<int32_t,1> ids{10579};
    if(!lifecycleOnly)for(const auto volume:{uint8_t(0),uint8_t(5),uint8_t(10)}) {
      PlaybackMissionVoice voice(playback,usrdir,volume,1.0f);voice.Register(ids);
      voice.Begin(10579,0);
      if(!playback.MissionVoicePlaying())throw FormatError("mission source did not start");
      const auto expected=(info.Frames*48000+info.SampleRate-1)/info.SampleRate;
      if(expected>48000*20)throw FormatError("mission cue diagnostic duration budget exceeded");
      std::vector<int16_t> pcm;
      while(playback.MissionVoicePlaying()) {
        if(pcm.size()/2>expected+24000)throw FormatError("mission cue did not finish within source duration budget");
        std::array<int16_t,960> block;playback.Render(block);
        pcm.insert(pcm.end(),block.begin(),block.end());
      }
      SaveWav(captureDirectory/("gain-"+std::to_string(volume)+".wav"),pcm);
      if(volume)report << ',';report << voice.Gain();voice.Reset();
    }
    PlaybackOrdinaryVoice ordinary(playback,usrdir,10,1.0f);
    ordinary.Prepare(7,5322);ordinary.Start(7);
    PlaybackMissionVoice voice(playback,usrdir,10,1.0f);voice.Register(ids);voice.Begin(10579,0);
    std::vector<int16_t> block(4800*2);playback.Render(block);
    const auto before=ordinary.Position(7);
    bool rejected=false;try{voice.Begin(10580,0);}catch(const UnsupportedCommand&){rejected=true;}
    if(!rejected || !playback.MissionVoicePlaying())throw FormatError("unregistered cue disturbed current mission source");
    voice.Disable();playback.Render(block);
    const auto after=ordinary.Position(7);
    // Start consumes the prepared reader. Mission stop must preserve the
    // playing dialogue source, without recreating its consumed preparation.
    if(!ordinary.Playing(7) || ordinary.Ready(7) || before<0 || after<=before ||
        playback.MissionVoicePlaying() || playback.MissionVoicePosition()!=-1)
      throw FormatError("mission independence: before="+std::to_string(before)+", after="+std::to_string(after)+
          ", dialogue_playing="+std::to_string(ordinary.Playing(7))+", ready="+std::to_string(ordinary.Ready(7))+
          ", mission_playing="+std::to_string(playback.MissionVoicePlaying())+
          ", mission_position="+std::to_string(playback.MissionVoicePosition()));
    ordinary.Stop(7);playback.Render(block);  // Settle both stop ramps.
    voice.Begin(10579,0);playback.Render(block);voice.Disable();
    std::vector<int16_t> tail(9600*2);playback.Render(tail);SaveWav(captureDirectory/"stop-tail.wav",tail);
    report << "],\"unregistered_rejected\":true,\"dialogue_before_ms\":" << before
           << ",\"dialogue_after_ms\":" << after
           << ",\"independent_dialogue\":true,\"reset_missing_position\":true}\n";
    return 0;
  }catch(const std::exception& e) {
    if(created){std::ofstream error(captureDirectory/"error.txt");error << std::string(e.what()).substr(0,2048);}
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Saekano mission voice: %s",e.what());return 1;
  }
}
int RunUiSoundSession(const std::filesystem::path& usrdir,
                      const std::filesystem::path& captureDirectory) {
  bool created = false;
  try {
    CreateCaptureDirectory(usrdir, captureDirectory);
    created = true;
    std::ifstream first(usrdir / "first.dat", std::ios::binary);
    if (!first) throw FormatError("cannot open original UI sound bank");
    Bytes bytes{std::istreambuf_iterator<char>(first), {}};
    if (first.bad()) throw FormatError("cannot read original UI sound bank");
    SystemSoundAssets sounds(std::move(bytes));
    ArchiveBgmResources music(usrdir);
    if (!SDL_Init(SDL_INIT_EVENTS)) throw FormatError(SDL_GetError());
    struct Cleanup {
      ~Cleanup() { SDL_Quit(); }
    } cleanup;
    MusicPlayback playback(music, true);
    const float gain = ProfileVoiceGain(8, 1.0f);
    playback.BindUiSounds(sounds, gain);
    auto play = [&](int32_t id) {
      playback.Submit({GameEventKind::Sound, id});
    };
    auto render = [&](std::vector<int16_t>& pcm, size_t frames) {
      const auto old = pcm.size();
      pcm.resize(old + frames * 2);
      playback.Render(std::span(pcm).subspan(old));
    };
    auto finish = [&](std::vector<int16_t>& pcm) {
      size_t ticks = 0;
      while (playback.UiSoundPlaying() && ticks++ < 500) render(pcm, 480);
      if (playback.UiSoundPlaying())
        throw FormatError("UI sound did not reach EOF");
      render(pcm, 2400);
    };
    std::array<size_t, 2> counts{};
    for (size_t i = 0; i < 2; ++i) {
      const int32_t id = i == 0 ? 3 : 6;
      play(id);
      std::vector<int16_t> pcm;
      finish(pcm);
      counts[i] = pcm.size() / 2;
      SaveWav(captureDirectory / ("sound" + std::to_string(id) + ".wav"), pcm);
    }
    play(3);
    std::vector<int16_t> replacement;
    render(replacement, 4800);
    bool rejected = false;
    try {
      play(-1);
    } catch (const FormatError&) {
      rejected = true;
    }
    const bool preserved = rejected && playback.UiSoundPlaying();
    if (!preserved)
      throw FormatError("invalid UI request changed live channel");
    play(6);
    finish(replacement);
    SaveWav(captureDirectory / "replacement.wav", replacement);
    std::ofstream out(captureDirectory / "summary.json");
    out << "{\"channel\":6,\"bank_entries\":" << sounds.Count()
        << ",\"gain_bits\":" << std::bit_cast<uint32_t>(gain)
        << ",\"sample_rate\":48000,\"channels\":2,\"frames\":[" << counts[0]
        << ',' << counts[1]
        << "],\"replacement_at_frame\":4800,\"replacement_frames\":"
        << replacement.size() / 2
        << ",\"invalid_request_preserves_channel\":true,\"stopped\":true}\n";
    if (!out) throw FormatError("cannot write UI sound summary");
    return 0;
  } catch (const std::exception& error) {
    if (created) {
      std::ofstream out(captureDirectory / "error.txt");
      out << error.what() << '\n';
    }
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Saekano UI sound: %s",
                 error.what());
    return 1;
  }
}
int RunBgmReplacementSession(const std::filesystem::path& usrdir,const std::filesystem::path& captureDirectory) {
  bool created=false;
  try {
    CreateCaptureDirectory(usrdir,captureDirectory);created=true;
    if(!SDL_Init(SDL_INIT_EVENTS))throw FormatError(SDL_GetError());
    struct Cleanup { ~Cleanup(){SDL_Quit();} } cleanup;
    ArchiveBgmResources resources(usrdir);resources.Prepare(8);MusicPlayback playback(resources,true);
    std::ofstream report(captureDirectory/"replacement.jsonl");std::vector<int16_t> samples;
    const auto play=[&](int32_t volume=100){GameEvent e{GameEventKind::PlayBgm,8};
      e.Bgm=BgmRequest(8,volume,100,resources.Coefficient());playback.Submit(e);};
    const auto render=[&](size_t ms){const auto at=samples.size();samples.resize(at+ms*96);
      playback.Render(std::span(samples).subspan(at));};
    const auto snapshot=[&](const char* label){const auto s=playback.BgmVoices();const auto f=playback.BgmFadeState();
      report << "{\"label\":\"" << label << "\",\"current\":" << s.Current << ",\"outgoing\":" << s.Outgoing
        << ",\"current_gain\":" << s.CurrentGain << ",\"outgoing_gain\":" << s.OutgoingGain
        << ",\"duration\":" << s.FadeOutDuration << ",\"phase\":" << f.Phase << ",\"elapsed\":" << f.Elapsed
        << ",\"retired\":[";
      for(size_t i=0;i<s.Retired.size();++i){if(i)report << ',';report << s.Retired[i];}report << "]}\n";};
    play();render(100);snapshot("initial");
    playback.Submit({GameEventKind::StopBgm,0,{},500});render(512);snapshot("silent_retirement");
    play();snapshot("replace_silent");render(0);
    play();render(100);snapshot("overlap_current");
    play();snapshot("replace_active_overlap");
    playback.Submit({GameEventKind::StopBgm,0,{},500});snapshot("stop_overlap");
    playback.Submit({GameEventKind::StopBgm,0,{},500});snapshot("repeat_stop");render(32);
    if(playback.Playing())throw FormatError("repeated BGM stop retained a voice");
    play();render(16);play(50);snapshot("shared_volume");
    const auto before=playback.BgmVoices();GameEvent invalid{GameEventKind::PlayBgm,8};
    invalid.Bgm=BgmRequest(8,100,100,resources.Coefficient());invalid.Bgm->Volume->Gain=2;
    bool rejected=false;try{playback.Submit(invalid);}catch(const UnsupportedCommand&){rejected=true;}
    const auto after=playback.BgmVoices();
    if(!rejected || before.Current!=after.Current || before.Outgoing!=after.Outgoing ||
        before.Retired!=after.Retired || before.CurrentGain!=after.CurrentGain || before.OutgoingGain!=after.OutgoingGain)
      throw FormatError("rejected BGM input disturbed live voices");
    snapshot("rejected_input_preserved");SaveWav(captureDirectory/"replacement.wav",samples);
    return 0;
  } catch(const std::exception& error) {
    if(created){std::ofstream out(captureDirectory/"error.txt");out << error.what() << '\n';}
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Saekano BGM replacement: %s",error.what());return 1;
  }
}
int RunBgmFadeSession(const std::filesystem::path& usrdir, uint32_t track,
                       const std::filesystem::path& captureDirectory) {
  bool created = false;
  try {
    CreateCaptureDirectory(usrdir, captureDirectory); created = true;
    ArchiveBgmResources resources(usrdir); resources.Prepare(int32_t(track));
    if (!SDL_Init(SDL_INIT_EVENTS)) throw FormatError(SDL_GetError());
    struct Cleanup { ~Cleanup() { SDL_Quit(); } } cleanup;
    const auto request = BgmRequest(int32_t(track), 100, 100, resources.Coefficient());
    for (uint32_t scenario = 0; scenario < 3; ++scenario) {
      MusicPlayback playback(resources, true);
      GameEvent play{GameEventKind::PlayBgm, int32_t(track)}; play.Bgm = request;
      playback.Submit(play);
      std::vector<int16_t> samples;
      std::ofstream trace(captureDirectory / ("fade-" + std::to_string(scenario) + ".jsonl"));
      for (uint32_t step = 0; step < 50; ++step) {
        if (scenario && (step == 10 || (scenario == 2 && step == 15)))
          playback.Submit({GameEventKind::StopBgm, 0, {}, 1000});
        if (scenario && step == 11) {
          const auto before = playback.BgmFadeState();
          bool rejected = false;
          try { playback.Submit({GameEventKind::StopBgm, 0, {}, -1}); }
          catch (const UnsupportedCommand&) { rejected = true; }
          const auto after = playback.BgmFadeState();
          if (!rejected || before.Elapsed != after.Elapsed || before.Phase != after.Phase || before.Weight != after.Weight)
            throw FormatError("invalid fade request changed playback");
        }
        playback.Update(0.05f);
        const auto old = samples.size(); samples.resize(old + 4800);
        playback.Render(std::span(samples).subspan(old));
        const auto fade = playback.BgmFadeState();
        trace << "{\"now\":" << (step + 1) * 50 << ",\"phase\":" << fade.Phase
              << ",\"elapsed\":" << fade.Elapsed << ",\"weight_bits\":" << std::bit_cast<uint32_t>(fade.Weight)
              << ",\"gain_bits\":" << std::bit_cast<uint32_t>(playback.BgmOutputGain())
              << ",\"playing\":" << (playback.Playing() ? "true" : "false") << "}\n";
      }
      SaveWav(captureDirectory / ("fade-" + std::to_string(scenario) + ".wav"), samples);
      if (!trace) throw FormatError("cannot write fade trace");
    }
    return 0;
  } catch (const std::exception& error) {
    if (created) { std::ofstream out(captureDirectory / "error.txt"); out << error.what() << '\n'; }
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Saekano BGM fade: %s", error.what());
    return 1;
  }
}
int RunMusicSession(const std::filesystem::path& usrdir, uint32_t track,
                    const std::filesystem::path& captureDirectory,
                    std::optional<uint32_t> soundChannel) {
  try {
    const bool capture = !captureDirectory.empty();
    ArchiveBgmResources resources(usrdir);
    std::unique_ptr<ArchiveSeResources> sounds;
    if (soundChannel) {
      if (*soundChannel > 3) throw FormatError("SE channel must be0..3");
      sounds = std::make_unique<ArchiveSeResources>(usrdir);
      sounds->Prepare(int32_t(track));
    } else resources.Prepare(int32_t(track));
    if (capture) CreateCaptureDirectory(usrdir,captureDirectory);
    if (!SDL_Init(SDL_INIT_EVENTS)) throw FormatError(SDL_GetError());
    struct SdlCleanup {
      ~SdlCleanup() { SDL_Quit(); }
    } cleanup;
    MusicPlayback playback(resources, capture, sounds.get());
    SessionScript scripts(track, soundChannel);
    HeadlessBackend backend;
    backend.MusicResources = &resources;
    backend.SeArchives = sounds.get();
    SemanticState state;
    state.MusicHistory.ExtentKnown = true;
    state.BgmMasterVolume = 80;
    state.SeMasterVolume = 80;
    VirtualMachine vm(scripts, backend, state);
    std::vector<int16_t> samples;
    uint32_t playRequests = 0, stopRequests = 0, completed = 0, gain = 0;
    bool otherPlayingAtFirstStop = false;
    const auto epoch = SDL_GetTicks();
    bool ended = false;
    for (uint64_t frame = 0; frame < 240 && !ended; ++frame) {
      const uint64_t now = capture ? frame * 1000 / 60 : SDL_GetTicks() - epoch;
      for (size_t steps = 0; steps < 32; ++steps) {
        const auto event = vm.Step(now);
        for (const auto& request : vm.TakeEvents()) {
          playback.Submit(request);
          if (request.Kind == GameEventKind::PlaySe) {
            ++playRequests;
            gain = std::bit_cast<uint32_t>(request.Se->Volume->Gain);
          } else if (request.Kind == GameEventKind::PlayBgm) {
            ++playRequests;
            gain = std::bit_cast<uint32_t>(request.Bgm->Volume->Gain);
          } else if (request.Kind == GameEventKind::StopBgm || request.Kind == GameEventKind::StopSe) {
            if (soundChannel && stopRequests == 0)
              otherPlayingAtFirstStop = playback.SoundPlaying((*soundChannel + 1) % 4);
            ++stopRequests;
          }
        }
        if (event.Boundary == VmBoundary::CommandCompletedBeforeDispatch)
          ++completed;
        if (event.Boundary == VmBoundary::Ended) {
          ended = true;
          break;
        }
        if (event.Boundary == VmBoundary::WaitingBackend ||
            event.Boundary == VmBoundary::WaitingInput)
          break;
      }
      if (capture) {
        const auto offset = samples.size();
        samples.resize(offset + 1600);
        playback.Render(std::span(samples).subspan(offset));
      } else {
        playback.Update(1.0f / 60);
        SDL_Delay(16);
        SDL_Event event;
        while (SDL_PollEvent(&event))
          if (event.type == SDL_EVENT_QUIT) return 0;
      }
    }
    if (!ended || playback.Playing() || playRequests != (soundChannel ? 2u : 1u) ||
        stopRequests != (soundChannel ? 2u : 1u) || completed != (soundChannel ? 7u : 3u))
      throw FormatError(
          "audio VM session did not finish playback/wait/stop sequence");
    if (capture) {
      SaveWav(captureDirectory / "audio.wav", samples);
      std::ofstream report(captureDirectory / "summary.json");
      report << "{\"track\":" << track
             << ",\"sample_rate\":48000,\"channels\":2,\"frames\":"
             << samples.size() / 2 << ",\"gain_bits\":" << gain
             << ",\"play_requests\":" << playRequests
             << ",\"stop_requests\":" << stopRequests
             << ",\"completed_commands\":" << completed << ",\"history\":[";
      for (size_t i = 0; i < vm.State().MusicHistory.Entries.size(); ++i) {
        if (i) report << ',';
        report << ExactMusicStatus(vm.State().MusicHistory, i);
      }
      report << "],\"stopped\":true";
      if (soundChannel) report << ",\"sound_channel\":" << *soundChannel
          << ",\"other_playing_at_first_stop\":" << (otherPlayingAtFirstStop ? "true" : "false")
          << ",\"remembered_track\":" << *vm.State().SeTrack
          << ",\"remembered_channel\":" << *vm.State().SeChannel;
      report << "}\n";
      if (!report) throw FormatError("cannot write audio session report");
    }
    SDL_Log(
        "Saekano BGM session completed through the shared VM and Impacto audio "
        "channel");
    return 0;
  } catch (const std::exception& error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Saekano audio: %s",
                 error.what());
    return 1;
  }
}
int RunOrdinaryVoiceStopSession(const std::filesystem::path& usrdir,
                               const std::filesystem::path& captureDirectory) {
  bool created = false;
  try {
    CreateCaptureDirectory(usrdir, captureDirectory); created = true;
    ArchiveBgmResources resources(usrdir);
    if (!SDL_Init(SDL_INIT_EVENTS)) throw FormatError(SDL_GetError());
    struct Cleanup { ~Cleanup() { SDL_Quit(); } } cleanup;
    MusicPlayback playback(resources, true);
    PlaybackOrdinaryVoice voice(playback, usrdir, 10, 1.0f);
    auto queue = MakeFreshOrdinaryVoiceQueue();
    queue.Catalog = {7490, 7491, 7492};
    (void)PrepareOrdinaryVoice(queue, 7490, true, voice);
    // Reproduce the retired-cache bug before exercising the corrected owner.
    voice.Stop(7); voice.Stop(8);
    const auto staleSlot = PrepareOrdinaryVoice(queue, 7491, true, voice);
    if (voice.Playing(queue.BaseChannel + staleSlot))
      throw FormatError("stale prepared-voice reproduction unexpectedly played");
    voice.Stop(7); voice.Stop(8);
    queue = MakeFreshOrdinaryVoiceQueue(); queue.Catalog = {7490, 7491, 7492};
    (void)PrepareOrdinaryVoice(queue, 7490, true, voice);
    StopOrdinaryVoiceChannel(queue, voice, 7);
    StopOrdinaryVoiceChannel(queue, voice, 8);
    if (!queue.Prepared.empty() || voice.Ready(7) || voice.Ready(8))
      throw FormatError("external stop retained lookahead");
    std::vector<int16_t> samples;
    uint32_t lastChannel = 7;
    for (const auto id : {7491, 7492}) {
      const auto slot = PrepareOrdinaryVoice(queue, int16_t(id), true, voice);
      const auto channel = queue.BaseChannel + slot;
      lastChannel = channel;
      if (!voice.Playing(channel)) throw FormatError("stopped lookahead did not reprepare");
      const auto old = samples.size(); samples.resize(old + 9600);
      playback.Render(std::span(samples).subspan(old));
      if (!voice.Playing(channel) || voice.Position(channel) < 95)
        throw FormatError("recovered voice did not advance");
      if (id == 7491) {
        StopOrdinaryVoiceChannel(queue, voice, queue.BaseChannel + (slot ^ 1));
        if (!queue.Prepared.empty() || !voice.Playing(channel))
          throw FormatError("pending-slot stop affected current voice");
      }
    }
    bool rejected = false;
    try { StopOrdinaryVoiceChannel(queue, voice, 9); }
    catch (const UnsupportedCommand&) { rejected = true; }
    if (!rejected || !voice.Playing(lastChannel)) throw FormatError("invalid channel stop changed playback");
    SaveWav(captureDirectory / "recovered-voices.wav", samples);
    std::ofstream report(captureDirectory / "result.json");
    report << "{\"passed\":true,\"stale_cache_failure_reproduced\":true,"
              "\"recovered_voice_ids\":[7491,7492],\"pending_stop_preserves_current\":true,"
              "\"invalid_channel_preserves_current\":true,\"pcm_frames\":9600}\n";
    return 0;
  } catch (const std::exception& error) {
    if (created) { std::ofstream out(captureDirectory / "error.txt"); out << error.what() << '\n'; }
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Saekano voice stop: %s", error.what());
    return 1;
  }
}
int RunOrdinaryVoiceSession(const std::filesystem::path& usrdir,
                            const std::filesystem::path& captureDirectory) {
  bool created = false;
  try {
    CreateCaptureDirectory(usrdir, captureDirectory);
    created = true;
    std::ifstream file(usrdir / "resource.dat", std::ios::binary);
    if (!file) throw FormatError("cannot open script voice resources");
    Bytes raw{std::istreambuf_iterator<char>(file), {}};
    if (file.bad()) throw FormatError("cannot read script voice resources");
    GameResources resources(std::move(raw));
    const auto envelopes = resources.LoadScriptVoiceEnvelopes(21);
    if (envelopes.size() < 3) throw FormatError("insufficient script voice catalog");
    auto queue = MakeFreshOrdinaryVoiceQueue();
    for (const auto& envelope : envelopes)
      queue.Catalog.push_back(int16_t(envelope.VoiceId));
    const auto first = queue.Catalog[0], second = queue.Catalog[1];
    ArchiveBgmResources music(usrdir);
    if (!SDL_Init(SDL_INIT_EVENTS)) throw FormatError(SDL_GetError());
    struct Cleanup { ~Cleanup() { SDL_Quit(); } } cleanup;
    MusicPlayback playback(music, true);
    const auto settings = MakeFreshOrdinaryVoiceSettings();
    PlaybackOrdinaryVoice voice(playback, usrdir, settings.Volume, 1.0f);
    voice.Start(7);  // Missing native handle: harmless, without playback.
    if (voice.Ready(7) || voice.Playing(7) || voice.Position(7) != -1)
      throw FormatError("missing ordinary voice acknowledged");
    const auto firstSlot = PrepareOrdinaryVoice(queue, first, true, voice);
    if (firstSlot != 0 || !voice.Playing(7) || !voice.Ready(8) || voice.Playing(8))
      throw FormatError("ordinary initial/lookahead state mismatch");
    std::vector<int16_t> early(4800 * 2);
    playback.Render(std::span(early).first(4800));
    const auto before = voice.Position(7);
    voice.Start(7);  // Must not restart at the beginning.
    playback.Render(std::span(early).subspan(4800));
    const auto after = voice.Position(7);
    if (before < 45 || after < 95 || after <= before)
      throw FormatError("repeated ordinary start reset position");
    bool invalidPrepare = false;
    try { voice.Prepare(8, INT16_MAX); }
    catch (const FormatError&) { invalidPrepare = true; }
    if (!invalidPrepare || !voice.Playing(7) || !voice.Ready(8))
      throw FormatError("invalid preparation interrupted ordinary voices");
    SaveWav(captureDirectory / "early.wav", early);
    const auto secondSlot = PrepareOrdinaryVoice(queue, second, true, voice);
    if (secondSlot != 1 || !voice.Playing(8) || !voice.Ready(7) || voice.Playing(7))
      throw FormatError("ordinary cached-slot reuse mismatch");
    auto bank = std::make_shared<AwbBank>(usrdir / "voice.awb");
    const auto secondInfo = HcaWaveReader(bank, uint32_t(second)).Info();
    if (!secondInfo.SampleRate || secondInfo.Frames / secondInfo.SampleRate > 30)
      throw FormatError("ordinary voice diagnostic duration budget");
    std::vector<int16_t> output(4800 * 2);
    playback.Render(output);
    voice.Start(7);  // Independently start the lookahead for a bounded overlap.
    const auto offset = output.size();
    output.resize(offset + 4800 * 2);
    playback.Render(std::span(output).subspan(offset));
    const auto pos7 = voice.Position(7), pos8 = voice.Position(8);
    if (pos7 < 95 || pos8 < 195 || pos8 - pos7 < 90)
      throw FormatError("ordinary voice positions are not independent");
    voice.Stop(7);
    if (voice.Ready(7) || voice.Playing(7) || !voice.Playing(8))
      throw FormatError("ordinary stop affected the other slot");
    const auto expectedFrames =
        (secondInfo.Frames * 48000 + secondInfo.SampleRate - 1) / secondInfo.SampleRate;
    while (voice.Playing(8)) {
      if (output.size() / 2 > expectedFrames + 48000)
        throw FormatError("ordinary voice failed to reach EOF");
      const auto next = output.size();
      output.resize(next + 1600);
      playback.Render(std::span(output).subspan(next));
    }
    if (voice.Ready(8) || voice.Position(8) != -1)
      throw FormatError("ordinary natural EOF state mismatch");
    voice.Prepare(7, queue.Catalog[2]);
    if (!voice.Ready(7)) throw FormatError("ordinary stop fixture not prepared");
    GameEvent stop;
    stop.Kind = GameEventKind::StopChannel;
    stop.Id = 7;
    stop.Parameter = 0;
    playback.Submit(stop);
    voice.Start(7);
    voice.Stop(8);
    if (voice.Ready(7) || voice.Playing(7) || playback.Playing())
      throw FormatError("common channel stop retained prepared ordinary voice");
    std::vector<int16_t> silence(4800 * 2);
    playback.Render(silence);
    int peak = 0;
    for (auto sample : std::span(silence).last(512))
      peak = std::max(peak, std::abs(int(sample)));
    if (peak > 1) throw FormatError("ordinary stop output did not settle");
    SaveWav(captureDirectory / "audio.wav", output);
    SaveWav(captureDirectory / "stopped.wav", silence);
    std::ofstream report(captureDirectory / "summary.json");
    report << "{\"first_id\":" << first << ",\"second_id\":" << second
           << ",\"source_frames\":" << secondInfo.Frames
           << ",\"source_rate\":" << secondInfo.SampleRate
           << ",\"expected_frames\":" << expectedFrames
           << ",\"rendered_frames\":" << output.size()/2
           << ",\"positions_ms\":[" << before << ',' << after << ',' << pos7 << ',' << pos8
           << "],\"settled_stop_peak\":" << peak
           << ",\"invalid_prepare_preserves_both\":true,\"lookahead_reused\":true"
              ",\"repeated_start_retains_position\":true,\"independent_stop\":true"
              ",\"common_stop_clears_prepared\":true,\"natural_end\":true}\n";
    if (!report) throw FormatError("cannot write ordinary voice report");
    return 0;
  } catch (const std::exception& error) {
    if (created) {
      std::ofstream report(captureDirectory / "error.txt");
      report << error.what() << '\n';
    }
    ImpLog(LogLevel::Error, LogChannel::General, "ordinary voice: %s\n", error.what());
    return 1;
  }
}

int RunProfileVoiceSession(const std::filesystem::path& usrdir,
                           uint32_t character, uint32_t entry,
                           const std::filesystem::path& captureDirectory) {
  bool captureCreated = false;
  try {
    const bool capture = !captureDirectory.empty();
    if (capture) {
      CreateCaptureDirectory(usrdir, captureDirectory);
      captureCreated = true;
    }
    std::ifstream file(usrdir / "resource.dat", std::ios::binary);
    if (!file) throw FormatError("cannot open profile voice resources");
    Bytes raw{std::istreambuf_iterator<char>(file), {}};
    if (file.bad()) throw FormatError("cannot read profile voice resources");
    GameResources resources(std::move(raw));
    auto assets = resources.LoadProfileVoice(character, entry);
    HcaWaveReader metadata(assets.Hca);
    const auto info = metadata.Info();
    if (!info.SampleRate || !info.Frames || info.Frames / info.SampleRate > 120)
      throw FormatError("profile voice diagnostic duration budget");
    ArchiveBgmResources music(usrdir);
    if (!SDL_Init(SDL_INIT_EVENTS)) throw FormatError(SDL_GetError());
    struct Cleanup {
      ~Cleanup() { SDL_Quit(); }
    } cleanup;
    MusicPlayback playback(music, capture);
    if (playback.ProfileVoiceStopped() || playback.ProfileVoicePosition())
      throw FormatError("missing profile voice incorrectly acknowledged");
    bool missingStartRejected = false;
    try {
      playback.StartProfileVoice();
    } catch (const UnsupportedCommand&) {
      missingStartRejected = true;
    }
    if (!missingStartRejected) throw FormatError("unprepared voice started");
    playback.PrepareProfileVoice(assets.Hca, 1.0f);
    if (!playback.ProfileVoiceStopped())
      throw FormatError("prepared voice is not stopped");
    playback.StartProfileVoice();
    if (playback.ProfileVoiceStopped())
      throw FormatError("started voice incorrectly acknowledged as stopped");
    float position = 0;
    int settledStopPeak = 0;
    if (capture) {
      // Exercise actual output and an explicit interruption before the full
      // playback. The second preparation must restart at the beginning.
      std::vector<int16_t> early(4800 * 2);
      playback.Render(early);
      position = playback.ProfileVoicePosition().value_or(0);
      if (!(position > 0) || playback.ProfileVoiceStopped())
        throw FormatError("profile voice output/position did not advance");
      bool badPrepareRejected = false;
      try {
        playback.PrepareProfileVoice(Bytes{0, 1}, 1.0f);
      } catch (const FormatError&) {
        badPrepareRejected = true;
      }
      if (!badPrepareRejected || playback.ProfileVoiceStopped())
        throw FormatError(
            "invalid voice preparation interrupted current output");
      playback.StopProfileVoice();
      if (!playback.ProfileVoiceStopped())
        throw FormatError("explicit profile voice stop not acknowledged");
      std::vector<int16_t> silence(4800 * 2);
      playback.Render(silence);
      // Existing OpenAL output includes a short zero-time stop smoothing tail
      // and signed16 dither. Use the same settled-tail bound as the BGM probe.
      for (auto sample : std::span(silence).last(512))
        settledStopPeak = std::max(settledStopPeak, std::abs(int(sample)));
      if (settledStopPeak > 1)
        throw FormatError("profile voice output did not settle after stop");
      SaveWav(captureDirectory / "early-stop.wav", early);
      playback.PrepareProfileVoice(assets.Hca, 1.0f);
      playback.StartProfileVoice();
    }
    const uint64_t expectedFrames =
        (info.Frames * 48000 + info.SampleRate - 1) / info.SampleRate;
    std::vector<int16_t> output;
    const auto epoch = SDL_GetTicks();
    while (!playback.ProfileVoiceStopped()) {
      if (capture) {
        if (output.size() / 2 > expectedFrames + 48000)
          throw FormatError("profile voice failed to reach natural end");
        const auto offset = output.size();
        output.resize(offset + 1600);
        playback.Render(std::span(output).subspan(offset));
      } else {
        if (SDL_GetTicks() - epoch > expectedFrames / 48 + 2000)
          throw FormatError(
              "profile voice exceeded real-time playback deadline");
        playback.Update(1.0f / 60);
        SDL_Delay(16);
        SDL_Event event;
        while (SDL_PollEvent(&event))
          if (event.type == SDL_EVENT_QUIT) return 0;
      }
    }
    playback.StopProfileVoice();
    if (!playback.ProfileVoiceStopped() || playback.Playing())
      throw FormatError("profile voice did not finish on the shared host");
    if (capture) {
      if (!std::any_of(output.begin(), output.end(),
                       [](int16_t v) { return v != 0; }))
        throw FormatError("profile voice software output was silent");
      SaveWav(captureDirectory / "audio.wav", output);
      std::ofstream report(captureDirectory / "summary.json");
      report << "{\"character\":" << character << ",\"entry\":" << entry
             << ",\"source_frames\":" << info.Frames
             << ",\"source_rate\":" << info.SampleRate
             << ",\"sample_rate\":48000,\"channels\":2,\"rendered_frames\":"
             << output.size() / 2 << ",\"expected_frames\":" << expectedFrames
             << ",\"early_position_seconds\":" << position
             << ",\"mouth_duration\":" << assets.Mouth.Duration
             << ",\"mouth_samples\":" << assets.Mouth.Samples.size()
             << ",\"settled_stop_peak\":" << settledStopPeak
             << ",\"missing_voice_ready\":false,\"missing_start_rejected\":"
                "true,\"prepared_ready\":true"
             << ",\"running_ready\":false,\"invalid_prepare_preserves_"
                "playback\":true,\"explicit_stop_ready\":true"
             << ",\"output_after_stop_silent\":true,\"natural_end_ready\":true,"
                "\"stopped\":true}\n";
      if (!report) throw FormatError("cannot write profile voice report");
    }
    SDL_Log(
        "Saekano profile voice completed through existing HCA/OpenAL services");
    return 0;
  } catch (const std::exception& error) {
    if (captureCreated) {
      std::ofstream report(captureDirectory / "error.txt");
      report << std::string(error.what()).substr(0, 2048) << '\n';
    }
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Saekano profile voice: %s",
                 error.what());
    return 1;
  }
}
int RunProfileServiceSession(const std::filesystem::path& usrdir,
                             uint32_t scene, uint32_t ordinal, uint8_t volume,
                             const std::filesystem::path& captureDirectory) {
  bool captureCreated = false;
  try {
    const bool capture = !captureDirectory.empty();
    if (capture) {
      CreateCaptureDirectory(usrdir, captureDirectory);
      captureCreated = true;
    }
    const auto read = [](const std::filesystem::path& path) {
      std::ifstream file(path, std::ios::binary);
      if (!file) throw FormatError("cannot open profile service data");
      Bytes bytes{std::istreambuf_iterator<char>(file), {}};
      if (file.bad()) throw FormatError("cannot read profile service data");
      return bytes;
    };
    GameResources resources(read(usrdir / "resource.dat"));
    ArchiveBgmResources music(usrdir);
    if (!SDL_Init(SDL_INIT_EVENTS)) throw FormatError(SDL_GetError());
    struct Cleanup {
      ~Cleanup() { SDL_Quit(); }
    } cleanup;
    MusicPlayback playback(music, capture);
    // This diagnostic supplies a known current setting and fresh coefficient;
    // it does not infer a live channel gain or new-game defaults from captures.
    PlaybackProfileVoice voice(playback, volume, 1.0f);
    ArchiveProfileServices services(
        resources,
        ScriptNameIndex::FromFirstArchive(read(usrdir / "first.dat")),
        TextFormatEnvironment::Decode(
            read(usrdir / "env/env_string_format.dat"), true),
        TextTimingEnvironment::Decode(read(usrdir / "env/env_config.dat"),
                                      true),
        &voice);
    services.BeginMenu(int32_t(scene), 2);  // Explicit diagnostic text option.
    if (!services.MenuReady() || !services.MenuTextureCount())
      throw FormatError("profile menu preparation failed");
    ProfileResponseState response;
    response.Ordinal = ordinal;
    response.CharacterId = services.Scene().at(ordinal).Id;
    response.Event = SelectedProfileEvent(services.Scene(), ordinal, 0);
    response.Background = -1;  // Explicit blank diagnostic background.
    const auto assets = resources.LoadProfileVoice(size_t(response.CharacterId),
                                                   size_t(response.Event.Tail));
    HcaWaveReader metadata(assets.Hca);
    const auto info = metadata.Info();
    if (!info.SampleRate || !info.Frames ||
        info.Frames / info.SampleRate > 120 ||
        assets.Mouth.Samples.size() > 1400 || assets.Mouth.Duration > 120000)
      throw FormatError("profile service diagnostic duration budget");
    if (services.ResponseStatus(0).VoiceStopped)
      throw FormatError("missing profile service voice acknowledged");
    services.Act(ProfileResponseAction::BeginResources, response, 0);
    if (!services.ResponseStatus(0).ResourcesReady)
      throw FormatError("profile response resource preparation failed");
    services.Act(ProfileResponseAction::PreparePresentation, response, 0);
    services.Act(ProfileResponseAction::PrepareVoice, response, 0);
    if (!services.ResponseStatus(0).VoiceStopped)
      throw FormatError("prepared PC voice did not acknowledge stopped state");
    services.Act(ProfileResponseAction::BeginTextReveal, response, 0);
    services.Act(ProfileResponseAction::StartVoice, response, 0);
    voice.Update(1000000);  // A clock tick must not fake the device's EOF.
    if (voice.Stopped())
      throw FormatError("PC voice completion follows supplied clock");
    std::ofstream trace;
    if (capture) trace.open(captureDirectory / "mouth.jsonl");
    if (capture && !trace) throw FormatError("cannot create mouth trace");
    std::array<uint64_t, 8> previous{};
    bool first = true;
    size_t transitions = 0;
    std::vector<int16_t> output;
    uint64_t now = 0, eof = UINT64_MAX;
    const auto epoch = SDL_GetTicks();
    while (true) {
      const auto status = services.ResponseStatus(now);
      services.Act(ProfileResponseAction::UpdatePresentation, response, now);
      const auto& mouth = services.Mouth();
      const std::array<uint64_t, 8> snapshot{
          mouth.Active,         mouth.Armed,        mouth.Epoch,
          uint64_t(mouth.From), uint64_t(mouth.To), uint64_t(mouth.Frame),
          mouth.CycleStart,     mouth.CycleEnd};
      if (first || snapshot != previous) {
        if (capture)
          trace << "{\"now\":" << now
                << ",\"active\":" << (mouth.Active ? "true" : "false")
                << ",\"armed\":" << (mouth.Armed ? "true" : "false")
                << ",\"epoch\":" << mouth.Epoch << ",\"tween\":[" << mouth.From
                << ',' << mouth.To << ',' << mouth.Frame << ','
                << mouth.CycleStart << ',' << mouth.CycleEnd << "]}\n";
        previous = snapshot;
        first = false;
        ++transitions;
      }
      if (status.VoiceStopped && eof == UINT64_MAX) eof = now;
      if (status.VoiceStopped && status.TextRevealed && !mouth.Active) break;
      if (now > 121000) throw FormatError("profile service failed to settle");
      if (capture) {
        const auto offset = output.size();
        output.resize(offset + 1600);
        playback.Render(std::span(output).subspan(offset));
        now = (output.size() / 2) / 48;
      } else {
        playback.Update(1.0f / 60);
        SDL_Delay(16);
        now = SDL_GetTicks() - epoch;
        SDL_Event event;
        while (SDL_PollEvent(&event))
          if (event.type == SDL_EVENT_QUIT) return 0;
      }
    }
    services.Act(ProfileResponseAction::StopVoice, response, now);
    if (!services.ResponseStatus(now).VoiceStopped || services.VoiceFrames())
      throw FormatError(
          "PC service lost actual stop acknowledgement or fabricated frame "
          "count");
    voice.Reset();
    if (services.ResponseStatus(now).VoiceStopped ||
        playback.ProfileVoiceStopped())
      throw FormatError("PC voice reset retained stale stop acknowledgement");
    if (capture) {
      trace.close();
      if (!trace) throw FormatError("cannot save mouth trace");
      SaveWav(captureDirectory / "audio.wav", output);
      std::ofstream report(captureDirectory / "summary.json");
      report << "{\"scene\":" << scene << ",\"ordinal\":" << ordinal
             << ",\"character\":" << response.CharacterId
             << ",\"entry\":" << response.Event.Tail
             << ",\"volume_option\":" << unsigned(volume)
             << ",\"gain_bits\":" << std::bit_cast<uint32_t>(voice.Gain())
             << ",\"text_option\":2,\"background\":-1,\"source_frames\":"
             << info.Frames << ",\"source_rate\":" << info.SampleRate
             << ",\"menu_textures\":" << services.MenuTextureCount()
             << ",\"rendered_frames\":" << output.size() / 2
             << ",\"finished_ms\":" << now << ",\"voice_eof_ms\":" << eof
             << ",\"mouth_transitions\":" << transitions
             << ",\"text_revealed\":true,\"mouth_finished\":true,\"stop_"
                "acknowledged\":true"
             << ",\"reset_clears_acknowledgement\":true,\"clock_only_update_"
                "keeps_playing\":true}\n";
      if (!report) throw FormatError("cannot save profile service report");
    }
    return 0;
  } catch (const std::exception& error) {
    if (captureCreated) {
      std::ofstream report(captureDirectory / "error.txt");
      report << std::string(error.what()).substr(0, 2048) << '\n';
    }
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Saekano profile service: %s",
                 error.what());
    return 1;
  }
}
}  // namespace Impacto::Saekano
