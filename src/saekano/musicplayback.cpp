#include "musicplayback.h"

#include "headlessbackend.h"
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

namespace Impacto::Saekano {
namespace {
class AwbAudioStream final : public Audio::AudioStream {
 public:
  AwbAudioStream(ArchiveBgmResources& resources, int32_t track)
      : AwbAudioStream(resources.Open(track), track, "bgm.awb") {}
  AwbAudioStream(ArchiveSeResources& resources, int32_t track)
      : AwbAudioStream(resources.Open(track), track, "se.awb") {}
  AwbAudioStream(std::unique_ptr<HcaWaveReader> reader, int32_t track,
                  const char* bank) : reader_(std::move(reader)) {
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
}  // namespace

struct MusicPlayback::Impl {
  ArchiveBgmResources& Resources;
  ArchiveSeResources* Sounds;
  std::unique_ptr<Audio::OpenAL::AudioBackend> Backend;
  ALCdevice* Device = nullptr;
  ALCcontext* Context = nullptr;
  LPALCRENDERSAMPLESSOFT RenderSamples = nullptr;
  std::unique_ptr<Audio::OpenAL::OpenALAudioChannel> Channel;
  std::array<std::unique_ptr<Audio::OpenAL::OpenALAudioChannel>,4> SeChannels;
  float OldMaster = Audio::MasterVolume;
  float OldGroup = Audio::GroupVolumes[Audio::ACG_BGM];
  float OldSeGroup = Audio::GroupVolumes[Audio::ACG_SE];
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
      Channel = std::make_unique<Audio::OpenAL::OpenALAudioChannel>(
          Audio::AC_BGM0, Audio::ACG_BGM);
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
        std::make_unique<AwbAudioStream>(impl_->Resources, request.Track);
    const bool loop = impl_->Resources.Info(request.Track).Loop;
    impl_->Channel->SetVolume(request.Volume->Gain);
    impl_->Channel->Play(std::move(stream), loop, 0);
  } else if (event.Kind == GameEventKind::StopBgm) {
    if (event.Parameter != 0)
      throw UnsupportedCommand(
          "native BGM fade-time conversion is not verified");
    impl_->Channel->Stop(0);
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
    auto stream = std::make_unique<AwbAudioStream>(*impl_->Sounds, request.Track);
    // The final service refresh writes the same SE gain to backend2..5.
    // BGM is recomputed from unchanged service percentages, preserving its
    // applied gain here. The voice consumer remains separate. Remember is
    // never a looping flag.
    for (auto& channel : impl_->SeChannels) channel->SetVolume(gain);
    impl_->SeChannels[size_t(request.Channel - 2)]->Play(std::move(stream),
        impl_->Sounds->Info(request.Track).Loop, 0);
  } else if (event.Kind == GameEventKind::StopSe) {
    if (!impl_->Sounds || event.Id < 2 || event.Id > 5)
      throw UnsupportedCommand("SE stop outside the four native SE channels");
    if (event.Parameter != 0)
      throw UnsupportedCommand("native SE fade-time conversion is not verified");
    impl_->SeChannels[size_t(event.Id - 2)]->Stop(0);
  } else
    throw UnsupportedCommand(
        "Saekano audio host cannot consume this event yet");
  AlCheck("BGM request");
}
void MusicPlayback::Update(float seconds) {
  impl_->Channel->Update(seconds);
  for (auto& channel : impl_->SeChannels) if (channel) channel->Update(seconds);
  AlCheck("BGM frame");
}
bool MusicPlayback::Playing() const {
  if (impl_->Channel->GetState() != Audio::ACS_Stopped) return true;
  for (auto& channel : impl_->SeChannels)
    if (channel && channel->GetState() != Audio::ACS_Stopped) return true;
  return false;
}
bool MusicPlayback::SoundPlaying(size_t logicalChannel) const {
  const auto& channel = impl_->SeChannels.at(logicalChannel);
  return channel && channel->GetState() != Audio::ACS_Stopped;
}
void MusicPlayback::Render(std::span<int16_t> output) {
  if (!impl_->RenderSamples || output.size() % 2)
    throw FormatError("invalid stereo capture request");
  size_t frame = 0;
  while (frame < output.size() / 2) {
    const auto count = std::min<size_t>(480, output.size() / 2 - frame);
    Update(float(count) / 48000);
    impl_->RenderSamples(impl_->Device, output.data() + frame * 2,
                         ALCsizei(count));
    if (alcGetError(impl_->Device) != ALC_NO_ERROR)
      throw FormatError("OpenAL capture failed");
    frame += count;
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
    if (capture) {
      const auto output = std::filesystem::weakly_canonical(captureDirectory);
      const auto data = std::filesystem::weakly_canonical(usrdir);
      for (auto parent = output; !parent.empty();
           parent = parent.parent_path()) {
        if (parent == data ||
            (data.filename() == "USRDIR" && parent == data.parent_path()))
          throw FormatError("audio capture overlaps original game data");
        if (parent == parent.parent_path()) break;
      }
      if (std::filesystem::exists(output))
        throw FormatError("choose a fresh audio capture directory");
      std::filesystem::create_directories(output);
    }
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
}  // namespace Impacto::Saekano
