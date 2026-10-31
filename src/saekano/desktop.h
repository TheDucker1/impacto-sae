#pragma once

#include <filesystem>
#include <cstdint>
#include <optional>

namespace Impacto::Saekano {
int RunMovieInfoProbe(const std::filesystem::path& path,const std::filesystem::path& output);
int RunMovieDecodeProbe(const std::filesystem::path& path,const std::filesystem::path& output,uint32_t videoFrames,uint32_t audioFrames);
int RunTitleScene(const std::filesystem::path& usrdir,
                  const std::filesystem::path& progressRoot={},
                  const std::filesystem::path& captureDirectory={},bool capturePointer=false,bool captureOpening=false);
int RunTitleDrawProbe(const std::filesystem::path& usrdir,const std::filesystem::path& inputs,
                      const std::filesystem::path& output);
int RunParticleGroupProbe(const std::filesystem::path& usrdir,
                          const std::filesystem::path& fixtureDirectory,
                          const std::filesystem::path& output);
int RunParticleRasterProbe(const std::filesystem::path& fixtureDirectory,
                           const std::filesystem::path& output);
int RunParticleFragmentProbe(const std::filesystem::path& fixtureDirectory,
                             const std::filesystem::path& output);
int RunParticleProjectionProbe(const std::filesystem::path& fixtureDirectory,
                               const std::filesystem::path& output);
int RunResultRenderStateProbe(const std::filesystem::path& output);
int RunScreenFilterProbe(const std::filesystem::path& fixtureDirectory,
                         const std::filesystem::path& output);
int RunSceneSdkUiProbe(const std::filesystem::path& usrdir,
                       const std::filesystem::path& captureDirectory);
int RunFirstSceneViewer(const std::filesystem::path& usrdir,
                        const std::filesystem::path& captureDirectory = {},
                        bool captureAuto = false, bool captureButtonAuto = false,
                        bool captureVoiced = false,
                        std::optional<size_t> captureUntilMessage = {},
                        std::optional<size_t> captureFromCommand = {},
                        std::optional<int32_t> captureScriptOverride = {},
                        bool captureWaitingInput = false,
                        bool captureConfirmInput = false,
                        int32_t captureMenuInput = 0,
                        const std::filesystem::path& capturePlan = {},
                        const std::filesystem::path& systemSave = {},
                        const std::filesystem::path& progressRoot = {});
int RunFontSample(const std::filesystem::path& usrdir,
                   const std::filesystem::path& captureDirectory = {});
// Diagnostic entry point for the loader/SDK rendering path intended for the
// game backend. Capture mode is hidden and terminates after all models.
int RunModelViewer(const std::filesystem::path& resourceDat,
                   const std::filesystem::path& captureDirectory = {});
int RunModelScenes(const std::filesystem::path& resourceDat,
                   const std::filesystem::path& environmentDat,
                   const std::filesystem::path& captureDirectory = {});
int RunProfileBackdropViewer(const std::filesystem::path& resourceDat,
                             const std::filesystem::path& environmentDat,
                             const std::filesystem::path& captureDirectory = {},
                             bool menuChrome = false, bool itemFrames = false,
                             std::optional<size_t> itemScene = {},
                             bool captureNavigation = false,
                             bool captureDialog = false,
                             bool captureFlow = false);
int RunProfileItemsViewer(const std::filesystem::path& usrdir, size_t scene,
                          const std::filesystem::path& captureDirectory = {},
                          bool captureNavigation = false,
                          bool captureDialog = false, bool captureFlow = false);
}  // namespace Impacto::Saekano
