class PcMovieFrameLayer {
 public:
  std::unique_ptr<YUVFrame> Planes;
  bool Available = false;
  int64_t Pts = -1;
  uint64_t Uploads = 0;
  ~PcMovieFrameLayer() { Retire(); }
  void Present(const AVFrame* frame, int64_t pts) {
    if (!frame || frame->format != AV_PIX_FMT_YUV420P || frame->width <= 0 ||
        frame->height <= 0 || frame->width % 2 || frame->height % 2)
      throw FormatError("unsupported movie YUV frame layout");
    auto state = ViewerWindow::Gpu->SaveState();
    if (Available && (int(Planes->Width) != frame->width ||
                      int(Planes->Height) != frame->height))
      Retire();
    if (!Available) {
      Planes.reset(Impacto::Renderer->CreateYUVFrame(float(frame->width),
                                                     float(frame->height)));
      Available = true;
    }
    Planes->SubmitStrided(frame->data[0], frame->linesize[0], frame->data[1],
                          frame->linesize[1], frame->data[2],
                          frame->linesize[2]);
    Pts = pts;
    ++Uploads;
  }
  void Draw(std::array<int32_t, 4> destination = {0, 0, 960, 544},
            std::array<uint8_t, 4> color = {255, 255, 255, 255},
            bool preserveClip = false) {
    if (!Available) return;
    ViewerWindow::BeginFrame();
    auto state = ViewerWindow::Gpu->SaveState();
    if (!preserveClip) {
      ViewerWindow::Gpu->SetDepthTest(5, false);
      ViewerWindow::Gpu->SetStencil(0);
    }
    ViewerWindow::Gpu->SetBlend({true, 4, 5, 4, 5});
    Impacto::Renderer->InvalidateState();
    Impacto::Renderer->DrawVideoTexture(
        *Planes,
        RectF(float(destination[0]), float(destination[1]),
              float(destination[2]), float(destination[3])),
        glm::vec4(color[0], color[1], color[2], color[3]) / 255.f, false);
    Impacto::Renderer->FlushDraws();
  }
  void Retire() {
    if (Planes) {
      Planes->Release();
      Planes.reset();
    }
    Available = false;
    Pts = -1;
  }
};
