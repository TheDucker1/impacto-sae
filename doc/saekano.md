# Saekano PC backend

This backend uses separately supplied original game data. The repository does not
include game archives, fonts, movies, audio, saves, native snapshots or test fixtures.

## Build

The current Windows backend targets x86 with OpenGL3.3 compatibility and a
compatible legacy Cubism2.1 OpenGL SDK. Use Visual Studio2022 C++ Build Tools,
the Windows SDK, CMake/Ninja and the vcpkg dependencies declared by the project.
Supply the SDK separately through SAEKANO_CUBISM2_SDK_ROOT; respect its license.
The backend is enabled with IMPACTO_ENABLE_SAEKANO=ON and the x86-windows-saekano
triplet. See the main Impacto build documentation for general dependency setup.

The local Windows helper uses the existing dependency cache under impacto-build:

```powershell
./tools/saekano/build_pc.ps1
```

It builds only impacto. The current executable is
impacto-build/SaekanoPC-x86-Release/impacto.exe. The script does not reinstall
cached dependencies unless requested with -AllowDownloads during configuration.
It expects Visual Studio2022 Build Tools in its standard installation folder.

To create a distribution from a prepared build:

```powershell
./tools/saekano/package_pc.ps1 -OutputDirectory 'C:/Builds/Saekano PC' -SkipBuild
```

The package contains the executable, runtime libraries, launcher SVG icons and
license notices. Original game data and personal saves remain external.

## Launcher and configuration

```powershell
./impacto.exe -g saekano
./impacto.exe -g saekano -uc 'C:/PC Settings/launcher.toml'
```

Select this launcher with -g saekano (or --game saekano). Running without a
selected game follows the original Impacto startup path. The packaged
Play-Saekano.cmd supplies the game-selection flag for convenience.
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

## Input and presentation

The launcher's Controls page maps keyboard, mouse and SDL device inputs to Vita
actions, with named profiles and a visual button layout. Initial bindings are
arrows for D-pad, Z for Circle/confirm, X for Cross/back, A for Square, S for
Triangle, Q/E for L/R, Enter for Start, Tab for Select and Ctrl for hold-skip.
The game uses the same configured input router as the launcher.

Display settings select the initial resolution; SDL scales the rendered scene.
Skip logos skips only the startup logos and continues with the opening movie.
Dialogue uses authored read/unread colors and existing shadow/reveal behavior.
The portable font rasterizer is not claimed pixel-identical to the original.
