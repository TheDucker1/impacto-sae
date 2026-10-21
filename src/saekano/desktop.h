#pragma once

#include <filesystem>

namespace Impacto::Saekano {
// Diagnostic entry point for the loader/SDK rendering path intended for the
// game backend. Capture mode is hidden and terminates after all models.
int RunModelViewer(const std::filesystem::path& resourceDat,
                   const std::filesystem::path& captureDirectory = {});
}  // namespace Impacto::Saekano
