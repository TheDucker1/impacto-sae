#include "musicplayback.h"

#include "../../profile/games/saekano/game.h"

#include "headlessbackend.h"
#include "resources.h"
#include "profilearchive.h"
#include "optionspreview.h"
#include "ordinaryvoiceoutput.h"
#include "scenemessage.h"
#include "../../audio/audiosystem.h"
#include "../../audio/openal/audiobackend.h"
#include "../../audio/openal/openalaudiochannel.h"
#include "../../io/dummystream.h"

#include <SDL3/SDL.h>
#include <chrono>

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
      auto stop = [&](uint32_t channel,uint32_t fade) {
        Bytes payload; Word(payload, channel); Word(payload, fade);
        record(0x25b, payload);
      };
      wait();
      play.clear();
      const uint32_t other = (*soundChannel + 1) % 4;
      for (auto value : {0u, track, other, 50u, 0u, 0u}) Word(play, value);
      record(0x25a, play);
      wait(); stop(*soundChannel,500); wait(); stop(other,0);
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
  PlaybackVoiceAttenuation* Attenuation = nullptr;
  uint64_t RenderedFrames = 0;
  std::chrono::steady_clock::time_point AudioEpoch = std::chrono::steady_clock::now();
  std::unique_ptr<Audio::OpenAL::OpenALAudioChannel> Channel;
  std::unique_ptr<Audio::OpenAL::OpenALAudioChannel> OutgoingBgm;
  uint64_t BgmSerial=0,CurrentBgmSerial=0,OutgoingBgmSerial=0;
  uint32_t BgmFadeOutDuration=500;
  std::vector<uint64_t> RetiredBgm;
  std::array<std::unique_ptr<Audio::OpenAL::OpenALAudioChannel>,4> SeChannels;
  std::array<BgmStopFade,4> SeFades;
  std::array<float,4> SeGains{1,1,1,1};
  void SetSeGain(size_t slot,float gain) {
    SeGains[slot]=gain;
    SeChannels[slot]->SetVolume(gain*(SeFades[slot].Active()?SeFades[slot].Weight:1.0f));
  }
  void StopSe(size_t slot,int32_t duration) {
    if(duration<0)throw UnsupportedCommand("negative SE fade duration is not bound");
    auto& fade=SeFades[slot];
    auto& output=SeChannels[slot];
    if(output->GetState()==Audio::ACS_Stopped || fade.Request(duration)) {
      output->Stop(0);fade={};return;
    }
    fade.Tick(0);fade.Tick(0); // Existing source is ready; arm at service time0.
    output->SetVolume(SeGains[slot]*fade.Weight);
  }
  std::unique_ptr<Audio::OpenAL::OpenALAudioChannel> ProfileVoice;
  std::unique_ptr<Audio::OpenAL::OpenALAudioChannel> SecondaryVoice;
  std::array<std::unique_ptr<Audio::OpenAL::OpenALAudioChannel>,2> AlternateVoices;
  auto& OrdinaryOutput(size_t slot) {
    if(slot==4)return MissionVoice;
    if(slot==5)return PreviewVoice;
    return slot==0?ProfileVoice:slot==1?SecondaryVoice:AlternateVoices.at(slot-2);
  }
  std::unique_ptr<Audio::OpenAL::OpenALAudioChannel> MissionVoice;
  std::unique_ptr<Audio::OpenAL::OpenALAudioChannel> PreviewVoice;
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
  std::array<std::unique_ptr<HcaPlaybackStream>, 6> PreparedOrdinaryVoices;
  std::array<bool, 6> HasOrdinaryVoice{};
  bool HasProfileVoice = false;
  // Native b6c38 attaches the same linear fader to voices7..12. Prepare
  // stops the old token; Start retires that tail before playing the new token.
  std::array<BgmStopFade, 6> VoiceFades;
  std::array<float, 6> VoiceGains{1, 1, 1, 1, 1, 1};
  double ChannelFadeFraction = 0;
  uint32_t ChannelFadeFrameRemainder = 0;
  void StopVoice(size_t slot, bool prepared) {
    auto& output = OrdinaryOutput(slot);
    auto& fade = VoiceFades[slot];
    if (prepared || fade.Active() || output->GetState() == Audio::ACS_Stopped) {
      output->Stop(0);
      fade = {};
      output->SetVolume(VoiceGains[slot]);
      return;
    }
    fade = {};
    fade.Request(500);
    fade.Tick(0); fade.Tick(0); // The existing output is already ready.
  }
  void SetVoiceGain(size_t slot, float gain) {
    VoiceGains[slot] = gain;
    OrdinaryOutput(slot)->SetVolume(gain *
        (VoiceFades[slot].Active() ? VoiceFades[slot].Weight : 1.0f));
  }
  void StartVoice(size_t slot, std::unique_ptr<HcaPlaybackStream> stream) {
    VoiceFades[slot] = {};
    auto& output = OrdinaryOutput(slot);
    output->SetVolume(VoiceGains[slot]);
    output->Play(std::move(stream), false, 0);
  }
  void AdvanceChannelFades(uint32_t milliseconds) {
    for(size_t slot=0;slot<SeFades.size();++slot) {
      auto& fade=SeFades[slot];
      if(!fade.Active())continue;
      fade.Tick(milliseconds);
      SeChannels[slot]->SetVolume(SeGains[slot]*fade.Weight);
      if(!fade.Active())SeChannels[slot]->Stop(0);
    }
    for (size_t slot = 0; slot < VoiceFades.size(); ++slot) {
      auto& fade = VoiceFades[slot];
      if (!fade.Active()) continue;
      fade.Tick(milliseconds);
      OrdinaryOutput(slot)->SetVolume(VoiceGains[slot] * fade.Weight);
      if (!fade.Active()) OrdinaryOutput(slot)->Stop(0);
    }
  }
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
      for(size_t i=0;i<2;++i)AlternateVoices[i]=std::make_unique<Audio::OpenAL::OpenALAudioChannel>(
          i?Audio::AC_VOICE1:Audio::AC_VOICE0,Audio::ACG_Voice); // Independent owned AL sources for native9/10.
      MissionVoice = std::make_unique<Audio::OpenAL::OpenALAudioChannel>(
          Audio::AC_VOICE2, Audio::ACG_Voice);
      PreviewVoice = std::make_unique<Audio::OpenAL::OpenALAudioChannel>(
          Audio::AC_VOICE2, Audio::ACG_Voice); // Separate owned native12 source.
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
    for(auto& channel:AlternateVoices)channel.reset();
    MissionVoice.reset();
    PreviewVoice.reset();
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
  if(event.Kind==GameEventKind::StopBgmWithCurrentFade) {
    if(event.Id || event.Parameter || event.Bgm || event.Se)throw FormatError("invalid direct BGM stop request");
    Submit({GameEventKind::StopBgm,0,{},int32_t(impl_->BgmFadeOutDuration)});return;
  }
  if (event.Kind == GameEventKind::PlayBgm || event.Kind == GameEventKind::PlayBgmRetainingGain) {
    const bool retaining = event.Kind == GameEventKind::PlayBgmRetainingGain;
    if (retaining && (event.Parameter || event.Bgm || event.Se))
      throw FormatError("invalid direct BGM play request");
    if (!retaining && (!event.Bgm || event.Bgm->Track != event.Id))
      throw FormatError("invalid typed BGM request");
    const auto request = retaining
        ? BgmPlayRequest{event.Id,0,BgmVolume{0,0,impl_->BgmGain}}
        : *event.Bgm;
    if (!request.Volume)
      throw UnsupportedCommand("native BGM volume state is unknown");
    if (!std::isfinite(request.Volume->Gain) || request.Volume->Gain < 0 ||
        request.Volume->Gain > 1)
      throw UnsupportedCommand(
          "BGM output gain outside authored settings range");
    auto stream =
        std::make_unique<HcaPlaybackStream>(impl_->Resources, request.Track);
    const bool loop = impl_->Resources.Info(request.Track).Loop;
    // A script/slot volume publication changes the native service's saved
    // target. Direct gallery/title replay without a volume write retains it.
    if (impl_->Attenuation && !retaining)
      impl_->Attenuation->state_.SetGain(int32_t(request.Volume->ConvertedVolume));
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
  } else if(event.Kind==GameEventKind::SetBgmFadeDuration) {
    if(event.Id!=0 || event.Parameter<0)throw UnsupportedCommand("invalid BGM attached fade duration request");
    impl_->BgmFadeOutDuration=uint32_t(event.Parameter);
  } else if(event.Kind==GameEventKind::PumpAudio) {
    if(event.Id!=0 || event.Parameter)throw UnsupportedCommand("invalid audio service pump request");
    Update(0); // Service work at the already sampled scene clock; no fabricated elapsed time.
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
    // Normal ScriptPlaySe's final service refresh writes gain to backend2..5.
    // BGM is recomputed from unchanged service percentages, preserving its
    // applied gain here. The voice consumer remains separate. Remember is
    // never a looping flag. Saved replay omits that refresh and retains716's
    // shifted four-channel gain writes instead.
    if (request.RefreshServiceVolume) {
      for (size_t slot=0;slot<impl_->SeChannels.size();++slot) impl_->SetSeGain(slot,gain);
    } else {
      for (uint32_t channel = uint32_t(request.Channel);
           channel < uint32_t(request.Channel) + 4; ++channel)
        SetChannelOutputGain(channel, gain);
    }
    const auto slot=size_t(request.Channel-2);
    // Native Prepare stops the previous token; Start retires its outgoing
    // tail before the new playback. No second source is needed for SE.
    impl_->SeFades[slot]={};
    impl_->SeChannels[slot]->SetVolume(impl_->SeGains[slot]);
    impl_->SeChannels[slot]->Play(std::move(stream),
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
    impl_->StopSe(size_t(event.Id-2),event.Parameter);
  } else if (event.Kind == GameEventKind::StopChannel) {
    if (event.Parameter || (event.Id < 7 || event.Id > 10))
      throw UnsupportedCommand(
          "native channel stop outside bound voice7..10 or nonzero fade");
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
  if (!impl_->RenderSamples) {
    impl_->ChannelFadeFraction += double(seconds) * 1000.0;
    const double whole = std::floor(impl_->ChannelFadeFraction);
    impl_->ChannelFadeFraction -= whole;
    impl_->AdvanceChannelFades(uint32_t(std::min(whole, 67.0)));
  }
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
  for(auto& channel:impl_->AlternateVoices)channel->Update(seconds);
  impl_->MissionVoice->Update(seconds);
  impl_->PreviewVoice->Update(seconds);
  impl_->UiSound->Update(seconds);
  if (impl_->Attenuation) {
    if (const auto gain = impl_->Attenuation->state_.Tick(AudioServiceTime(), VoiceAttenuationInputs()))
      SetBgmOutputGain(float(*gain) / 100);
  }
  AlCheck("BGM frame");
}
void MusicPlayback::StopBgmImmediately(){
  impl_->RetiredBgm.clear();
  if(impl_->CurrentBgmSerial)impl_->RetiredBgm.push_back(impl_->CurrentBgmSerial);
  impl_->CurrentBgmSerial=0;impl_->Channel->Stop(0);impl_->RetireOutgoingBgm();
  impl_->BgmFade={};impl_->BgmFadeFraction=0;impl_->BgmFadeFrameRemainder=0;
}
void MusicPlayback::PlayGalleryMusic(int32_t wave,std::unique_ptr<HcaWaveReader> reader){
  if(!reader || wave<0)throw FormatError("invalid gallery music source");
  const bool loop=reader->Info().Loop;
  auto stream=std::make_unique<HcaPlaybackStream>(std::move(reader),wave,"music.awb");
  StopBgmImmediately();impl_->Channel->SetVolume(impl_->BgmGain);
  impl_->Channel->Play(std::move(stream),loop,0);impl_->CurrentBgmSerial=++impl_->BgmSerial;
}
void MusicPlayback::SetBgmPaused(bool paused){if(paused)impl_->Channel->Pause();else impl_->Channel->Resume();}
bool MusicPlayback::BgmPaused()const{return impl_->Channel->GetState()==Audio::ACS_Paused;}
bool MusicPlayback::BgmBusy()const{return impl_->Channel->GetState()!=Audio::ACS_Stopped;}
uint64_t MusicPlayback::BgmPositionMs()const{return uint64_t(std::max(0.0,double(impl_->Channel->PositionInSeconds())*1000.0));}
void MusicPlayback::StopAllChannels() {
  impl_->Channel->Stop(0);
  if (impl_->CurrentBgmSerial)
    impl_->RetiredBgm.push_back(impl_->CurrentBgmSerial);
  impl_->CurrentBgmSerial = 0;
  impl_->RetireOutgoingBgm();
  impl_->BgmFade = {};
  impl_->BgmFadeFraction = 0;
  impl_->BgmFadeFrameRemainder = 0;
  for (auto& channel : impl_->SeChannels) if (channel) channel->Stop(0);
  impl_->SeFades={};
  impl_->UiSound->Stop(0);
  for (uint32_t channel = 7; channel <= 12; ++channel)
    StopOrdinaryVoice(channel);
  // Native immediate-stop teardown bypasses attached fades, including tails
  // just created by stopping live channels above.
  for (size_t slot = 0; slot < impl_->VoiceFades.size(); ++slot) {
    impl_->OrdinaryOutput(slot)->Stop(0);
    impl_->VoiceFades[slot] = {};
  }
  impl_->ChannelFadeFraction = 0;
  impl_->ChannelFadeFrameRemainder = 0;
  impl_->HasOrdinaryVoice.fill(false);
  impl_->HasProfileVoice = impl_->HasMissionVoice = false;
  AlCheck("shared audio quiescence");
}
namespace {
void ValidateLiveGain(float gain){
  if(!std::isfinite(gain) || gain<0 || gain>1)throw FormatError("live output gain outside supported range");
}
}
void MusicPlayback::SetBgmOutputGain(float gain){
  ValidateLiveGain(gain);impl_->BgmGain=gain;
  impl_->Channel->SetVolume(gain);
  impl_->OutgoingBgm->SetVolume(gain*impl_->BgmFade.Weight);
  AlCheck("live BGM gain");
}
void MusicPlayback::SetChannelOutputGain(uint32_t channel,float gain){
  ValidateLiveGain(gain);
  if(channel<2 || channel>12)throw FormatError("live channel gain outside native2..12");
  if(channel<6){
    if(!impl_->SeChannels.at(channel-2))throw UnsupportedCommand("live SE gain has no bound channel");
    impl_->SetSeGain(channel-2,gain);
  }
  else if(channel==6){impl_->UiSoundGain=gain;impl_->UiSound->SetVolume(gain);}
  else impl_->SetVoiceGain(channel-7, gain);
  AlCheck("live channel gain");
}
float MusicPlayback::ChannelOutputGain(uint32_t channel)const{
  if(channel<2 || channel>12)throw FormatError("live channel gain query outside native2..12");
  if(channel<6){
    if(!impl_->SeChannels.at(channel-2))throw UnsupportedCommand("live SE gain query has no bound channel");
    return impl_->SeGains.at(channel-2);
  }
  if(channel==6)return impl_->UiSound->GetVolume();
  return impl_->VoiceGains[channel-7];
}
AudioDuckingInputs MusicPlayback::VoiceAttenuationInputs()const{
  AudioDuckingInputs in;in.Available=0;in.TargetAvailable=bool(impl_->Channel);
  for(uint32_t slot=0;slot<6;++slot){
    const auto& channel=impl_->OrdinaryOutput(slot);const auto bit=1u<<(slot+7);
    if(channel){in.Available|=bit;const auto s=channel->GetState();if(s==Audio::ACS_Playing || s==Audio::ACS_Paused)in.Busy|=bit;}
    const bool prepared=slot==0?bool(impl_->PreparedProfileVoice || impl_->PreparedOrdinaryVoices[0]):
      slot==4?bool(impl_->PreparedMissionVoice):bool(impl_->PreparedOrdinaryVoices[slot]);
    if(prepared)in.Prepared|=bit;
  }
  return in;
}
PlaybackVoiceAttenuation::PlaybackVoiceAttenuation(MusicPlayback& output,int32_t gain,int32_t percent,bool enabled):output_(output){
  state_.SetGain(gain);state_.SetPercent(percent);state_.SetEnabled(enabled);output_.SetBgmOutputGain(float(gain)/100);
}
PlaybackVoiceAttenuation::~PlaybackVoiceAttenuation(){
  if(output_.impl_->Attenuation==this)output_.impl_->Attenuation=nullptr;
}
void PlaybackVoiceAttenuation::Bind(){
  if(output_.impl_->Attenuation && output_.impl_->Attenuation!=this)
    throw FormatError("playback already has an attenuation owner");
  output_.impl_->Attenuation=this;
}
PlaybackVoiceAttenuation* MusicPlayback::VoiceAttenuation()const{return impl_->Attenuation;}
uint64_t MusicPlayback::AudioServiceTime()const{
  if(impl_->RenderSamples)return impl_->RenderedFrames/48;
  return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now()-impl_->AudioEpoch).count());
}
void PlaybackVoiceAttenuation::SetGain(int32_t percent){state_.SetGain(percent);output_.SetBgmOutputGain(float(percent)/100);}
void PlaybackVoiceAttenuation::StartProfileVoice(uint64_t now){
  output_.StartProfileVoice();if(output_.VoiceAttenuation()!=this)state_.StartVoice(7,true,now);
}
void PlaybackVoiceAttenuation::StartOrdinaryVoice(uint32_t channel,uint64_t now){
  const bool ready=output_.OrdinaryVoiceReady(channel);output_.StartOrdinaryVoice(channel);
  if(output_.VoiceAttenuation()!=this)state_.StartVoice(channel,ready,now);
}
void PlaybackVoiceAttenuation::Update(uint64_t now){
  if(output_.VoiceAttenuation()==this)return; // The physical host pumps it once.
  if(const auto gain=state_.Tick(now,output_.VoiceAttenuationInputs()))output_.SetBgmOutputGain(float(*gain)/100);
}
bool MusicPlayback::Playing() const {
  if (impl_->Channel->GetState() != Audio::ACS_Stopped) return true;
  if (impl_->OutgoingBgm->GetState() != Audio::ACS_Stopped) return true;
  if (impl_->ProfileVoice->GetState() != Audio::ACS_Stopped) return true;
  if (impl_->SecondaryVoice->GetState() != Audio::ACS_Stopped) return true;
  for(const auto& channel:impl_->AlternateVoices)if(channel->GetState()!=Audio::ACS_Stopped)return true;
  if (MissionVoicePlaying()) return true;
  if(impl_->PreviewVoice->GetState()!=Audio::ACS_Stopped)return true;
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
void MusicPlayback::StopUiSound(){impl_->UiSound->Stop(0);AlCheck("UI sound stop");}
PlaybackTitleAudio::PlaybackTitleAudio(MusicPlayback& playback,SystemSoundAssets& ui,float voiceGain,float uiGain)
    :PlaybackTitleAudio(playback,voiceGain){
  playback_.BindUiSounds(ui,uiGain);
}
PlaybackTitleAudio::PlaybackTitleAudio(MusicPlayback& playback,float voiceGain)
    :playback_(playback),gain_(voiceGain){
  if(!std::isfinite(gain_) || gain_<0 || gain_>1)throw FormatError("title voice gain outside supported range");
}
void PlaybackTitleAudio::SetGains(float voiceGain,float uiGain){
  ValidateLiveGain(voiceGain);ValidateLiveGain(uiGain);
  playback_.SetChannelOutputGain(7,voiceGain);playback_.SetChannelOutputGain(6,uiGain);gain_=voiceGain;
}
void PlaybackTitleAudio::BindVoiceAttenuation(PlaybackVoiceAttenuation& owner,std::function<uint64_t()> clock){
  if(!clock)throw FormatError("missing title audio-service clock");attenuation_=&owner;audioClock_=std::move(clock);
}
void PlaybackTitleAudio::PlayVoice(Bytes encoded){
  playback_.PrepareMemoryVoice(7,std::move(encoded),gain_);
  if(attenuation_)attenuation_->StartOrdinaryVoice(7,audioClock_());else playback_.StartOrdinaryVoice(7);
}
void PlaybackTitleAudio::PlayUi(int32_t id){playback_.Submit({GameEventKind::Sound,id});}
void PlaybackTitleAudio::Stop(uint32_t channel){if(channel==6)playback_.StopUiSound();else if(channel==7)playback_.StopOrdinaryVoice(7);else throw FormatError("title audio channel outside6/7");}
void PlaybackTitleAudio::Retire(){playback_.StopOrdinaryVoice(7,true);playback_.StopUiSound();}
bool PlaybackTitleAudio::Playing(uint32_t channel)const{if(channel==6)return playback_.UiSoundPlaying();if(channel==7)return playback_.OrdinaryVoicePlaying(7);throw FormatError("title audio channel outside6/7");}
void MusicPlayback::PrepareProfileVoice(Bytes encoded, float gain) {
  if (!std::isfinite(gain) || gain < 0 || gain > 1)
    throw FormatError("profile voice gain outside supported range");
  auto stream = std::make_unique<HcaPlaybackStream>(std::move(encoded));
  impl_->StopVoice(0, bool(impl_->PreparedProfileVoice || impl_->PreparedOrdinaryVoices[0]));
  impl_->PreparedOrdinaryVoices[0].reset();
  impl_->HasOrdinaryVoice[0] = false;
  impl_->SetVoiceGain(0, gain);
  impl_->PreparedProfileVoice = std::move(stream);
  impl_->HasProfileVoice = true;
  AlCheck("profile voice preparation");
}
void MusicPlayback::StartProfileVoice() {
  if (!impl_->PreparedProfileVoice)
    throw UnsupportedCommand(
        "profile voice must be prepared before each start");
  impl_->StartVoice(0, std::move(impl_->PreparedProfileVoice));
  AlCheck("profile voice start");
  if(impl_->Attenuation)impl_->Attenuation->state_.StartVoice(7,true,AudioServiceTime());
}
void MusicPlayback::StopProfileVoice() {
  impl_->StopVoice(0, bool(impl_->PreparedProfileVoice || impl_->PreparedOrdinaryVoices[0]));
  impl_->PreparedProfileVoice.reset();
  impl_->PreparedOrdinaryVoices[0].reset();
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
  if (channel<7 || channel>12)
    throw UnsupportedCommand("ordinary voice channel outside7..12");
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
  if(channel==11){PrepareMissionVoice(voiceId,std::move(reader),gain);return;}
  auto stream = std::make_unique<HcaPlaybackStream>(std::move(reader), int32_t(voiceId), "voice.awb");
  impl_->StopVoice(slot, bool(impl_->PreparedOrdinaryVoices[slot] ||
      (!slot && impl_->PreparedProfileVoice)));
  impl_->SetVoiceGain(slot, gain);
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
  if(channel==11){StartMissionVoice();return;}
  const bool ready = bool(impl_->PreparedOrdinaryVoices[slot]);
  auto& output = impl_->OrdinaryOutput(slot);
  if (impl_->PreparedOrdinaryVoices[slot])
    impl_->StartVoice(slot, std::move(impl_->PreparedOrdinaryVoices[slot]));
  else if (impl_->HasOrdinaryVoice[slot]) output->Resume();
  AlCheck("ordinary voice start");
  if(impl_->Attenuation)impl_->Attenuation->state_.StartVoice(channel,ready,AudioServiceTime());
}
void MusicPlayback::PrepareMemoryVoice(uint32_t channel, Bytes encoded, float gain) {
  const auto slot = OrdinaryVoiceSlot(channel);
  if (channel > 10) throw UnsupportedCommand("memory voice channel outside dialogue7..10");
  if (!std::isfinite(gain) || gain < 0 || gain > 1)
    throw FormatError("memory voice gain outside supported range");
  auto stream = std::make_unique<HcaPlaybackStream>(
      std::make_unique<HcaWaveReader>(std::move(encoded)), 0, "memory.hca");
  impl_->StopVoice(slot, bool(impl_->PreparedOrdinaryVoices[slot] ||
      (!slot && impl_->PreparedProfileVoice)));
  impl_->SetVoiceGain(slot, gain);
  if (!slot) {
    impl_->PreparedProfileVoice.reset();
    impl_->HasProfileVoice = false;
  }
  impl_->PreparedOrdinaryVoices[slot] = std::move(stream);
  impl_->HasOrdinaryVoice[slot] = true;
  AlCheck("memory voice preparation");
}
void MusicPlayback::StopOrdinaryVoice(uint32_t channel, bool immediate) {
  const auto slot = OrdinaryVoiceSlot(channel);
  impl_->StopVoice(slot, channel==11 ? bool(impl_->PreparedMissionVoice) :
      bool(impl_->PreparedOrdinaryVoices[slot] || (!slot && impl_->PreparedProfileVoice)));
  if(channel==11)impl_->PreparedMissionVoice.reset();
  impl_->PreparedOrdinaryVoices[slot].reset();
  if (!slot) {
    impl_->PreparedProfileVoice.reset();
    impl_->HasProfileVoice = false;
  }
  if(immediate) {
    impl_->OrdinaryOutput(slot)->Stop(0);
    impl_->VoiceFades[slot]={};
    impl_->HasOrdinaryVoice[slot]=false;
  }
  AlCheck("ordinary voice stop");
}
bool MusicPlayback::OrdinaryVoiceReady(uint32_t channel) const {
  if(channel==11)return MissionVoiceReady();
  return bool(impl_->PreparedOrdinaryVoices[OrdinaryVoiceSlot(channel)]);
}
bool MusicPlayback::OrdinaryVoicePlaying(uint32_t channel) const {
  const auto slot = OrdinaryVoiceSlot(channel);
  if(channel==11)return MissionVoicePlaying();
  return impl_->HasOrdinaryVoice[slot] &&
         !impl_->PreparedOrdinaryVoices[slot] &&
         (impl_->OrdinaryOutput(slot))->GetState() == Audio::ACS_Playing;
}
int32_t MusicPlayback::OrdinaryVoicePosition(uint32_t channel) const {
  const auto slot = OrdinaryVoiceSlot(channel);
  if(channel==11)return MissionVoicePosition();
  if (!OrdinaryVoicePlaying(channel)) return -1;
  const auto seconds =
      (impl_->OrdinaryOutput(slot))->PositionInSeconds();
  if (!std::isfinite(seconds) || seconds < 0 || seconds >= 2147483.0f)
    throw FormatError("ordinary voice position outside supported range");
  return int32_t(seconds * 1000.0f);
}
PlaybackOrdinaryVoice::PlaybackOrdinaryVoice(MusicPlayback& playback,
    const std::filesystem::path& usrdir, uint8_t volume, float coefficient)
    : playback_(playback), bank_(std::make_shared<AwbBank>(Profile::Saekano::AssetPath(usrdir, Profile::Saekano::GameAsset::VoiceBank))),
      gain_(ProfileVoiceGain(volume, coefficient)) {
  if (!std::isfinite(gain_) || gain_ < 0 || gain_ > 1)
    throw FormatError("ordinary voice gain outside supported range");
}
void PlaybackOrdinaryVoice::Prepare(uint32_t channel, int16_t voiceId) {
  if (voiceId < 0) throw FormatError("negative ordinary voice ID");
  auto reader = std::make_unique<HcaWaveReader>(bank_, uint32_t(voiceId));
  playback_.PrepareOrdinaryVoice(channel, uint32_t(voiceId), std::move(reader), gain_);
}
void PlaybackOrdinaryVoice::SetVolume(uint8_t volume){gain_=ProfileVoiceGain(volume,1.0f);}
void PlaybackOrdinaryVoice::Start(uint32_t channel) {
  playback_.StartOrdinaryVoice(channel);
}
void PlaybackOrdinaryVoice::PrepareMemory(uint32_t channel, Bytes encoded) {
  playback_.PrepareMemoryVoice(channel, std::move(encoded), gain_);
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
  impl_->StopVoice(4, bool(impl_->PreparedMissionVoice));impl_->SetVoiceGain(4, gain);
  impl_->PreparedMissionVoice=std::move(stream);impl_->HasMissionVoice=false;
  AlCheck("mission voice prepare");
}
void MusicPlayback::StartMissionVoice() {
  const bool ready = bool(impl_->PreparedMissionVoice);
  if(impl_->PreparedMissionVoice) {
    impl_->StartVoice(4,std::move(impl_->PreparedMissionVoice));impl_->HasMissionVoice=true;
  } else if(impl_->HasMissionVoice)impl_->MissionVoice->Resume();
  AlCheck("mission voice play");
  if(impl_->Attenuation)impl_->Attenuation->state_.StartVoice(11,ready,AudioServiceTime());
}
bool MusicPlayback::MissionVoiceReady() const { return bool(impl_->PreparedMissionVoice); }
void MusicPlayback::ResetMissionVoice() {
  StopOrdinaryVoice(11);AlCheck("mission voice reset");
}
bool MusicPlayback::MissionVoicePlaying() const {
  return impl_->HasMissionVoice && !impl_->PreparedMissionVoice && impl_->MissionVoice->GetState()!=Audio::ACS_Stopped;
}
int32_t MusicPlayback::MissionVoicePosition() const {
  if(!MissionVoicePlaying())return -1;
  const auto seconds=impl_->MissionVoice->PositionInSeconds();
  return std::isfinite(seconds) && seconds>=0 ? int32_t(seconds*1000) : -1;
}
PlaybackMissionVoice::PlaybackMissionVoice(MusicPlayback& playback,
    const std::filesystem::path& usrdir,uint8_t volume,float coefficient)
    : playback_(playback),bank_(std::make_shared<AwbBank>(Profile::Saekano::AssetPath(usrdir, Profile::Saekano::GameAsset::VoiceBank))),
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
    impl_->ChannelFadeFrameRemainder += uint32_t(count);
    impl_->AdvanceChannelFades(impl_->ChannelFadeFrameRemainder / 48);
    impl_->ChannelFadeFrameRemainder %= 48;
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
    impl_->RenderedFrames += count;
    frame += count;
  }
}

