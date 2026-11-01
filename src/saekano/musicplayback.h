#pragma once
#include <functional>

#include "commands.h"
#include "profilevoiceoutput.h"
#include "ordinaryvoice.h"
#include "titleaudio.h"
#include "audioducking.h"
#include <set>

namespace Impacto::Saekano {
class SystemSoundAssets;
struct BgmPlaybackSnapshot {
  uint64_t Current=0,Outgoing=0;
  float CurrentGain=0,OutgoingGain=0;
  uint32_t FadeOutDuration=500;
  std::vector<uint64_t> Retired; // Retirements since the last BGM request.
};
// Saekano's audio host uses Impacto's existing OpenAL channel and the same
// streaming HCA reader as headless verification. One host owns the AL context.
class MusicPlayback {
 public:
  explicit MusicPlayback(ArchiveBgmResources& resources, bool loopback = false,
                         ArchiveSeResources* sounds = nullptr);
  ~MusicPlayback();
  void Submit(const GameEvent& event);
  void Update(float seconds);
  void Render(std::span<int16_t> stereoPcm);  // Offline 48kHz loopback only.
  bool Playing() const;
  // Native shared audio utility's immediate all-channel stop. Retire both BGM
  // sources, cancel active fades and discard prepared voice handles. Preserve
  // gain settings and attached fade duration; script remembered IDs are external.
  void StopAllChannels();
  bool SoundPlaying(size_t logicalChannel) const;
  BgmStopFade BgmFadeState() const;
  float BgmOutputGain() const;
  BgmPlaybackSnapshot BgmVoices() const;
  // Effective gains, after native option/command/coefficient arithmetic. These
  // setters preserve decoder ownership, playback position and attached fades.
  void SetBgmOutputGain(float gain);
  void SetChannelOutputGain(uint32_t channel,float gain); // Native2..12.
  float ChannelOutputGain(uint32_t channel) const;
  AudioDuckingInputs VoiceAttenuationInputs() const;
  // Native UI channel6 is independent of SE2..5 and profile voice7.
  // Caller supplies a known effective gain and preserves the asset lifetime.
  void BindUiSounds(SystemSoundAssets& resources, float gain);
  bool UiSoundPlaying() const;
  void StopUiSound();
  // Native profile channel7, mapped onto an existing Impacto voice channel.
  // Caller supplies known effective gain. Preparation parses/owns HCA bytes;
  // no playback or successful missing-channel stop is fabricated.
  void PrepareProfileVoice(Bytes encoded, float gain);
  void PrepareMemoryVoice(uint32_t channel, Bytes encoded, float gain);
  void StartProfileVoice();
  void StopProfileVoice();
  void ResetProfileVoice();
  bool ProfileVoiceStopped() const;
  std::optional<float> ProfileVoicePosition() const;
  void PrepareOrdinaryVoice(uint32_t channel, uint32_t voiceId,
                            std::unique_ptr<HcaWaveReader> reader, float gain);
  void StartOrdinaryVoice(uint32_t channel);
  void StopOrdinaryVoice(uint32_t channel);
  bool OrdinaryVoiceReady(uint32_t channel) const;
  bool OrdinaryVoicePlaying(uint32_t channel) const;
  int32_t OrdinaryVoicePosition(uint32_t channel) const;  // Milliseconds, or-1.
  void PlayMissionVoice(uint32_t voiceId,std::unique_ptr<HcaWaveReader> reader,float gain);
  void PrepareMissionVoice(uint32_t voiceId,std::unique_ptr<HcaWaveReader> reader,float gain);
  void StartMissionVoice();
  bool MissionVoiceReady() const;
  void ResetMissionVoice();
  bool MissionVoicePlaying() const;
  int32_t MissionVoicePosition() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Explicit owner for the native target0 attenuation service. Use this object's
// gain/start methods while bound; pass the actual audio-service clock once the
// physical host has been pumped. UI and cinematic audio have separate owners.
class PlaybackVoiceAttenuation {
 public:
  PlaybackVoiceAttenuation(MusicPlayback& output,int32_t gainPercent,int32_t attenuationPercent,bool enabled);
  const BgmVoiceAttenuation& State()const{return state_;}
  void SetGain(int32_t percent);
  void SetPercent(int32_t percent){state_.SetPercent(percent);}
  void SetEnabled(bool enabled){state_.SetEnabled(enabled);}
  void StartProfileVoice(uint64_t audioNow);
  void StartOrdinaryVoice(uint32_t channel,uint64_t audioNow);
  void Update(uint64_t audioNow);
 private:
  MusicPlayback& output_;BgmVoiceAttenuation state_;
};

// On-demand voice.awb preparation7..12, with11 sharing the mission source.
// Text/actor actions and the frontend's update clock remain with their owner.
class PlaybackOrdinaryVoice final : public OrdinaryVoiceOutput {
 public:
  PlaybackOrdinaryVoice(MusicPlayback& playback,
                        const std::filesystem::path& usrdir,
                        uint8_t volume, float coefficient);
  void SetVolume(uint8_t volume);
  void Prepare(uint32_t channel, int16_t voiceId) override;
  void PrepareMemory(uint32_t channel, Bytes encoded) override;
  void Start(uint32_t channel) override;
  void Stop(uint32_t channel) override;
  void Update(uint64_t) override {}  // Frontend pumps the shared physical host.
  bool Ready(uint32_t channel) const override;
  bool Playing(uint32_t channel) const override;
  int32_t Position(uint32_t channel) const override;
 private:
  MusicPlayback& playback_;
  std::shared_ptr<AwbBank> bank_;
  float gain_;
};

class PlaybackTitleAudio final : public TitleAudioOutput {
 public:
  PlaybackTitleAudio(MusicPlayback& playback,SystemSoundAssets& ui,float voiceGain,float uiGain);
  // Borrow the playback owner's already bound UI bank (scenario submenus).
  PlaybackTitleAudio(MusicPlayback& playback,float voiceGain);
  void SetGains(float voiceGain,float uiGain);
  void BindVoiceAttenuation(PlaybackVoiceAttenuation& owner,std::function<uint64_t()> clock);
  void PlayVoice(Bytes encoded)override;
  void PlayUi(int32_t id)override;
  void Stop(uint32_t channel)override;
  bool Playing(uint32_t channel)const override;
 private:
  MusicPlayback& playback_;float gain_;
  PlaybackVoiceAttenuation* attenuation_=nullptr;std::function<uint64_t()> audioClock_;
};

// Native mission channel11 shares its source with battle preview11.
// Uses the existing HCA stream; settings/coefficient must be known current state.
class PlaybackMissionVoice final : public ApproachMissionAudio {
 public:
  PlaybackMissionVoice(MusicPlayback& playback,const std::filesystem::path& usrdir,
                       uint8_t volume,float coefficient);
  void Register(std::span<const int32_t> ids) override;
  void Begin(int32_t id,uint64_t now) override;
  void Prepare(int32_t id) override;
  void Start(uint64_t now) override;
  void Update(uint64_t) override {}  // The physical host owns the clock.
  void Disable() override;
  void Reset() override { Disable();catalog_.clear(); }
  float Gain() const { return gain_; }
 private:
  MusicPlayback& playback_;
  std::shared_ptr<AwbBank> bank_;
  std::set<int32_t> catalog_;
  float gain_;
};
// The frontend pumps the shared device; Update does not substitute a script
// clock for the profile channel's actual playback state.
class PlaybackProfileVoice final : public ProfileVoiceOutput {
 public:
  PlaybackProfileVoice(MusicPlayback& playback, uint8_t option,
                       float channelCoefficient);
  void Reset() override;
  void Prepare(Bytes encoded) override;
  void Start(uint64_t now) override;
  void Stop() override;
  bool Stopped() const override;
  void Update(uint64_t) override {}
  float Gain() const { return gain_; }

