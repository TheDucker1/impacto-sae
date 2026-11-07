class PcSceneThumbnail {
  std::array<std::unique_ptr<SceneRenderTarget>, 2> Frames;
  std::unique_ptr<SceneRenderTarget> Reduced;
  size_t Current = 0;
  bool Published = false;

 public:
  bool Ready() const { return Published; }
  void Present(std::array<int, 4> viewport) const {
    if (!Ready()) throw FormatError("scene modal has no completed background");
    ViewerWindow::BeginFrame();
    ViewerWindow::Gpu->PresentTarget(*Frames[Current], viewport);
  }
  void Publish(std::array<int, 4> viewport) {
    if (viewport[0] < 0 || viewport[1] < 0 || viewport[2] <= 0 ||
        viewport[3] <= 0)
      throw FormatError("invalid completed scene capture viewport");
    auto& next = Frames[1 - Current];
    if (!next || next->Texture.Width != uint32_t(viewport[2]) ||
        next->Texture.Height != uint32_t(viewport[3]))
      next = ViewerWindow::Gpu->CreateTarget(viewport[2], viewport[3]);
    ViewerWindow::Gpu->CopyTarget(*next, viewport);
    Current = 1 - Current;
    Published = true;
  }
  Bytes CaptureRgba() {
    if (!Ready())
      throw UnsupportedCommand(
          "scene thumbnail requires a completed scene frame");
    auto state = ViewerWindow::Gpu->SaveState();
    if (!Reduced) Reduced = ViewerWindow::Gpu->CreateTarget(192, 108);
    ViewerWindow::Gpu->SetTarget(*Reduced);
    ViewerWindow::Gpu->PresentTarget(*Frames[Current], {0, 0, 192, 108});
    return ViewerWindow::Gpu->ReadTarget(Reduced.get(), {0, 0, 192, 108});
  }
  SlotThumbnail Capture() { return PackSceneThumbnailRgba(CaptureRgba()); }
  void PublishTo(NativeSlotRecord& record) {
    record.Thumbnail = Capture();
    record.ThumbnailPresent = 1;
  }
};
#include "../../renderer/opengl/scenethumbnailreference.inc"
