# impacto [![impacto](https://github.com/CommitteeOfZero/impacto/actions/workflows/impacto.yml/badge.svg)](https://github.com/CommitteeOfZero/impacto/actions/workflows/impacto.yml)

**impacto** is an open-source **reimplementation** of the **"MAGES." visual novel engine** in C++ and OpenGL. Using the original data files, impacto can run any supported game on any supported platform.

## Status

**impacto is in active development**. At current stage it should be possible to reach all endings in at least one supported game - "Memories Off 6 \~T-Wave\~." However, some functionality and architectural design are still missing and **no games are fully complete yet**. Refer to [#1](https://github.com/CommitteeOfZero/impacto/issues/1) for supported games and remaining work.

impacto is currently being developed for **64-bit Windows 10 and desktop Linux PCs** but we've successfully experimented on macOS, Android (ARM), Switch (homebrew and Linux) and HTML5/WebAssembly (via Emscripten) and full official Android support is planned. OpenGL is used for the graphics renderer with experimental Vulkan and DirectX 9 options available.

## Building

### Saekano / PCSG00543 port

The `saekano` branch contains an in-progress PS Vita command-engine backend.
Selected PC gameplay paths work; full-game playability is incomplete. Development
follows the [Saekano development manifesto](doc/saekano/DEVELOPMENT_MANIFESTO.md):
reuse existing Impacto services, verify modules first, then integrate representative
real-data cases. The implementation plan, measured native-reference
coverage and next work are in [doc/saekano/STATUS.md](doc/saekano/STATUS.md) and
[doc/saekano/PLAN.md](doc/saekano/PLAN.md).

PC ordinary dialogue controls: Enter/gamepad East confirms; Space/gamepad West reveals
unfinished text or toggles the completed dialogue window; Escape/gamepad Start
opens the scenario menu. [Visibility behavior and verification](doc/saekano/MESSAGE_VISIBILITY.md).

Build/package and normal launch instructions are in
[PC_PLAYABLE_BUILD.md](doc/saekano/PC_PLAYABLE_BUILD.md). The PC build script
defaults to the cached graphical/headless targets without running tests or
reinstalling dependencies. Packaged launch uses external original data and an
explicit writable PC save directory.

On the documented Windows Build Tools installation, build and test its standalone
C++ core with `./tools/saekano/build.ps1`. This does not require the graphical
application's dependencies. See [the VM bridge documentation](doc/saekano/VM_PROTOCOL.md)
for headless execution and the preserved native-reference comparison contract.

For building on Windows with Visual Studio 2019 or newer, please refer to the [building instructions](doc/windows_build.md).

For building on Linux, see the [instructions for Ubuntu Desktop](doc/ubuntu_build.md) and adapt to your distribution if necessary.

For building for the Nintendo Switch (Horizon / devkitPro), see the [Switch build instructions](doc/switch_build.md).

More platforms and toolchains are known to work.

## Contributing

**We are looking for contributors!** Check out the [Getting Started guide](doc/getting_started.md) for pointers on setting up games for testing, finding your way around the codebase and adding functionality. Also check out the [Contributor guide](doc/contributor_guide.md) for important information on the code style you should follow.

There is work to be done for C++ programmers of any skill level in a wide range of subjects, on game engine architecture (design and implementation), reverse-engineering the original (looking at just the game's outside behaviour as a *black box* or inspecting the internals through a *white box*), replicating it, improving on it with new functionality, documenting our efforts, fixing bugs and polish. 

If you're interested, [come join our Discord](https://discord.gg/rq4GGCh) to discuss ideas and help you get into it.

## Legal stuff

impacto source code as a whole is released under the liberal [ISC license](LICENSE). Some parts are based on or copied from third-party code under various licenses. Binary distributions or parts thereof may fall under more restrictive licensing terms. See [THIRDPARTY.md](THIRDPARTY.md) for details and attribution.