 private:
  MusicPlayback& playback_;
  float gain_;
  bool present_ = false;
};

// Bounded owning-VM integration: BGM/wait/stop, or two overlapping SE channels
// with shared gain and independent stops. Capture writes actual software-device
// WAV output and diagnostics. soundChannel selects the SE sequence.
int RunMusicSession(const std::filesystem::path& usrdir, uint32_t track,
                    const std::filesystem::path& captureDirectory = {},
                    std::optional<uint32_t> soundChannel = std::nullopt);
int RunBgmFadeSession(const std::filesystem::path& usrdir, uint32_t track,
                       const std::filesystem::path& captureDirectory);
int RunBgmReplacementSession(const std::filesystem::path& usrdir,
                            const std::filesystem::path& captureDirectory);
int RunProfileVoiceSession(const std::filesystem::path& usrdir,
                           uint32_t character, uint32_t entry,
                           const std::filesystem::path& captureDirectory = {});
int RunOrdinaryVoiceSession(const std::filesystem::path& usrdir,
                            const std::filesystem::path& captureDirectory);
int RunBattlePreviewVoiceSession(const std::filesystem::path& usrdir,
                                 const std::filesystem::path& captureDirectory);
int RunBattleResultVoiceSession(const std::filesystem::path& usrdir,
                                const std::filesystem::path& captureDirectory);
int RunBattleResultMusicSession(const std::filesystem::path& usrdir,
                                const std::filesystem::path& captureDirectory);
int RunMissionVoiceSession(const std::filesystem::path& usrdir,
                           const std::filesystem::path& captureDirectory,bool lifecycleOnly=false);
int RunMissionVoicePrepareSession(const std::filesystem::path& usrdir,
                                  const std::filesystem::path& captureDirectory);
int RunOrdinaryVoiceStopSession(const std::filesystem::path& usrdir,
                               const std::filesystem::path& captureDirectory);
int RunUiSoundSession(const std::filesystem::path& usrdir,
                      const std::filesystem::path& captureDirectory);
int RunSharedAudioStopSession(const std::filesystem::path& usrdir,
                             const std::filesystem::path& captureDirectory);
int RunTitleAudioSession(const std::filesystem::path& usrdir,
                         const std::filesystem::path& captureDirectory);
int RunVoiceAttenuationSession(const std::filesystem::path& usrdir,
                               const std::filesystem::path& captureDirectory);
int RunOptionsMixerSession(const std::filesystem::path& usrdir,
                           const std::filesystem::path& captureDirectory);
int RunSlotReplayAudioSession(const std::filesystem::path& usrdir,
                              const std::filesystem::path& captureDirectory);
int RunOptionsPreviewSession(const std::filesystem::path& usrdir,
                             const std::filesystem::path& captureDirectory);
int RunProfileServiceSession(
    const std::filesystem::path& usrdir, uint32_t scene, uint32_t ordinal,
    uint8_t volume, const std::filesystem::path& captureDirectory = {});
}  // namespace Impacto::Saekano
