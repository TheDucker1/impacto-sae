#include "audio.h"

#include <clHCA.h>

#include <algorithm>
#include <array>
#include <limits>

namespace Impacto::Saekano {
namespace {
uint32_t Little(std::span<const uint8_t> bytes, size_t at, size_t width) {
  if (at > bytes.size() || width > bytes.size() - at)
    throw FormatError("truncated AWB index");
  uint32_t value = 0;
  for (size_t n = 0; n < width; ++n)
    value |= uint32_t(bytes[at + n]) << (8 * n);
  return value;
}
uint64_t Align(uint64_t value, uint16_t alignment) {
  const auto remainder = value % alignment;
  return value + (remainder ? alignment - remainder : 0);
}
void ReadFile(std::ifstream& file, std::span<uint8_t> output) {
  if (output.size() > size_t(std::numeric_limits<std::streamsize>::max()))
    throw FormatError("AWB read is too large");
  if (output.empty()) return;
  file.read(reinterpret_cast<char*>(output.data()),
            static_cast<std::streamsize>(output.size()));
  if (!file) throw FormatError("cannot read AWB data");
}
}  // namespace

size_t AwbIndex::IndexSize(std::span<const uint8_t> header, uint64_t fileSize) {
  if (header.size() < 16) throw FormatError("truncated AWB header");
  if (!std::equal(header.begin(), header.begin() + 4, "AFS2"))
    throw FormatError("invalid AWB signature");
  if (header[4] != 1) throw FormatError("unsupported AWB version");
  const auto offsets = header[5];
  const auto ids = Little(header, 6, 2);
  if ((offsets != 2 && offsets != 4) || (ids != 2 && ids != 4))
    throw FormatError("unsupported AWB table width");
  if (!Little(header, 12, 2)) throw FormatError("zero AWB alignment");
  const uint64_t count = Little(header, 8, 4);
  // Resource budget, not a claimed format limit. Validate before allocating.
  if (count > 1000000) throw FormatError("AWB entry budget exceeded");
  const uint64_t size = 16 + count * ids + (count + 1) * offsets;
  if (size > fileSize) throw FormatError("AWB index exceeds file");
  return static_cast<size_t>(size);
}

AwbIndex::AwbIndex(std::span<const uint8_t> index, uint64_t fileSize) {
  const auto size = IndexSize(index, fileSize);
  if (index.size() < size) throw FormatError("truncated AWB index");
  alignment_ = static_cast<uint16_t>(Little(index, 12, 2));
  subkey_ = static_cast<uint16_t>(Little(index, 14, 2));
  const auto count = Little(index, 8, 4);
  const auto ids = Little(index, 6, 2);
  const auto width = index[5];
  const size_t table = 16 + size_t(count) * ids;
  uint64_t raw = Little(index, table, width);
  if (raw < size || raw > fileSize)
    throw FormatError("AWB payload overlaps index or exceeds file");
  entries_.reserve(count);
  for (uint32_t n = 0; n < count; ++n) {
    const auto id = Little(index, 16 + size_t(n) * ids, ids);
    if (!byId_.emplace(id, entries_.size()).second)
      throw FormatError("duplicate AWB wave ID");
    const uint64_t end = Little(index, table + size_t(n + 1) * width, width);
    const uint64_t start = Align(raw, alignment_);
    if (end < raw || end > fileSize || start > end)
      throw FormatError("invalid AWB wave extent");
    entries_.push_back({id, start, end - start});
    raw = end;
  }
  if (raw != fileSize)
    throw FormatError("AWB terminal offset differs from file");
}

const AwbEntry& AwbIndex::Find(uint32_t id) const {
  const auto found = byId_.find(id);
  if (found == byId_.end()) throw FormatError("unknown AWB wave ID");
  return entries_[found->second];
}

AwbBank::AwbBank(const std::filesystem::path& path)
    : file_(path, std::ios::binary | std::ios::ate) {
  if (!file_) throw FormatError("cannot open AWB bank");
  const auto end = file_.tellg();
  if (end < 0) throw FormatError("cannot determine AWB size");
  const auto size = static_cast<uint64_t>(end);
  file_.seekg(0);
  std::array<uint8_t, 16> header;
  ReadFile(file_, header);
  Bytes index(AwbIndex::IndexSize(header, size));
  std::copy(header.begin(), header.end(), index.begin());
  ReadFile(file_, std::span(index).subspan(16));
  index_ = std::make_unique<AwbIndex>(index, size);
}

void AwbBank::Read(uint32_t id, uint64_t offset, std::span<uint8_t> output) {
  const auto& entry = index_->Find(id);
  if (offset > entry.Size || output.size() > entry.Size - offset)
    throw FormatError("read exceeds AWB wave");
  file_.clear();
  file_.seekg(static_cast<std::streamoff>(entry.Offset + offset));
  if (!file_) throw FormatError("cannot seek AWB wave");
  ReadFile(file_, output);
}

struct HcaWaveReader::Impl {
  std::shared_ptr<AwbBank> Bank;
  uint32_t Id;
  std::unique_ptr<clHCA, decltype(&clHCA_delete)> Decoder{clHCA_new(),
                                                          clHCA_delete};
  HcaWaveInfo Info;
  Bytes Encoded;
  std::vector<int16_t> Decoded;
  uint32_t SamplesPerBlock = 0;
  uint32_t NextBlock = 0;
  size_t BufferPosition = 0;
  uint64_t Position = 0;
  bool Failed = false;