int RunBattleResultMusicSession(const std::filesystem::path& usrdir,const std::filesystem::path& captureDirectory){
  bool created=false;
  try{
    CreateCaptureDirectory(usrdir,captureDirectory);created=true;
    if(!SDL_Init(SDL_INIT_EVENTS))throw FormatError(SDL_GetError());
    struct Cleanup{~Cleanup(){SDL_Quit();}}cleanup;
    ArchiveBgmResources music(usrdir);music.Prepare(0);music.Prepare(7);MusicPlayback playback(music,true);
    constexpr float gain=.37f;constexpr int32_t fade=213;
    GameEvent begin{GameEventKind::PlayBgm,0};begin.Bgm=BgmPlayRequest{0,0,BgmVolume{0,0,gain}};
    playback.Submit(begin);playback.Submit({GameEventKind::SetBgmFadeDuration,0,{},fade});
    std::vector<int16_t> block(9600*2);playback.Render(block);
    playback.Submit({GameEventKind::StopBgmWithCurrentFade,0});
    if(playback.BgmVoices().FadeOutDuration!=fade || playback.BgmFadeState().Duration!=uint32_t(fade))throw FormatError("direct BGM stop changed attached fade");
    playback.Submit({GameEventKind::PlayBgmRetainingGain,7});playback.Render(block);
    int32_t peak=0;for(const auto v:block)peak=std::max(peak,std::abs(int32_t(v)));
    if(!playback.Playing()||playback.BgmOutputGain()!=gain||playback.BgmVoices().CurrentGain!=gain||playback.BgmVoices().FadeOutDuration!=fade||peak<=32)
      throw FormatError("direct result BGM did not retain current service gain/fade or produce audio");
    SaveWav(captureDirectory/"result-music.wav",block);
    playback.Submit({GameEventKind::StopBgmWithCurrentFade,0});
    if(playback.BgmFadeState().Duration!=uint32_t(fade))throw FormatError("result exit changed current BGM fade");
    std::vector<int16_t> tail(48000*2);playback.Render(tail);
    int32_t tailPeak=0;for(const auto v:std::span(tail).last(4800))tailPeak=std::max(tailPeak,std::abs(int32_t(v)));
    if(playback.Playing()||tailPeak>1)throw FormatError("direct result BGM failed to stop");
    std::ofstream out(captureDirectory/"music.json");out << "{\"passed\":true,\"track\":7,\"gain_bits\":" << std::bit_cast<uint32_t>(gain)
      << ",\"retained_fade_ms\":" << fade << ",\"peak\":" << peak << ",\"tail_peak\":" << tailPeak << "}\n";return 0;
  }catch(const std::exception& error){
    if(created){std::ofstream out(captureDirectory/"error.txt");out << std::string(error.what()).substr(0,2048);}
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Saekano result music: %s",error.what());return 1;
  }
}
int RunBattleResultVoiceSession(const std::filesystem::path& usrdir,const std::filesystem::path& captureDirectory){
  bool created=false;
  try{
    CreateCaptureDirectory(usrdir,captureDirectory);created=true;
    std::ifstream file(Profile::Saekano::AssetPath(usrdir, Profile::Saekano::GameAsset::ResourceArchive),std::ios::binary);
    if(!file)throw FormatError("cannot open result voice resources");
    GameResources resources(Bytes{std::istreambuf_iterator<char>(file),{}});
    if(!SDL_Init(SDL_INIT_EVENTS))throw FormatError(SDL_GetError());
    struct Cleanup{~Cleanup(){SDL_Quit();}}cleanup;
    ArchiveBgmResources music(usrdir);MusicPlayback playback(music,true);
    PlaybackOrdinaryVoice voices(playback,usrdir,10,1);
    const auto peak=[](std::span<const int16_t> pcm){int32_t p=0;for(const auto v:pcm)p=std::max(p,std::abs(int32_t(v)));return p;};
    std::vector<int16_t> block(9600*2);
    voices.Prepare(7,7490);voices.Start(7);voices.Prepare(8,7491);voices.Start(8);playback.Render(block);
    voices.PrepareMemory(7,resources.LoadBattleResultVoice(0).Data);
    if(!voices.Ready(7)||voices.Playing(7)||!voices.Playing(8))throw FormatError("memory replacement disturbed physical8");
    voices.Start(7);playback.Render(block);
    const auto position8=voices.Position(8);
    // Reverse replacement uses the existing profile service on the same source.
    playback.PrepareProfileVoice(resources.LoadBattleResultVoice(1).Data,1);
    if(voices.Ready(7)||voices.Playing(7))throw FormatError("profile failed to replace memory7");
    playback.StartProfileVoice();playback.Render(block);
    voices.PrepareMemory(7,resources.LoadBattleResultVoice(0).Data);
    if(playback.ProfileVoicePosition() || !voices.Ready(7) || !voices.Playing(8) || voices.Position(8)<=position8)
      throw FormatError("memory failed to retire profile7 or interrupted8");
    bool rejected=false;try{voices.PrepareMemory(7,Bytes{});}catch(const FormatError&){rejected=true;}
    if(!rejected || !voices.Ready(7))throw FormatError("invalid source retired prepared memory7");
    voices.Start(7);playback.Render(block);const auto position7=voices.Position(7);
    rejected=false;try{voices.PrepareMemory(7,Bytes{0,1,2});}catch(const FormatError&){rejected=true;}
    if(!rejected || !voices.Playing(7) || voices.Position(7)!=position7)throw FormatError("invalid memory changed playing source");
    voices.Stop(7);voices.Start(7);playback.Render(block);
    if(voices.Ready(7)||voices.Playing(7)||playback.ProfileVoicePosition()||!voices.Playing(8))throw FormatError("retired memory restarted or stopped8");
    voices.Stop(8);playback.Render(block);
    if(peak(std::span(block).last(9600))>1)throw FormatError("memory stop retained audible source");
    std::ofstream out(captureDirectory/"result.json");
    out << "{\"passed\":true,\"shared_profile_ordinary7\":true,\"independent8\":true,\"invalid_preserves_ready_and_playing\":true,\"retired_start_noop\":true,\"clips\":[";
    for(size_t i=0;i<8;++i){
      const auto asset=resources.LoadBattleResultVoice(i);const auto info=HcaWaveReader(asset.Data).Info();
      voices.PrepareMemory(7,asset.Data);voices.Start(7);playback.Render(block);
      const auto position=voices.Position(7);
      if(!voices.Playing(7)||position<190||position>230||peak(block)<=32)throw FormatError("memory source did not produce positioned audio");
      std::vector<int16_t> samples;
      if(i==0)samples=block;
      voices.Start(7);playback.Render(block);
      if(i==0)samples.insert(samples.end(),block.begin(),block.end());
      if(voices.Position(7)<=position)throw FormatError("repeated memory Start reset playback");
      const auto frames=(info.Frames*48000+info.SampleRate-1)/info.SampleRate;
      std::vector<int16_t> tail(size_t(frames+4800-19200)*2);playback.Render(tail);
      if(voices.Playing(7)||voices.Ready(7)||voices.Position(7)!=-1||peak(std::span(tail).last(4800))>1)
        throw FormatError("memory source EOF/settled tail mismatch");
      if(i)out << ',';
      out << "{\"entry\":" << i << ",\"bytes\":" << asset.Data.size() << ",\"rate\":" << info.SampleRate << ",\"frames\":" << info.Frames
          << ",\"position_ms\":" << position << ",\"peak\":" << peak(block) << ",\"settled_tail_peak\":" << peak(std::span(tail).last(4800)) << '}';
      if(i==0){samples.insert(samples.end(),tail.begin(),tail.end());SaveWav(captureDirectory/"result-voice.wav",samples);}
      voices.Stop(7);
    }
    out << "]}\n";if(!out)throw FormatError("cannot write result voice report");return 0;
  }catch(const std::exception& error){
    if(created){std::ofstream out(captureDirectory/"error.txt");out << std::string(error.what()).substr(0,2048);}
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Saekano battle result voice: %s",error.what());return 1;
  }
}
int RunBattlePreviewVoiceSession(const std::filesystem::path& usrdir,const std::filesystem::path& captureDirectory){
  bool created=false;
  try{
    CreateCaptureDirectory(usrdir,captureDirectory);created=true;ArchiveBgmResources resources(usrdir);
    if(!SDL_Init(SDL_INIT_EVENTS))throw FormatError(SDL_GetError());
    struct Cleanup{~Cleanup(){SDL_Quit();}}cleanup;
    MusicPlayback playback(resources,true);PlaybackOrdinaryVoice preview(playback,usrdir,10,1);
    PlaybackMissionVoice mission(playback,usrdir,10,1);const std::array<int32_t,1> ids{10579};mission.Register(ids);
    preview.Start(11);preview.Start(12);
    if(preview.Ready(11)||preview.Playing(11)||preview.Playing(12))throw FormatError("empty preview start created playback");
    mission.Prepare(10579);preview.Prepare(11,12150);
    if(!playback.MissionVoiceReady()||!preview.Ready(11)||preview.Playing(11))throw FormatError("preview11 did not replace prepared mission source");
    mission.Start(0); // Either owner starts the same current physical descriptor.
    std::vector<int16_t> first(19200*2);playback.Render(first);const auto firstPosition=preview.Position(11);
    if(!preview.Playing(11)||!playback.MissionVoicePlaying()||firstPosition<390)throw FormatError("shared preview11 failed to play");
    preview.Prepare(12,12149);preview.Start(12);std::vector<int16_t> overlap(9600*2);playback.Render(overlap);
    const auto position11=preview.Position(11),position12=preview.Position(12);
    if(position11<590||position12<190||position11-position12<390)throw FormatError("preview12 did not advance independently");
    mission.Prepare(10579);bool rejected=false;
    try{preview.Prepare(12,INT16_MAX);}catch(const FormatError&){rejected=true;}
    if(!rejected||!preview.Ready(11)||preview.Playing(11)||!preview.Playing(12))throw FormatError("mission replacement/invalid prepare disturbed preview12");
    preview.Start(11);std::vector<int16_t> replacement(9600*2);playback.Render(replacement);
    if(preview.Position(11)<190||preview.Position(11)>230||preview.Position(12)<=position12)
      throw FormatError("mission replacement did not reset only shared11");
    preview.Stop(11);mission.Start(0);preview.Start(11);const auto before12=preview.Position(12);preview.Start(12);
    std::vector<int16_t> independent(9600*2);playback.Render(independent);const auto after12=preview.Position(12);
    if(preview.Ready(11)||preview.Playing(11)||playback.MissionVoicePlaying()||preview.Position(11)!=-1||after12<=before12)
      throw FormatError("retired11 restarted or stopped independent12");
    preview.Stop(12);std::vector<int16_t> tail(9600*2);playback.Render(tail);
    const auto peak=[](std::span<const int16_t> pcm){int32_t p=0;for(const auto v:pcm)p=std::max(p,std::abs(int32_t(v)));return p;};
    if(peak(first)<=32||peak(overlap)<=32||peak(std::span(tail).last(9600))>1)throw FormatError("preview audio energy/stop-tail mismatch");
    std::vector<int16_t> samples;for(const auto* part:{&first,&overlap,&replacement,&independent,&tail})samples.insert(samples.end(),part->begin(),part->end());
    SaveWav(captureDirectory/"preview-alias.wav",samples);std::ofstream out(captureDirectory/"preview.json");
    out << "{\"passed\":true,\"shared11\":true,\"independent12\":true,\"retired_start_noop\":true,\"invalid_preserves_ready\":true,"
      << "\"preview_ids\":[12150,12149],\"mission_id\":10579,\"first_position_ms\":" << firstPosition
      << ",\"overlap_positions_ms\":[" << position11 << ',' << position12 << "],\"independent12_positions_ms\":[" << before12 << ',' << after12
      << "],\"first_peak\":" << peak(first) << ",\"overlap_peak\":" << peak(overlap) << ",\"settled_tail_peak\":" << peak(std::span(tail).last(9600))
      << ",\"output_frames\":" << samples.size()/2 << "}\n";return 0;
  }catch(const std::exception& error){
    if(created){std::ofstream out(captureDirectory/"error.txt");out << std::string(error.what()).substr(0,2048);}
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Saekano battle preview: %s",error.what());return 1;
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
    auto bank=std::make_shared<AwbBank>(Profile::Saekano::AssetPath(usrdir, Profile::Saekano::GameAsset::VoiceBank));
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
        !playback.MissionVoicePlaying() || playback.MissionVoicePosition()<0)
      throw FormatError("mission independence: before="+std::to_string(before)+", after="+std::to_string(after)+
          ", dialogue_playing="+std::to_string(ordinary.Playing(7))+", ready="+std::to_string(ordinary.Ready(7))+
          ", mission_playing="+std::to_string(playback.MissionVoicePlaying())+
          ", mission_position="+std::to_string(playback.MissionVoicePosition()));
    ordinary.Stop(7);playback.Render(block);
    voice.Begin(10579,0);playback.Render(block);voice.Disable();
    std::vector<int16_t> tail(9600*2);playback.Render(tail);SaveWav(captureDirectory/"stop-tail.wav",tail);
    report << "],\"unregistered_rejected\":true,\"dialogue_before_ms\":" << before
           << ",\"dialogue_after_ms\":" << after
           << ",\"independent_dialogue\":true,\"stop_tail_retains_position\":true}\n";
    return 0;
  }catch(const std::exception& e) {
    if(created){std::ofstream error(captureDirectory/"error.txt");error << std::string(e.what()).substr(0,2048);}
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Saekano mission voice: %s",e.what());return 1;
  }
}
int RunVoiceAttenuationSession(const std::filesystem::path& usrdir,const std::filesystem::path& captureDirectory){
  bool created=false;
  try{
    CreateCaptureDirectory(usrdir,captureDirectory);created=true;
    if(!SDL_Init(SDL_INIT_EVENTS))throw FormatError(SDL_GetError());
    struct Cleanup{~Cleanup(){SDL_Quit();}}cleanup;
    const auto read=[](const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);if(!f)throw FormatError("missing attenuation resource");return Bytes{std::istreambuf_iterator<char>(f),{}};};
    auto resources=std::make_unique<GameResources>(read(Profile::Saekano::AssetPath(usrdir, Profile::Saekano::GameAsset::ResourceArchive)));const auto clip=resources->LoadSystemVoice(4,0).Data;
    ArchiveBgmResources music(usrdir);music.Prepare(0);MusicPlayback playback(music,true);
    GameEvent bgm{GameEventKind::PlayBgm,0};bgm.Bgm=BgmPlayRequest{0,100,BgmVolume{100,60,0.6f}};playback.Submit(bgm);
    PlaybackVoiceAttenuation attenuation(playback,60,50,true);SystemSoundAssets ui(read(Profile::Saekano::AssetPath(usrdir, Profile::Saekano::GameAsset::FirstArchive)));playback.BindUiSounds(ui,0.5f);
    const auto serial=playback.BgmVoices().Current;uint64_t now=0;std::vector<int16_t> pcm;
    const auto advance=[&](uint32_t ms){
      if(ms%10)throw FormatError("attenuation diagnostic requires exact10ms blocks");
      for(uint32_t i=0;i<ms;i+=10){std::array<int16_t,960> block{};playback.Render(block);pcm.insert(pcm.end(),block.begin(),block.end());now+=10;attenuation.Update(now);}
    };
    playback.Submit({GameEventKind::Sound,2});advance(100);
    if(attenuation.State().Started || playback.BgmOutputGain()!=0.6f)throw FormatError("UI sound triggered voice attenuation");
    // Muted voices retain real preparation/playback/EOF, isolating BGM in PCM.
    playback.PrepareProfileVoice(clip,0);attenuation.StartProfileVoice(now);advance(150);
    if(attenuation.State().CurrentGain!=45 || playback.BgmOutputGain()!=0.45f || playback.ProfileVoiceStopped())throw FormatError("first physical attenuation midpoint differs");
    playback.PrepareMemoryVoice(8,clip,0);attenuation.StartOrdinaryVoice(8,now);advance(150);
    if(attenuation.State().SavedGain!=60 || attenuation.State().CurrentGain!=38 || playback.BgmOutputGain()!=0.38f)throw FormatError("overlapping physical start compounded saved gain");
    playback.StopProfileVoice();advance(150);
    if(attenuation.State().CurrentGain!=30 || attenuation.State().Phase!=0 || !playback.OrdinaryVoicePlaying(8))throw FormatError("remaining voice failed to retain attenuation");
    playback.PrepareMemoryVoice(9,clip,0);
    if(!(playback.VoiceAttenuationInputs().Prepared&(1u<<9)))throw FormatError("physical prepared-state flag missing");
    while(playback.OrdinaryVoicePlaying(8) && now<6000)advance(10);
    if(playback.OrdinaryVoicePlaying(8) || attenuation.State().Phase!=2)throw FormatError("actual voice EOF did not initiate restoration");
    const auto restoreEpoch=attenuation.State().Epoch;advance(150);
    if(attenuation.State().CurrentGain!=45 || playback.BgmOutputGain()!=0.45f)throw FormatError("physical restoration midpoint differs");advance(150);
    if(attenuation.State().CurrentGain!=60 || playback.BgmOutputGain()!=0.6f || playback.BgmVoices().Current!=serial || playback.BgmVoices().Outgoing)
      throw FormatError("physical restoration changed BGM owner or final gain");
    const auto before=attenuation.State();bool rejected=false;
    try{attenuation.StartProfileVoice(now);}catch(const UnsupportedCommand&){rejected=true;}
    if(!rejected || attenuation.State().Epoch!=before.Epoch || attenuation.State().CurrentGain!=before.CurrentGain || attenuation.State().Phase!=before.Phase)
      throw FormatError("unprepared start changed attenuation state");
    rejected=false;try{attenuation.SetPercent(101);}catch(const FormatError&){rejected=true;}
    if(!rejected || attenuation.State().Percent!=50 || playback.BgmOutputGain()!=0.6f)throw FormatError("invalid attenuation setting changed physical output");
    int32_t peak=0;for(const auto sample:pcm)peak=std::max(peak,std::abs(int32_t(sample)));if(peak<=8)throw FormatError("attenuation BGM output is silent");
    SaveWav(captureDirectory/"attenuated-bgm.wav",pcm);
    std::ofstream report(captureDirectory/"summary.json");report<<"{\"passed\":true,\"frames\":"<<pcm.size()/2<<",\"audio_ms\":"<<now<<",\"restore_epoch_ms\":"<<restoreEpoch
      <<",\"target\":0,\"trigger_sources\":[7,8],\"ui_does_not_trigger\":true,\"gain_midpoints\":[45,38,30,45,60],\"voice_eof_observed\":true,\"prepared9_does_not_hold\":true,\"bgm_serial_preserved\":true,\"invalid_preserves\":true,\"pcm_peak\":"<<peak
      <<",\"software_openal_output\":true,\"caller_supplied_audio_clock\":true,\"pc_options_bound\":false,\"manual_acceptance\":false}\n";
    report.close();if(!report)throw FormatError("cannot save attenuation report");return 0;
  }catch(const std::exception& e){if(created){std::ofstream error(captureDirectory/"error.txt");error<<std::string(e.what()).substr(0,2048);}
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Voice attenuation: %s",e.what());return 1;}
}
int RunAudioOwnerSession(const std::filesystem::path& usrdir,const std::filesystem::path& captureDirectory){
  bool created=false;
  try{
    CreateCaptureDirectory(usrdir,captureDirectory);created=true;
    if(!SDL_Init(SDL_INIT_EVENTS))throw FormatError(SDL_GetError());
    struct Cleanup{~Cleanup(){SDL_Quit();}} cleanup;
    const auto read=[](const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);
      if(!f)throw FormatError("missing audio-owner resource");return Bytes{std::istreambuf_iterator<char>(f),{}};};
    GameResources resources(read(Profile::Saekano::AssetPath(usrdir, Profile::Saekano::GameAsset::ResourceArchive)));
    const auto clip=resources.LoadSystemVoice(4,0).Data;
    ArchiveBgmResources music(usrdir);MusicPlayback playback(music,true);
    SystemSoundAssets ui(read(Profile::Saekano::AssetPath(usrdir, Profile::Saekano::GameAsset::FirstArchive)));playback.BindUiSounds(ui,0.5f);
    int32_t peak=0;uint64_t frames=0;std::vector<int32_t> gains;
    const auto advance=[&](uint32_t ms){
      if(ms%10)throw FormatError("audio-owner interval requires exact10ms blocks");
      for(uint32_t i=0;i<ms;i+=10){std::array<int16_t,960> block{};playback.Render(block);
        frames+=480;for(auto sample:block)peak=std::max(peak,std::abs(int32_t(sample)));}
      playback.Update(0); // Observe the completed device interval, without UI time.
    };
    const auto play=[&](int32_t track,int32_t gain){
      music.Prepare(track);
      GameEvent event{GameEventKind::PlayBgm,track};event.Bgm=BgmPlayRequest{track,100,
          BgmVolume{100,uint32_t(gain),float(gain)/100}};playback.Submit(event);
    };
    play(0,60);
    {
      PlaybackVoiceAttenuation owner(playback,60,50,true);owner.Bind();
      const auto expect=[&](int32_t gain){gains.push_back(owner.State().CurrentGain);
        if(owner.State().CurrentGain!=gain || playback.BgmOutputGain()!=float(gain)/100)
          throw FormatError("shared audio owner gain differs from native interpolation");};
      playback.Submit({GameEventKind::Sound,2});advance(100);
      if(owner.State().Started)throw FormatError("UI channel triggered shared attenuation");
      PlaybackOrdinaryVoice dialogue(playback,usrdir,0,1);
      dialogue.PrepareMemory(7,clip);dialogue.Start(7);advance(150);expect(45);
      // A different frontend's UI clock starts at zero. The device clock does not.
      PlaybackProfileVoice profile(playback,0,1);profile.Prepare(clip);profile.Start(0);
      if(owner.State().Epoch!=250)throw FormatError("mode change reset the audio service clock");
      advance(150);expect(38);
      playback.PrepareMissionVoice(0,std::make_unique<HcaWaveReader>(clip),0);
      playback.StartMissionVoice();advance(300);expect(30);
      play(8,40);expect(40);
      dialogue.PrepareMemory(8,clip);dialogue.Start(8);advance(300);expect(20);
      if(owner.State().SavedGain!=40)throw FormatError("script BGM bypassed shared saved gain");
      music.Prepare(7);playback.Submit({GameEventKind::PlayBgmRetainingGain,7});
      const auto serial=playback.BgmVoices().Current;
      if(owner.State().SavedGain!=40 || playback.BgmOutputGain()!=0.2f)
        throw FormatError("direct BGM replacement overwrote the attenuation target");
      dialogue.PrepareMemory(9,clip); // Unstarted preparation cannot hold ducking.
      for(uint32_t channel:{7u,8u,11u})playback.StopOrdinaryVoice(channel);
      // Stops now retain native fading/silent tails. Restoration begins only
      // after those outputs retire; an unstarted prepared token is excluded.
      for(uint32_t waited=0;playback.VoiceAttenuationInputs().Busy && waited<1000;waited+=10)
        advance(10);
      if(playback.VoiceAttenuationInputs().Busy)
        throw FormatError("voice tails exceeded attached fade retirement");
      playback.Update(0);advance(150);expect(30);advance(150);expect(40);
      if(owner.State().SavedGain!=40 || playback.BgmVoices().Current!=serial ||
          !(playback.VoiceAttenuationInputs().Prepared&(1u<<9)) || peak<=8)
        throw FormatError("shared owner restoration lost BGM/prepared state or PCM");
      const auto retirementTime=playback.AudioServiceTime();
      playback.StopAllChannels();
      if(playback.VoiceAttenuation()!=&owner || playback.AudioServiceTime()!=retirementTime)
        throw FormatError("scene retirement replaced the session audio owner/clock");
    }
    if(playback.VoiceAttenuation())throw FormatError("retired attenuation owner remains bound");
    playback.Update(0);
    std::ofstream report(captureDirectory/"summary.json");
    report<<"{\"passed\":true,\"scope\":\"Shared playback ownership and real HCA/OpenAL composition; native integer expectations reused, no native mixed-waveform claim\",\"audio_ms\":"
      <<playback.AudioServiceTime()<<",\"frames\":"<<frames<<",\"pcm_peak\":"<<peak<<",\"gains\":[";
    for(size_t i=0;i<gains.size();++i){if(i)report<<',';report<<gains[i];}
    report<<"],\"direct_dialogue_profile_mission_starts\":true,\"ui_clock_reset_ignored\":true,\"typed_bgm_updates_saved_gain\":true,\"direct_bgm_retains_gain\":true,\"prepared_voice_excluded\":true,\"retirement_preserves_owner_clock\":true,\"owner_detaches\":true}\n";
    report.close();if(!report)throw FormatError("cannot save shared audio-owner report");return 0;
  }catch(const std::exception& e){if(created){std::ofstream error(captureDirectory/"error.txt");error<<std::string(e.what()).substr(0,2048);}
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Audio owner: %s",e.what());return 1;}
}
int RunSlotReplayAudioSession(const std::filesystem::path& usrdir,const std::filesystem::path& captureDirectory){
  bool created=false;
  try{
    CreateCaptureDirectory(usrdir,captureDirectory);created=true;
    if(!SDL_Init(SDL_INIT_EVENTS))throw FormatError(SDL_GetError());
    struct Cleanup{~Cleanup(){SDL_Quit();}} cleanup;
    ArchiveBgmResources music(usrdir);ArchiveSeResources sounds(usrdir);
    MusicPlayback playback(music,true,&sounds);
    music.Prepare(8);sounds.Prepare(0);
    GameEvent bgm{GameEventKind::PlayBgm,8};bgm.Bgm=BgmRequest(8,80,100,music.Coefficient());playback.Submit(bgm);
    std::vector<int16_t> pcm;
    for(int32_t logical=0;logical<4;++logical){
      std::array<float,11> before{};
      for(uint32_t channel=2;channel<=12;++channel){const float gain=float(channel)/16;playback.SetChannelOutputGain(channel,gain);before[channel-2]=gain;}
      SePlayRequest request;request.Track=0;request.Channel=logical+2;request.CommandVolume=60;request.Remember=true;
      request.Volume=SeVolume(80,60,sounds.Coefficient());request.RefreshServiceVolume=false;
      GameEvent se{GameEventKind::PlaySe,0};se.Se=request;playback.Submit(se);
      for(uint32_t channel=2;channel<=12;++channel){
        const auto expected=channel>=uint32_t(logical+2) && channel<uint32_t(logical+6)?request.Volume->Gain:before[channel-2];
        if(playback.ChannelOutputGain(channel)!=expected)throw FormatError("saved SE replay touched the wrong physical gain");
      }
      if(!playback.SoundPlaying(size_t(logical)))throw FormatError("saved SE replay did not start its target");
      std::vector<int16_t> block(4800*2);playback.Render(block);pcm.insert(pcm.end(),block.begin(),block.end());
      playback.Submit({GameEventKind::StopSe,logical+2});
    }
    if(playback.BgmOutputGain()!=0.4f)throw FormatError("saved SE replay changed BGM gain");
    int32_t peak=0;for(auto sample:pcm)peak=std::max(peak,std::abs(int32_t(sample)));
    if(peak<=8)throw FormatError("saved audio replay is silent");
    SaveWav(captureDirectory/"replay.wav",pcm);
    std::ofstream report(captureDirectory/"summary.json");
    report<<"{\"passed\":true,\"logical_channels\":4,\"gain_comparisons\":44,\"audio_frames\":"<<pcm.size()/2<<",\"pcm_peak\":"<<peak
      <<",\"bgm_track\":8,\"se_track\":0,\"bgm_gain\":0.4,\"se_gain\":0.48,\"existing_hca_decoder\":true,\"software_openal_output\":true,\"full_pc_resume_bound\":false}\n";
    report.close();if(!report)throw FormatError("cannot save slot replay audio report");return 0;
  }catch(const std::exception& e){if(created){std::ofstream error(captureDirectory/"error.txt");error<<std::string(e.what()).substr(0,2048);}
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Slot replay audio: %s",e.what());return 1;}
}
int RunOptionsMixerSession(const std::filesystem::path& usrdir,const std::filesystem::path& captureDirectory){
  bool created=false;
  try{
    CreateCaptureDirectory(usrdir,captureDirectory);created=true;
    if(!SDL_Init(SDL_INIT_EVENTS))throw FormatError(SDL_GetError());
    struct Cleanup{~Cleanup(){SDL_Quit();}} cleanup;
    const auto read=[](const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);if(!f)throw FormatError("missing mixer resource");return Bytes{std::istreambuf_iterator<char>(f),{}};};
    auto resources=std::make_unique<GameResources>(read(Profile::Saekano::AssetPath(usrdir, Profile::Saekano::GameAsset::ResourceArchive)));SystemSoundAssets ui(read(Profile::Saekano::AssetPath(usrdir, Profile::Saekano::GameAsset::FirstArchive)));
    ArchiveBgmResources music(usrdir);ArchiveSeResources sounds(usrdir);MusicPlayback playback(music,true,&sounds);
    PlaybackTitleAudio title(playback,ui,1,1);
    title.PlayVoice(resources->LoadSystemVoice(4,0).Data);title.PlayUi(2);
    playback.PrepareMemoryVoice(8,resources->LoadSystemVoice(4,1).Data,0.75f);playback.StartOrdinaryVoice(8);
    playback.PrepareMissionVoice(0,std::make_unique<HcaWaveReader>(resources->LoadSystemVoice(4,2).Data),0.625f);playback.StartMissionVoice();
    music.Prepare(0);music.Prepare(1);
    GameEvent bgm{GameEventKind::PlayBgm,0};bgm.Bgm=BgmPlayRequest{0,100,BgmVolume{100,60,0.6f}};playback.Submit(bgm);
    const auto render=[&](size_t frames){std::vector<int16_t> pcm(frames*2);playback.Render(pcm);return pcm;};render(2400);
    bgm.Id=1;bgm.Bgm->Track=1;playback.Submit(bgm);render(2400);
    const auto voices=playback.BgmVoices();const auto fade=playback.BgmFadeState();
    const auto p7=playback.OrdinaryVoicePosition(7),p8=playback.OrdinaryVoicePosition(8),p11=playback.MissionVoicePosition();
    if(!voices.Current || !voices.Outgoing || !fade.Active() || p7<=0 || p8<=0 || p11<=0)throw FormatError("mixer fixture has no simultaneous sources/fade");
    std::array<float,11> gains{};for(uint32_t ch=2;ch<=12;++ch){const auto g=float(ch)/16;playback.SetChannelOutputGain(ch,g);gains[ch-2]=g;}
    title.SetGains(0.375f,0.25f);gains[5]=0.375f;gains[4]=0.25f;
    playback.SetBgmOutputGain(0.25f);
    for(uint32_t ch=2;ch<=12;++ch)if(playback.ChannelOutputGain(ch)!=gains[ch-2])throw FormatError("live gain changed another channel");
    const auto checkOwners=[&]{
      const auto v=playback.BgmVoices();const auto f=playback.BgmFadeState();
      if(v.Current!=voices.Current || v.Outgoing!=voices.Outgoing || v.FadeOutDuration!=voices.FadeOutDuration ||
          f.Phase!=fade.Phase || f.Duration!=fade.Duration || f.Elapsed!=fade.Elapsed || f.Weight!=fade.Weight || f.From!=fade.From ||
          playback.OrdinaryVoicePosition(7)!=p7 || playback.OrdinaryVoicePosition(8)!=p8 || playback.MissionVoicePosition()!=p11 ||
          !playback.OrdinaryVoicePlaying(7) || !playback.OrdinaryVoicePlaying(8) || !playback.MissionVoicePlaying())
        throw FormatError("live gain restarted or retired a source/fade");
    };
    checkOwners();const auto liveBgm=playback.BgmVoices();
    if(liveBgm.CurrentGain!=0.25f || liveBgm.OutgoingGain!=0.25f*fade.Weight)throw FormatError("live BGM gain lost fade weight");
    uint32_t rejected=0;
    for(const auto bad:{-1.f,1.125f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
      for(uint32_t which=0;which<3;++which){bool failed=false;try{
        if(which==0)playback.SetBgmOutputGain(bad);else if(which==1)playback.SetChannelOutputGain(7,bad);else title.SetGains(0.5f,bad);
      }catch(const FormatError&){failed=true;}if(!failed)throw FormatError("invalid live gain accepted");++rejected;}
    }
    for(const auto channel:{0u,1u,13u}){bool failed=false;try{playback.SetChannelOutputGain(channel,0.5f);}catch(const FormatError&){failed=true;}
      if(!failed)throw FormatError("invalid live channel accepted");++rejected;}
    for(uint32_t ch=2;ch<=12;++ch)if(playback.ChannelOutputGain(ch)!=gains[ch-2])throw FormatError("invalid live gain changed channel state");
    if(playback.BgmOutputGain()!=0.25f)throw FormatError("invalid live gain changed BGM base");checkOwners();
    // Check subsequent title preparations use the new gains, not just current AL values.
    title.PlayVoice(resources->LoadSystemVoice(4,1).Data);title.PlayUi(2);
    if(playback.ChannelOutputGain(7)!=0.375f || playback.ChannelOutputGain(6)!=0.25f)throw FormatError("title preparation restored stale gain");
    for(uint32_t ch=2;ch<=12;++ch)playback.SetChannelOutputGain(ch,0);playback.SetBgmOutputGain(0);
    const auto silent=render(4800);int32_t silentPeak=0;for(const auto sample:std::span(silent).last(2048))silentPeak=std::max(silentPeak,std::abs(int32_t(sample)));
    if(silentPeak>1)throw FormatError("live zero gains left audible output");
    const auto mutedPosition=playback.OrdinaryVoicePosition(7);playback.SetChannelOutputGain(7,0.5f);auto pcm=render(4800);
    int32_t peak=0;for(const auto sample:pcm)peak=std::max(peak,std::abs(int32_t(sample)));
    if(peak<=8 || playback.OrdinaryVoicePosition(7)<=mutedPosition)throw FormatError("live gain did not resume audible advancing output");
    SaveWav(captureDirectory/"live-gain-prefix.wav",pcm);
    std::ofstream report(captureDirectory/"summary.json");report<<"{\"passed\":true,\"physical_channels\":11,\"simultaneous_voices\":[7,8,11],\"gain_changes_preserve_positions\":true,\"bgm_preserves_serials_and_fade\":true,\"future_title_gains\":true,\"invalid_rejections\":"<<rejected
      <<",\"silent_tail_peak\":"<<silentPeak<<",\"unmuted_peak\":"<<peak<<",\"software_openal_output\":true,\"native_option_arithmetic_tested\":false,\"ducking_bound\":false,\"pc_options_bound\":false,\"manual_acceptance\":false}\n";
    report.close();if(!report)throw FormatError("cannot save mixer report");return 0;
  }catch(const std::exception& e){if(created){std::ofstream error(captureDirectory/"error.txt");error<<std::string(e.what()).substr(0,2048);}
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Options mixer: %s",e.what());return 1;}
}
int RunOptionsPreviewSession(const std::filesystem::path& usrdir,const std::filesystem::path& captureDirectory){
  bool created=false;
  try{
    CreateCaptureDirectory(usrdir,captureDirectory);created=true;
    if(!SDL_Init(SDL_INIT_EVENTS))throw FormatError(SDL_GetError());
    struct Cleanup{~Cleanup(){SDL_Quit();}} cleanup;
    const auto read=[](const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);if(!f)throw FormatError("missing preview resource");return Bytes{std::istreambuf_iterator<char>(f),{}};};
    auto resources=std::make_unique<GameResources>(read(Profile::Saekano::AssetPath(usrdir, Profile::Saekano::GameAsset::ResourceArchive)));ArchiveBgmResources music(usrdir);
    MusicPlayback playback(music,true);PlaybackProfileVoice output(playback,10,1);OptionsPreviewVoices samples(*resources,output);
    const auto render=[&](size_t frames){std::vector<int16_t> pcm(frames*2);playback.Render(pcm);return pcm;};
    output.Prepare(resources->LoadSystemVoice(6,0).Data);output.Start(0);render(2400);
    if(output.Stopped())throw FormatError("logo source was unavailable before preview replacement");
    samples.Prepare(0);if(!output.Stopped())throw FormatError("preview preparation did not stop the previous source");
    samples.Start(50);auto pcm=render(24000);const auto before=playback.ProfileVoicePosition();const auto stopped=output.Stopped();
    bool rejected=false;try{samples.Prepare(4);}catch(const FormatError&){rejected=true;}
    if(!rejected || playback.ProfileVoicePosition()!=before || output.Stopped()!=stopped)throw FormatError("invalid preview changed physical7");
    bool consumed=false;try{samples.Start(550);}catch(const UnsupportedCommand&){consumed=true;}if(!consumed)throw FormatError("preview start reused consumed preparation");
    if(!before || *before<=0)throw FormatError("preview sample clock did not advance");
    int32_t peak=0;for(auto sample:pcm)peak=std::max(peak,std::abs(int32_t(sample)));if(peak<=8)throw FormatError("preview prefix is silent");
    output.Stop();auto tail=render(4800);int32_t tailPeak=0;for(auto sample:std::span(tail).last(2048))tailPeak=std::max(tailPeak,std::abs(int32_t(sample)));
    if(!output.Stopped() || tailPeak>1)throw FormatError("preview stop did not settle");
    SaveWav(captureDirectory/"preview-prefix.wav",pcm);
    std::ofstream report(captureDirectory/"summary.json");report<<"{\"passed\":true,\"mode\":0,\"channel\":7,\"frames\":24000,\"rate\":48000,\"channels\":2,\"peak\":"<<peak
      <<",\"position_seconds\":"<<*before<<",\"stop_tail_peak\":"<<tailPeak<<",\"replaced_logo\":true,\"prepare_stops_before_start\":true,\"invalid_preserves\":true,\"start_requires_prepare\":true,\"software_openal_output\":true,\"manual_audible_acceptance\":false,\"pc_options_bound\":false}\n";
    report.close();if(!report)throw FormatError("cannot save preview report");return 0;
  }catch(const std::exception& error){
    if(created){std::ofstream out(captureDirectory/"error.txt");out<<std::string(error.what()).substr(0,2048);}
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Saekano Options preview: %s",error.what());return 1;
  }
}
int RunTitleAudioSession(const std::filesystem::path& usrdir,const std::filesystem::path& captureDirectory){
  bool created=false;
  try{
    CreateCaptureDirectory(usrdir,captureDirectory);created=true;
    if(!SDL_Init(SDL_INIT_EVENTS))throw FormatError(SDL_GetError());
    struct Cleanup{~Cleanup(){SDL_Quit();}} cleanup;
    const auto read=[](const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);if(!f)throw FormatError("missing title audio resource");return Bytes{std::istreambuf_iterator<char>(f),{}};};
    auto resources=std::make_unique<GameResources>(read(Profile::Saekano::AssetPath(usrdir, Profile::Saekano::GameAsset::ResourceArchive)));
    SystemSoundAssets ui(read(Profile::Saekano::AssetPath(usrdir, Profile::Saekano::GameAsset::FirstArchive)));ArchiveBgmResources music(usrdir);
    MusicPlayback playback(music,true);PlaybackTitleAudio output(playback,ui,ProfileVoiceGain(10,1),ProfileVoiceGain(8,1));
    TitleAudioOwner owner(*resources,output);owner.Load(0);
    const auto render=[&](std::vector<int16_t>& pcm,size_t frames){const auto at=pcm.size();pcm.resize(at+frames*2);playback.Render(std::span(pcm).subspan(at));};
    std::vector<int16_t> replacement;owner.PlayEntry(0);render(replacement,2400);
    if(!owner.Playing(7))throw FormatError("title logo cue did not remain active before replacement");
    const auto beforeReplace=playback.OrdinaryVoicePosition(7);owner.PlayMenu(0);owner.PlayUi(1);
    const bool overlap=owner.Playing(6) && owner.Playing(7);if(!overlap)throw FormatError("title menu voice and UI sound did not overlap independently");
    const auto afterReplace=playback.OrdinaryVoicePosition(7);
    for(size_t tick=0;(owner.Playing(6) || owner.Playing(7)) && tick<2000;++tick)render(replacement,480);
    if(owner.Playing(6) || owner.Playing(7))throw FormatError("title audio failed to reach actual source EOF");
    render(replacement,2400);SaveWav(captureDirectory/"replacement.wav",replacement);
    size_t audible=0;for(auto sample:replacement)if(sample>8 || sample<-8)++audible;
    if(!audible)throw FormatError("title audio capture is silent");
    std::vector<int16_t> mode4;owner.PlayEntry(2);render(mode4,2400);
    const auto beforeMode4=playback.OrdinaryVoicePosition(7);owner.Load(4);owner.PlayMenu(3);
    const auto afterMode4=playback.OrdinaryVoicePosition(7);
    const bool preserved=owner.Playing(7) && beforeMode4==afterMode4 && owner.MenuCount()==0;
    if(!preserved)throw FormatError("empty title voice mode replaced an active logo cue");
    owner.PlayUi(0);owner.Stop(7);const bool independent=owner.Playing(6) && !owner.Playing(7);
    if(!independent)throw FormatError("title voice stop affected UI source6");
    owner.Stop(6);if(owner.Playing(6))throw FormatError("title UI stop failed");
    SaveWav(captureDirectory/"mode4-prefix.wav",mode4);
    owner.PlayEntry(3);owner.PlayUi(3);owner.Retire();
    if(owner.Ready() || playback.UiSoundPlaying() || playback.OrdinaryVoicePlaying(7))throw FormatError("title audio retirement left a source/bank live");
    owner.Load(1);owner.PlayMenu(0);std::vector<int16_t> rebound;render(rebound,2400);
    if(!owner.Playing(7))throw FormatError("title audio did not rebind after retirement");
    owner.Retire();SaveWav(captureDirectory/"rebound-prefix.wav",rebound);
    std::ofstream report(captureDirectory/"summary.json");report<<"{\"passed\":true,\"sample_rate\":48000,\"channels\":2,\"voice_channel\":7,\"ui_channel\":6,\"replacement_frames\":"<<replacement.size()/2
      <<",\"audible_samples\":"<<audible<<",\"before_replace_ms\":"<<beforeReplace<<",\"after_replace_ms\":"<<afterReplace<<",\"mode4_position_ms\":"<<afterMode4
      <<",\"overlap\":true,\"mode4_preserves_logo\":true,\"independent_stops\":true,\"retirement_and_rebind\":true,\"software_openal_output\":true,\"manual_audible_acceptance\":false,\"pc_title_constructed\":false}\n";
    report.close();if(!report)throw FormatError("cannot save title audio session report");return 0;
  }catch(const std::exception& e){if(created){std::ofstream error(captureDirectory/"error.txt");error<<std::string(e.what()).substr(0,2048)<<'\n';}SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Saekano title audio: %s",e.what());return 1;}
}

int RunSharedAudioStopSession(const std::filesystem::path& usrdir,
                             const std::filesystem::path& captureDirectory) {
  bool created = false;
  try {
    CreateCaptureDirectory(usrdir, captureDirectory); created = true;
    if (!SDL_Init(SDL_INIT_EVENTS)) throw FormatError(SDL_GetError());
    struct Cleanup { ~Cleanup() { SDL_Quit(); } } cleanup;
    std::ifstream first(Profile::Saekano::AssetPath(usrdir, Profile::Saekano::GameAsset::FirstArchive), std::ios::binary);
    if (!first) throw FormatError("missing UI sound bank");
    SystemSoundAssets ui(Bytes{std::istreambuf_iterator<char>(first), {}});
    ArchiveBgmResources music(usrdir); ArchiveSeResources sounds(usrdir);
    MusicPlayback playback(music, true, &sounds);
    PlaybackOrdinaryVoice voice(playback, usrdir, 10, 1.0f);
    struct Mouth final : OrdinaryMessageMouth {
      size_t Calls = 0;
      void Begin(int32_t, const ProfileMouthAnimation&, uint64_t) override { ++Calls; }
      void End(int32_t) override { ++Calls; }
    } mouth;
    SceneMessageVoiceOwner owner{voice, mouth, MakeFreshOrdinaryVoiceQueue(), {}, {}, {}};
    owner.SecondaryQueue = MakeFreshOrdinaryVoiceQueue();
    owner.SecondaryQueue->BaseChannel = 9;
    for (auto* queue : {&owner.Queue, &*owner.SecondaryQueue}) {
      queue->Catalog = {7490, 7491, 7492};
      PrepareOrdinaryVoice(*queue, 7490, true, voice);
    }
    for (uint32_t channel : {8u, 10u}) voice.Start(channel);
    for (uint32_t channel : {11u, 12u}) {
      voice.Prepare(channel, 7490); voice.Start(channel);
    }
    playback.BindUiSounds(ui, 0.5f);
    playback.Submit({GameEventKind::Sound, 2});
    sounds.Prepare(0);
    for (int32_t channel = 2; channel <= 5; ++channel) {
      GameEvent sound{GameEventKind::PlaySe, 0};
      sound.Se = SePlayRequest{0, channel, 100, false, BgmVolume{100, 50, 0.5f}};
      playback.Submit(sound);
    }
    music.Prepare(0); music.Prepare(1);
    playback.SetBgmOutputGain(0.375f);
    playback.Submit({GameEventKind::SetBgmFadeDuration, 0, {}, 213});
    playback.Submit({GameEventKind::PlayBgmRetainingGain, 0});
    const auto render = [&](size_t frames) {
      std::vector<int16_t> pcm(frames * 2); playback.Render(pcm); return pcm;
    };
    render(768);
    playback.Submit({GameEventKind::PlayBgmRetainingGain, 1});
    std::array<float, 11> gains{};
    for (uint32_t channel = 2; channel <= 12; ++channel) {
      playback.SetChannelOutputGain(channel, float(channel) / 16);
      gains[channel - 2] = playback.ChannelOutputGain(channel);
    }
    const auto before = playback.BgmVoices();
    constexpr uint32_t voiceMask = 0x3fu << 7;
    if (!before.Current || !before.Outgoing || !playback.BgmFadeState().Active() ||
        playback.VoiceAttenuationInputs().Busy != voiceMask || !playback.UiSoundPlaying())
      throw FormatError("shared stop fixture lacks active sources/crossfade");
    for (size_t channel = 2; channel <= 5; ++channel)
      if (!playback.SoundPlaying(channel - 2)) throw FormatError("shared stop fixture lacks SE source");
    const auto prefix = render(768);
    const auto peak = [](std::span<const int16_t> pcm) {
      int32_t value = 0;
      for (auto sample : pcm) value = std::max(value, std::abs(int32_t(sample)));
      return value;
    };
    const auto prefixPeak = peak(prefix);
    if (prefixPeak <= 8) throw FormatError("shared stop fixture produced no PCM");
    const auto next = owner.Queue.NextSlot, secondaryNext = owner.SecondaryQueue->NextSlot;
    if (owner.Queue.Prepared.empty() || owner.SecondaryQueue->Prepared.empty())
      throw FormatError("shared stop fixture lacks lookahead caches");
    const auto stop = [&] {
      for (uint32_t channel = 2; channel <= 12; ++channel)
        gains[channel - 2] = playback.ChannelOutputGain(channel);
      playback.StopAllChannels(); InvalidateSceneVoicePreparations(owner);
    };
    const auto checkStopped = [&] {
      if (playback.Playing() || playback.VoiceAttenuationInputs().Busy ||
          playback.VoiceAttenuationInputs().Prepared || playback.BgmFadeState().Active())
        throw FormatError("shared audio stop retained active/prepared sources");
      for (uint32_t channel = 7; channel <= 12; ++channel)
        if (voice.Ready(channel) || voice.Playing(channel) || voice.Position(channel) != -1)
          throw FormatError("shared audio stop retained voice ownership");
      const auto after = playback.BgmVoices();
      if (after.Current || after.Outgoing || after.FadeOutDuration != 213 ||
          after.Retired.size() != 2 ||
          std::count(after.Retired.begin(), after.Retired.end(), before.Current) != 1 ||
          std::count(after.Retired.begin(), after.Retired.end(), before.Outgoing) != 1 ||
          playback.BgmOutputGain() != 0.375f)
        throw FormatError("shared audio stop corrupted BGM retirement/settings");
      for (uint32_t channel = 2; channel <= 12; ++channel)
        if (playback.ChannelOutputGain(channel) != gains[channel - 2])
          throw FormatError("shared audio stop changed output gain on channel " + std::to_string(channel));
      if (!owner.Queue.Prepared.empty() || !owner.SecondaryQueue->Prepared.empty() ||
          owner.Queue.NextSlot != next || owner.SecondaryQueue->NextSlot != secondaryNext ||
          owner.Queue.Catalog != std::vector<int16_t>{7490, 7491, 7492} ||
          owner.SecondaryQueue->Catalog != owner.Queue.Catalog || mouth.Calls)
        throw FormatError("shared stop corrupted retained dialogue owner");
    };
    stop(); checkStopped(); stop(); checkStopped();
    playback.Update(0.016f);
    const auto silence = render(4800);
    const auto silentPeak = peak(std::span(silence).last(2048));
    if (silentPeak > 1) throw FormatError("shared stop left audible output");
    // Prepared-but-not-started handles include the mission and profile aliases.
    for (uint32_t channel = 7; channel <= 12; ++channel) voice.Prepare(channel, 7490);
    if (playback.VoiceAttenuationInputs().Prepared != voiceMask)
      throw FormatError("shared stop fixture lacks prepared voice handles");
    stop(); checkStopped();
    playback.PrepareProfileVoice(ui.Load(2).Data, gains[5]);
    if (!(playback.VoiceAttenuationInputs().Prepared & (1u << 7)))
      throw FormatError("shared stop fixture lacks profile preparation");
    stop(); checkStopped();
    const auto slot = PrepareOrdinaryVoice(owner.Queue, 7491, true, voice);
    const auto recovered = render(4800);
    const auto recoveredPeak = peak(recovered);
    if (!voice.Playing(owner.Queue.BaseChannel + slot) || recoveredPeak <= 8 ||
        voice.Position(owner.Queue.BaseChannel + slot) < 95)
      throw FormatError("shared stop did not allow fresh voice preparation");
    SaveWav(captureDirectory / "recovered-voice.wav", recovered);
    stop();
    std::ofstream report(captureDirectory / "summary.json");
    report << "{\"passed\":true,\"physical_sources\":13,\"prepared_voice_channels\":6,"
              "\"profile_alias_cleared\":true,\"bgm_retirements\":2,"
              "\"idempotent\":true,\"settings_preserved\":true,\"cache_allocation_preserved\":true,"
              "\"prefix_peak\":" << prefixPeak << ",\"silent_tail_peak\":" << silentPeak
           << ",\"recovered_peak\":" << recoveredPeak
           << ",\"software_openal_output\":true,\"native_pcm_timing_compared\":false}\n";
    report.close(); if (!report) throw FormatError("cannot save shared stop report");
    return 0;
  } catch (const std::exception& error) {
    if (created) { std::ofstream out(captureDirectory / "error.txt"); out << error.what() << '\n'; }
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Shared audio stop: %s", error.what());
    return 1;
  }
}
int RunUiSoundSession(const std::filesystem::path& usrdir,
                      const std::filesystem::path& captureDirectory) {
  bool created = false;
  try {
    CreateCaptureDirectory(usrdir, captureDirectory);
    created = true;
    std::ifstream first(Profile::Saekano::AssetPath(usrdir, Profile::Saekano::GameAsset::FirstArchive), std::ios::binary);
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
    bool otherPlayingAtFirstStop = false,firstSeStillRetiring=false;
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
            if(soundChannel && stopRequests==1)
              firstSeStillRetiring=playback.SoundPlaying(*soundChannel);
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
    // Script completion precedes physical retirement: fade duration plus the
    // native500ms silent tail. Exercise both authored500 and zero through VM.
    if(soundChannel) {
      if(!otherPlayingAtFirstStop || !firstSeStillRetiring)
        throw FormatError("SE stop cut a source or affected the other channel");
      for(size_t tick=0;tick<65 && playback.Playing();++tick) {
        if(capture){const auto offset=samples.size();samples.resize(offset+1600);playback.Render(std::span(samples).subspan(offset));}
        else {playback.Update(1.0f/60);SDL_Delay(16);}
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
          << ",\"stop_fade_ms\":500,\"silent_retirement_ms\":500,\"first_se_retiring_at_second_stop\":true"
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
int RunVoiceFadeSession(const std::filesystem::path& usrdir,
                       const std::filesystem::path& captureDirectory) {
  bool created = false;
  try {
    CreateCaptureDirectory(usrdir, captureDirectory); created = true;
    ArchiveBgmResources resources(usrdir);
    if (!SDL_Init(SDL_INIT_EVENTS)) throw FormatError(SDL_GetError());
    struct Cleanup { ~Cleanup() { SDL_Quit(); } } cleanup;
    MusicPlayback playback(resources, true);
    PlaybackOrdinaryVoice pc(playback, usrdir, 10, 1.0f);
    ClockedOrdinaryVoice headless(usrdir);
    uint64_t now = 0;
    headless.Update(now);
    std::array<int16_t, 960> pcm;
    int tailPeak = 0, silentPeak = 0;
    auto advance = [&](uint32_t milliseconds, bool tail = false, bool silent = false) {
      for (uint32_t elapsed = 0; elapsed < milliseconds; elapsed += 10) {
        playback.Render(pcm); now += 10; headless.Update(now);
        if (pc.Playing(7) != headless.Playing(7))
          throw FormatError("PC/headless active voice retirement differs");
        for (const auto sample : pcm) {
          if (tail) tailPeak = std::max(tailPeak, std::abs(int(sample)));
          if (silent) silentPeak = std::max(silentPeak, std::abs(int(sample)));
        }
      }
    };
    auto prepare = [&] { pc.Prepare(7, 7491); headless.Prepare(7, 7491); };
    auto start = [&] { pc.Start(7); headless.Start(7); };
    auto stop = [&] { pc.Stop(7); headless.Stop(7); };
    prepare(); start(); advance(100);
    stop(); advance(100, true);
    if (!pc.Playing(7) || !headless.Playing(7) || tailPeak <= 32)
      throw FormatError("active voice was cut instead of faded");
    advance(500); advance(100, false, true);
    if (!pc.Playing(7) || !headless.Playing(7) || silentPeak > 1)
      throw FormatError("voice silent retirement interval is missing");
    advance(300);
    if (pc.Playing(7) || headless.Playing(7))
      throw FormatError("voice survived fade plus retirement delay");
    prepare(); start(); advance(100); stop(); advance(100); stop();
    if (pc.Playing(7) || headless.Playing(7))
      throw FormatError("repeated stop restarted the fade");
    prepare(); start(); advance(100);
    prepare(); // Old tail continues while the new token is prepared.
    if (!pc.Ready(7) || !headless.Ready(7) || pc.Playing(7) || headless.Playing(7))
      throw FormatError("prepared replacement exposes the old playing token");
    start(); advance(100);
    if (!pc.Playing(7) || !headless.Playing(7))
      throw FormatError("replacement start did not retire the old fade");
    prepare(); stop();
    if (pc.Ready(7) || headless.Ready(7) || pc.Playing(7) || headless.Playing(7))
      throw FormatError("prepared cancellation retained a playback");
    prepare(); start(); advance(100); playback.StopAllChannels();
    if (pc.Playing(7) || playback.VoiceAttenuationInputs().Busy ||
        playback.VoiceAttenuationInputs().Prepared)
      throw FormatError("immediate teardown retained voice tails");
    std::ofstream report(captureDirectory / "report.json");
    report << "{\"passed\":true,\"voice_id\":7491,\"fade_ms\":500,"
        "\"silent_delay_ms\":500,\"pc_headless_retirement_agrees\":true,"
        "\"repeat_stop_immediate\":true,\"replacement_start\":true,"
        "\"prepared_cancel\":true,\"immediate_teardown\":true,\"tail_peak\":"
        << tailPeak << ",\"silent_peak\":" << silentPeak
        << ",\"rendered_frames\":" << now * 48 << "}\n";
    if (!report) throw FormatError("cannot write voice fade report");
    return 0;
  } catch (const std::exception& error) {
    if (created) { std::ofstream out(captureDirectory / "error.txt"); out << error.what() << '\n'; }
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Saekano voice fade: %s", error.what());
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
    std::ifstream file(Profile::Saekano::AssetPath(usrdir, Profile::Saekano::GameAsset::ResourceArchive), std::ios::binary);
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
    auto bank = std::make_shared<AwbBank>(Profile::Saekano::AssetPath(usrdir, Profile::Saekano::GameAsset::VoiceBank));
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
    std::ifstream file(Profile::Saekano::AssetPath(usrdir, Profile::Saekano::GameAsset::ResourceArchive), std::ios::binary);
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
    GameResources resources(read(Profile::Saekano::AssetPath(usrdir, Profile::Saekano::GameAsset::ResourceArchive)));
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
        ScriptNameIndex::FromFirstArchive(read(Profile::Saekano::AssetPath(usrdir, Profile::Saekano::GameAsset::FirstArchive))),
        TextFormatEnvironment::Decode(
            read(Profile::Saekano::AssetPath(usrdir, Profile::Saekano::GameAsset::StringFormatEnvironment)), true),
        TextTimingEnvironment::Decode(read(Profile::Saekano::AssetPath(usrdir, Profile::Saekano::GameAsset::ConfigurationEnvironment)),
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
