#pragma once

#include <filesystem>

namespace Impacto::Saekano {
// Diagnostic entry point for the loader/SDK rendering path intended for the
// game backend. Capture mode is hidden and terminates after all models.
int RunModelViewer(const std::filesystem::path& resourceDat,
                   const std::filesystem::path& captureDirectory = {});
int RunModelScenes(const std::filesystem::path& resourceDat,
                   const std::filesystem::path& environmentDat,
                   const std::filesystem::path& captureDirectory = {});
int RunProfileBackdropViewer(
    const std::filesystem::path& resourceDat,
    const std::filesystem::path& environmentDat,
    const std::filesystem::path& captureDirectory = {},
    bool menuChrome = false, bool itemFrames = false);
}  // namespace Impacto::Saekano
