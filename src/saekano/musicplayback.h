#pragma once

#include "commands.h"
#include "profilevoiceoutput.h"
#include "ordinaryvoice.h"

namespace Impacto::Saekano {
class SystemSoundAssets;
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
  bool SoundPlaying(size_t logicalChannel) const;
  // Native UI channel6 is independent of SE2..5 and profile voice7.
  // Caller supplies a known effective gain and preserves the asset lifetime.
  void BindUiSounds(SystemSoundAssets& resources, float gain);
  bool UiSoundPlaying() const;
  // Native profile channel7, mapped onto an existing Impacto voice channel.
  // Caller supplies known effective gain. Preparation parses/owns HCA bytes;
  // no playback or successful missing-channel stop is fabricated.
  void PrepareProfileVoice(Bytes encoded, float gain);
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

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// On-demand voice.awb preparation on the same two channels as profile voices.
// Text/actor actions and the frontend's update clock remain with their owner.
class PlaybackOrdinaryVoice final : public OrdinaryVoicePreparation {
 public:
  PlaybackOrdinaryVoice(MusicPlayback& playback,
                        const std::filesystem::path& usrdir,
                        uint8_t volume, float coefficient);
  void Prepare(uint32_t channel, int16_t voiceId) override;
  void Start(uint32_t channel) override;
  void Stop(uint32_t channel) override;
  bool Ready(uint32_t channel) const;
  bool Playing(uint32_t channel) const;
  int32_t Position(uint32_t channel) const;
 private:
  MusicPlayback& playback_;
  std::shared_ptr<AwbBank> bank_;
  float gain_;
};

// Uses the same device/channel as BGM/SE and the existing PC HCA stream.
// Settings/coefficient must be known current state. The frontend pumps audio;
// Update intentionally does not substitute the profile clock for device state.
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
int RunProfileVoiceSession(const std::filesystem::path& usrdir,
                           uint32_t character, uint32_t entry,
                           const std::filesystem::path& captureDirectory = {});
int RunOrdinaryVoiceSession(const std::filesystem::path& usrdir,
                            const std::filesystem::path& captureDirectory);
int RunUiSoundSession(const std::filesystem::path& usrdir,
                      const std::filesystem::path& captureDirectory);
int RunProfileServiceSession(
    const std::filesystem::path& usrdir, uint32_t scene, uint32_t ordinal,
    uint8_t volume, const std::filesystem::path& captureDirectory = {});
}  // namespace Impacto::Saekano
