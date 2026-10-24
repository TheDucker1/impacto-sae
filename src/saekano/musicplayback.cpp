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
  std::array<std::unique_ptr<Audio::OpenAL::OpenALAudioChannel>,4> SeChannels;
  std::unique_ptr<Audio::OpenAL::OpenALAudioChannel> ProfileVoice;
  std::unique_ptr<HcaPlaybackStream> PreparedProfileVoice;
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
      ProfileVoice = std::make_unique<Audio::OpenAL::OpenALAudioChannel>(
          Audio::AC_VOICE0, Audio::ACG_Voice);
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
    ProfileVoice.reset();
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
    auto stream = std::make_unique<HcaPlaybackStream>(*impl_->Sounds, request.Track);
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
  impl_->ProfileVoice->Update(seconds);
  AlCheck("BGM frame");
}
bool MusicPlayback::Playing() const {
  if (impl_->Channel->GetState() != Audio::ACS_Stopped) return true;
  if (impl_->ProfileVoice->GetState() != Audio::ACS_Stopped) return true;
  for (auto& channel : impl_->SeChannels)
    if (channel && channel->GetState() != Audio::ACS_Stopped) return true;
  return false;
}
bool MusicPlayback::SoundPlaying(size_t logicalChannel) const {
  const auto& channel = impl_->SeChannels.at(logicalChannel);
  return channel && channel->GetState() != Audio::ACS_Stopped;
}
void MusicPlayback::PrepareProfileVoice(Bytes encoded, float gain) {
  if (!std::isfinite(gain) || gain < 0 || gain > 1)
    throw FormatError("profile voice gain outside supported range");
  auto stream = std::make_unique<HcaPlaybackStream>(std::move(encoded));
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
