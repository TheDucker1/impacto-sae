# Saekano PC backend

This backend uses separately supplied original game data. The repository does not
include game archives, fonts, movies, audio, saves, native snapshots or test fixtures.

## Build

The Windows backend targets x86 with OpenGL3.3 compatibility or DirectX9 and a
compatible legacy Cubism2 SDK for the selected API. Use Visual Studio2022 C++ Build Tools,
the Windows SDK, CMake/Ninja and the vcpkg dependencies declared by the project.
Supply the SDK separately through SAEKANO_CUBISM2_SDK_ROOT; respect its license.
The backend is enabled with IMPACTO_ENABLE_SAEKANO=ON and the x86-windows-saekano
triplet. See the main Impacto build documentation for general dependency setup.

The local Windows helper uses the existing dependency cache under impacto-build:

```powershell
./tools/saekano/build_pc.ps1
./tools/saekano/build_pc.ps1 -Renderer DirectX9
```

It builds the actual impacto product. The OpenGL executable is
impacto-build/SaekanoPC-x86-Release/impacto.exe; DirectX9 uses
impacto-build/SaekanoPC-DX9-x86-Release/impacto.exe. The script does not reinstall
cached dependencies unless requested with -AllowDownloads during configuration.
It expects Visual Studio2022 Build Tools in its standard installation folder.

The build selects `SAEKANO_CUBISM2_BACKEND=OpenGL` or `DirectX9`, with the matching
SDK in `SAEKANO_CUBISM2_SDK_ROOT`. The helper accepts `-SdkRoot` and `-D3dxRoot`
when configuring. DirectX9 needs the legacy `live2d_directX_md.lib`, compatible
framework sources and Microsoft.DXSDK.D3DX9.29.952.8 (`build/native`). Its older
SDK is isolated in `impacto_cubism2_d3d9.dll`, with the x86 Visual C++2012 runtime
selected by `SAEKANO_CUBISM2_LEGACY_CRT_DLL`. The helper's default dependency
directories are under `impacto-build/deps`. Supply these SDK dependencies separately.
The selected Saekano SDK determines its rendering API; changing that API requires
the corresponding build. The project's Vulkan backend remains present; a Saekano
Vulkan scene pass is not implemented.

To create a distribution from a prepared build:

```powershell
./tools/saekano/package_pc.ps1 -OutputDirectory 'C:/Builds/Saekano PC' -SkipBuild
./tools/saekano/package_pc.ps1 -Renderer DirectX9 -OutputDirectory 'C:/Builds/Saekano DirectX9' -SkipBuild
```

The package contains the executable, runtime libraries, shaders, shared UI
resources, Lua profiles, base-path and game definitions, launcher SVG icons and
license notices. Original game data
and personal saves remain external.

## Launcher and configuration

```powershell
./impacto.exe -g saekano
./impacto.exe -g saekano -uc 'C:/PC Settings/launcher.toml'
```

Select this launcher with -g saekano (or --game saekano). Running without a
selected game follows the original Impacto startup path. The packaged
Play-Saekano.cmd supplies the game-selection flag for convenience.
In a Saekano-enabled build, the shared game picker lists Saekano even before a
default data directory exists. Selecting it and choosing Start Game opens the
Saekano launcher; Save and Play enters the game after validation.
The game is registered in gamedefinitions.lua. Its Runtime selects the existing
Saekano implementation; profiles/saekano/game.lua defines the game and
profiles/saekano/files.lua supplies original archive, font and environment paths.
The shared -bp, -gc and -p options apply to profile loading. Environment files
continue to supply their authored UI and game settings through the existing
binary readers. Lua profiles do not replace the game's bytecode or logic.
The title portrait mapping is declared in `profiles/saekano/hud/titlemenu.lua`.
The GPDA archive reader lives in `src/io/gpdaarchive.*`; the existing Cubism 2.1
SDK adapter lives in `src/renderer/cubism2.*`. Shared window, texture and movie
services are used by both builds. Specialized scene shaders, render targets and
SDK state isolation belong to `src/renderer/opengl/` and `src/renderer/dx9/`, behind
the `Scene2D` interface; game owners retain draw ordering, timing and resource keys.
The existing script reader,
command decoding, control flow and execution loop live in `src/vm/saekano/`;
presentation and game-mode behavior live in `src/games/saekano/`.

Use Game Data to select USRDIR and
validate required files, configure display and input, then choose Save and Play.
Normal validation checks filenames/readability and permits modified contents.
Optional strict validation compares required files to compiled reference hashes.
It does not generate expected hashes from the selected directory.

Configuration is TOML in the per-user SDL preference directory, unless -uc selects
a file. Default saves are in a saves directory beside that configuration.
--usrdir preselects the launcher's editable data path; explicit Save/Play persists
it. --savedatapath selects a save directory for that run. Keep writable config and
saves outside original data. --help lists the shared Impacto command-line options.
The default gamedata/saekano directory is offered when it exists; a saved launcher
path or --usrdir takes precedence. Launcher configuration keeps its existing TOML
schema and per-user location, including when selected from the shared game picker.

## Input and presentation

The launcher's Controls page maps keyboard, mouse and SDL device inputs to Vita
actions, with named profiles and a visual button layout. Initial bindings are
arrows for D-pad, Z for Circle/confirm, X for Cross/back, A for Square, S for
Triangle, Q/E for L/R, Enter for Start, Tab for Select and Ctrl for hold-skip.
The game uses the same configured input router as the launcher.

Display settings select the initial resolution; SDL scales the rendered scene.
Skip logos skips only the startup logos and continues with the opening movie.
Dialogue uses authored read/unread colors and reveal behavior. Read dialogue keeps
its shadow; unread dialogue disables it, including ruby and keyword text.
Text preserves the original PVF cache-cell widths, cropping and half/full-width
draw scaling, including Latin letters. Font outlines use the shared FreeType
loader and a pinned legacy grayscale scan converter for stable coverage.
