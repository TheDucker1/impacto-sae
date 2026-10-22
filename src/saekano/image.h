#pragma once

#include "formats.h"

#include <array>
#include <map>
#include <memory>
#include <optional>

namespace Impacto::Saekano {
class GameResources;

// Native root SeekRecord indices, not names sorted by the host filesystem.
enum class ImageSection : uint32_t {
  Background = 5,
  Event = 9,
  Free = 10,
  Rule = 11
};
struct ImageKey {
  ImageSection Section;
  uint32_t Id;
  auto operator<=>(const ImageKey&) const = default;
};
struct ImagePixels {
  uint32_t Width = 0, Height = 0;
  Bytes Rgba;
};
// Measured rule/background/CG subset: one linear P8 image and one mip level.
// Other GXT layouts fail here; the full Impacto loader handles model textures.
ImagePixels DecodeSceneImage(std::span<const uint8_t> bytes);

enum class ImageSlot : size_t { Rule, Background, Count };
class SceneImageDriver {
 public:
  virtual ~SceneImageDriver() = default;
  virtual void Prepare(ImageSlot slot, ImageKey key) = 0;
  virtual bool Ready() const = 0;
  virtual void Release(ImageSlot slot) = 0;
  // Per-category resource records are separate from the rule-wipe bindings.
  virtual size_t AssetCount(ImageSection section) const;
  virtual void RequestAsset(ImageKey key);
  virtual bool AssetReady(ImageKey key) const;
  virtual void ReleaseAsset(ImageKey key);
};

// Real original-archive read, gzip verification and bounded palette expansion.
// A renderer can upload these same pixels; headless performs no GPU work.
class ArchiveSceneImages : public SceneImageDriver {
 public:
  explicit ArchiveSceneImages(GameResources& resources)
      : resources_(resources) {}
  void Prepare(ImageSlot slot, ImageKey key) override;
  bool Ready() const override {
    return true;
  }  // Synchronous read/create completed.
  void Release(ImageSlot slot) override;
  const ImagePixels* Pixels(ImageSlot slot) const;
  std::optional<ImageKey> Key(ImageSlot slot) const;
  size_t AssetCount(ImageSection section) const override;
  void RequestAsset(ImageKey key) override;
  bool AssetReady(ImageKey key) const override;
  void ReleaseAsset(ImageKey key) override;
  const ImagePixels* AssetPixels(ImageKey key) const;

 private:
  GameResources& resources_;
  std::array<std::shared_ptr<const ImagePixels>, size_t(ImageSlot::Count)>
      slots_;
  std::array<std::optional<ImageKey>, size_t(ImageSlot::Count)> keys_;
  std::map<ImageKey, std::weak_ptr<const ImagePixels>> cache_;
  std::map<ImageKey, std::shared_ptr<const ImagePixels>> assets_;
};
}  // namespace Impacto::Saekano