  Impl(std::shared_ptr<AwbBank> bank, uint32_t id, std::optional<uint64_t> key)
      : Bank(std::move(bank)), Id(id) {
    if (!Bank) throw FormatError("missing AWB bank");
    if (!Decoder) throw std::bad_alloc();
    std::array<uint8_t, 8> prefix;
    Bank->Read(Id, 0, prefix);
    const int headerSize = clHCA_isOurFile(prefix.data(), 8);
    if (headerSize < 8) throw FormatError("AWB wave is not supported HCA");
    Bytes header(static_cast<size_t>(headerSize));
    Bank->Read(Id, 0, header);
    if (clHCA_DecodeHeader(Decoder.get(), header.data(), headerSize) < 0)
      throw FormatError("invalid HCA header");
    clHCA_stInfo source{};
    if (clHCA_getInfo(Decoder.get(), &source) < 0)
      throw FormatError("cannot inspect HCA header");
    if ((source.channelCount != 1 && source.channelCount != 2) ||
        !source.samplingRate || source.samplesPerBlock != 1024 ||
        source.blockSize < 2)
      throw FormatError("unsupported HCA stream layout");
    Info.SampleRate = source.samplingRate;
    Info.Channels = source.channelCount;
    Info.HeaderSize = source.headerSize;
    Info.BlockSize = source.blockSize;
    Info.BlockCount = source.blockCount;
    Info.EncoderDelay = source.encoderDelay;
    Info.EncoderPadding = source.encoderPadding;
    const uint64_t total = uint64_t(source.blockCount) * source.samplesPerBlock;
    if (uint64_t(source.encoderDelay) + source.encoderPadding > total)
      throw FormatError("HCA trimming exceeds duration");
    const uint64_t encodedSize = uint64_t(source.headerSize) +
                                 uint64_t(source.blockCount) * source.blockSize;
    if (encodedSize != Bank->Index().Find(Id).Size)
      throw FormatError("HCA extent differs from AWB wave");
    Info.Frames = total - source.encoderDelay - source.encoderPadding;
    Info.Loop = source.loopEnabled != 0;
    if (Info.Loop) {
      const uint64_t start =
          uint64_t(source.loopStartBlock) * source.samplesPerBlock +
          source.loopStartDelay;
      const uint64_t end =
          (uint64_t(source.loopEndBlock) + 1) * source.samplesPerBlock;
      if (source.loopEndPadding > end || start < source.encoderDelay ||
          end - source.loopEndPadding < source.encoderDelay)
        throw FormatError("invalid HCA loop trimming");
      Info.LoopStart = start - source.encoderDelay;
      Info.LoopEnd = end - source.loopEndPadding - source.encoderDelay;
      if (Info.LoopStart >= Info.LoopEnd || Info.LoopEnd > Info.Frames)
        throw FormatError("HCA loop exceeds duration");
    }
    Info.Encrypted = source.encryptionEnabled != 0;
    if (Info.Encrypted && !key)
      throw FormatError("HCA encryption key is required");
    if (Info.Encrypted && Bank->Index().Subkey())
      throw FormatError("AWB HCA subkey derivation is not implemented");
    if (key) clHCA_SetKey(Decoder.get(), *key);
    SamplesPerBlock = source.samplesPerBlock;
    Encoded.resize(source.blockSize);
    Decoded.resize(size_t(SamplesPerBlock) * Info.Channels);
    Reset();
  }
  void Reset() {
    clHCA_DecodeReset(Decoder.get());
    NextBlock = 0;
    Position = 0;
    BufferPosition = Decoded.size();
    Failed = false;
    uint64_t skip = Info.EncoderDelay;
    while (skip) {
      Decode();
      const auto frames = std::min<uint64_t>(skip, SamplesPerBlock);
      BufferPosition = size_t(frames) * Info.Channels;
      skip -= frames;
    }
  }
  void Decode() {
    if (Failed) throw FormatError("HCA decoder requires reset after failure");
    if (NextBlock >= Info.BlockCount)
      throw FormatError("HCA block exceeds stream");
    try {
      Bank->Read(
          Id, uint64_t(Info.HeaderSize) + uint64_t(NextBlock) * Info.BlockSize,
          Encoded);
      if (clHCA_DecodeBlock(Decoder.get(), Encoded.data(), Info.BlockSize) < 0)
        throw FormatError("invalid HCA block " + std::to_string(NextBlock));
      clHCA_ReadSamples16(Decoder.get(), Decoded.data());
      ++NextBlock;
      BufferPosition = 0;
    } catch (...) {
      Failed = true;
      throw;
    }
  }
  size_t Read(std::span<int16_t> output) {
    if (output.size() % Info.Channels)
      throw FormatError("PCM output must contain complete frames");
    if (Failed) throw FormatError("HCA decoder requires reset after failure");
    const auto frames = static_cast<size_t>(std::min<uint64_t>(
        output.size() / Info.Channels, Info.Frames - Position));
    size_t copied = 0;
    while (copied < frames * Info.Channels) {
      if (BufferPosition == Decoded.size()) Decode();
      const auto take = std::min(Decoded.size() - BufferPosition,
                                 frames * Info.Channels - copied);
      std::copy_n(Decoded.begin() + BufferPosition, take,
                  output.begin() + copied);
      BufferPosition += take;
      copied += take;
    }
    Position += frames;
    return frames;
  }
};

HcaWaveReader::HcaWaveReader(std::shared_ptr<AwbBank> bank, uint32_t id,
                             std::optional<uint64_t> key)
    : impl_(std::make_unique<Impl>(std::move(bank), id, key)) {}
HcaWaveReader::~HcaWaveReader() = default;
const HcaWaveInfo& HcaWaveReader::Info() const { return impl_->Info; }
uint64_t HcaWaveReader::Position() const { return impl_->Position; }
size_t HcaWaveReader::Read(std::span<int16_t> output) {
  return impl_->Read(output);
}
void HcaWaveReader::Seek(uint64_t frame) {
  if (frame > impl_->Info.Frames)
    throw FormatError("HCA seek exceeds duration");
  impl_->Reset();
  std::vector<int16_t> scratch(size_t(impl_->SamplesPerBlock) *
                               impl_->Info.Channels);
  while (impl_->Position < frame) {
    const auto take =
        std::min<uint64_t>(impl_->SamplesPerBlock, frame - impl_->Position);
    impl_->Read(std::span(scratch).first(size_t(take) * impl_->Info.Channels));
  }
}

}  // namespace Impacto::Saekano
