# HackArena3 C++ API Wrapper

This repository is a faithful C++20 port of the Python `hackarena3` wrapper from `C:\Users\Bombini\PycharmProjects\HackArena3.0-ApiWrapper-Python`. It keeps the same split between reusable wrapper/runtime logic and bot-specific entry code, uses the same proto definitions for gRPC/protobuf integration, and preserves the workflow of “write bot code, then run the bot entrypoint”.

Release model:

- the reusable wrapper/library is released as a self-contained C++ SDK package built from the install tree, analogous to publishing the Python package from `/srv/hackarena3/`
- the contents of `template/` are released separately as a zip asset, analogous to the Python template release asset
- typed race snapshots expose `RaceSnapshot.car.last_pit_lap` and `RaceSnapshot.car.command_cooldowns`

## Clone and Setup

If you are building the wrapper from source, clone the repository and initialize submodules:

```bash
git clone https://github.com/INIT-SGGW/HackArena3.0-ApiWrapper-Cpp.git
cd HackArena3.0-ApiWrapper-Cpp
git submodule update --init --recursive
```

## Project Structure

- `include/hackarena3/`: public wrapper API, domain types, config loading, and `run_bot(...)`.
- `src/`: wrapper implementation for config, auth, game-token issuance, sandbox discovery, race APIs, proto-to-domain conversion, and participant runtime loop.
- `template/user/src/bot/main.cpp`: example/template bot executable source.
- `template/system/manifest.toml`: C++ equivalent manifest describing the bot entry concept.
- `third_party/HackArena3.0-Proto/`: Git submodule pointing at `https://github.com/INIT-SGGW/HackArena3.0-Proto.git`.
- `third_party/HackArena-Proto/`: Git submodule pointing at `https://github.com/INIT-SGGW/HackArena-Proto.git`.
- `cmake/GenerateGrpcSources.cmake`: reproducible protobuf/gRPC code generation wiring.

## Prerequisites

There are two different workflows:

- build the wrapper from source
- consume the released SDK tar.gz archive

For building the wrapper from source:

- primary dependency path: `vcpkg`
- required packages: `grpc`, `protobuf`, `nlohmann-json`
- initialize Git submodules:
  - `git submodule update --init --recursive`
- expected tooling:
  - Windows: Visual Studio 2022 or newer with MSVC and CMake
  - Linux: GCC 12+ or Clang 15+ with CMake
  - macOS: AppleClang with CMake
  - `vcpkg` checked out locally

For consuming the released Windows SDK tar.gz archive:

- `vcpkg` is not required
- the SDK tar.gz archive already bundles the required third-party headers, libraries, tools, and CMake package configs
- use Visual Studio/MSVC on Windows

This project is intentionally documented for MSVC on Windows. MinGW, Conda-discovered C++ toolchains, and Conda-managed C++ dependencies are not the primary path.

For CLion on Windows:

- use the `Visual Studio` toolchain, not `MinGW`
- set the Visual Studio toolchain architecture to `amd64`
- pair that toolchain with the `x64-windows` vcpkg triplet

## vcpkg Setup

This section applies to building the wrapper from source, not to consuming the released Windows SDK tar.gz archive.

Install dependencies with the triplet matching your machine:

Windows x64:

```powershell
vcpkg install grpc protobuf nlohmann-json:x64-windows
```

Linux x64:

```bash
vcpkg install grpc protobuf nlohmann-json:x64-linux
```

macOS Intel:

```bash
vcpkg install grpc protobuf nlohmann-json:x64-osx
```

macOS Apple Silicon:

```bash
vcpkg install grpc protobuf nlohmann-json:arm64-osx
```

## Build From Source

### Windows

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Release
```

CLion-specific Windows note:

- `Settings > Build, Execution, Deployment > Toolchains`
- Toolchain: `Visual Studio`
- Architecture: `amd64`

If CLion is left on the bundled `MinGW` toolchain while using `x64-windows` vcpkg packages, configuration or linking will fail because that is a GNU/MSVC mismatch.

## Packaging and Release Assets

The C++ equivalent of the Python wheel in this repository is a self-contained SDK tar.gz archive containing:

- exported CMake package files
- public headers
- generated protobuf/gRPC headers required by the public API
- `hackarena3` static library
- `hackarena3_proto` static library
- a vendored dependency prefix with the required gRPC/protobuf/JSON libraries, headers, tools, and CMake package configs

Create a local install tree:

```powershell
cmake --install build --config Release --prefix .\install
```

Create the SDK release package with the explicit SDK target:

Windows:

```powershell
cmake --build build --target hackarena3_sdk_tgz --config Release
```

Linux/macOS:

```bash
cmake --build build --target hackarena3_sdk_tgz
```

This produces a package named like:

- Windows: `hackarena3-cpp-sdk-0.1.0b9-Windows-AMD64.tar.gz`
- Linux: `hackarena3-cpp-sdk-0.1.0b9-Linux-x86_64.tar.gz`
- macOS: `hackarena3-cpp-sdk-0.1.0b9-Darwin-x86_64.tar.gz`

Create the template zip asset:

```powershell
cmake --build build --target hackarena3_template_zip --config Release
```

The output is written to:

- `build/release-assets/hackarena3-cpp-template-0.1.0b9.zip`

Recommended GitHub Release layout:

- one SDK package asset produced by `hackarena3_sdk_tgz`
- optionally one template asset produced by `hackarena3_template_zip`

## GitHub Actions Release Build

The repository includes a cross-platform GitHub Actions workflow at `.github/workflows/release-sdk.yml`.

It builds only the SDK package target:

- `hackarena3_sdk_tgz`

Target platforms:

- Windows: `windows-latest` with MSVC and `x64-windows`
- Linux: `ubuntu-latest` with `x64-linux`
- macOS: `macos-15-intel` with `x64-osx`

Trigger behavior:

- `workflow_dispatch`: builds the SDK tar.gz archives and uploads them as workflow artifacts
- tag push matching `v*`: builds the SDK tar.gz archives and uploads them to the GitHub Release for that tag

Workflow requirement for private proto submodules:

- if `third_party/HackArena3.0-Proto` and `third_party/HackArena-Proto` are private, add a repository or organization secret named `SUBMODULES_TOKEN`
- `SUBMODULES_TOKEN` must have read access to:
  - `INIT-SGGW/HackArena3.0-ApiWrapper-Cpp`
  - `INIT-SGGW/HackArena3.0-Proto`
  - `INIT-SGGW/HackArena-Proto`
- if the proto repositories are public, the workflow falls back to `github.token`

Consumer workflow from release assets only:

1. extract the SDK tar.gz archive
2. extract the template zip
3. configure the template with `-DHACKARENA3_SDK_ROOT=<extracted-sdk-root>`
4. build the template project

For the verified Windows/MSVC SDK tar.gz consumer flow, no external `vcpkg` installation is required.

Example consumer configure command from release archives only on Windows:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DHACKARENA3_SDK_ROOT=C:\path\to\hackarena3-cpp-sdk-0.1.0b9-Windows-AMD64
```

### Linux

```bash
cmake -S . -B build-linux -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build-linux -j
cmake --build build-linux --target hackarena3_sdk_tgz
```

### macOS

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

## Protobuf and gRPC Generation

The build generates C++ protobuf and gRPC sources at build time into `build/generated/`.

Generation inputs:

- `third_party/HackArena3.0-Proto/proto/**/*.proto`
- `third_party/HackArena-Proto/proto/**/*.proto`

Generation is handled by `protobuf::protoc` and `gRPC::grpc_cpp_plugin` discovered through `vcpkg` packages referenced by `find_package(Protobuf CONFIG REQUIRED)` and `find_package(gRPC CONFIG REQUIRED)`.

## Environment and Auth

Required environment variable:

- `HA3_WRAPPER_API_URL`: HTTPS API base URL used for broker discovery and game-token issuance.

Optional environment variable:

- `HA3_WRAPPER_HA_AUTH_BIN`: explicit path to `ha-auth` if it is not discoverable on `PATH` or in the expected install locations.
- `HA_AUTH_PROFILE`: optional `ha-auth` profile selector. The wrapper does not parse this itself; it is passed through to the `ha-auth` process via the normal environment, matching the Python workflow.

Official mode environment variables:

- `HA3_WRAPPER_BACKEND_ENDPOINT`: HTTPS backend endpoint with a non-root path prefix, for example `https://runtime.example.com/backend`.
- `HA3_WRAPPER_TEAM_TOKEN`: official race team token sent as `x-ha3-game-token`.
- `HA3_WRAPPER_AUTH_TOKEN`: member auth token sent as `cookie: auth_token=...`.

Local `.env` support matches the Python wrapper:

- If `user/.env` exists under the current working directory, the wrapper loads it first.
- Existing process environment variables win over `.env` values.

Auth flow matches Python:

1. Resolve `ha-auth`.
2. Run `ha-auth token -q`.
3. Parse the returned JSON and extract `token`.
4. Use the member JWT against `auth.v1.GameTokenIssuerService/IssueGameToken`.
5. Use the resulting game token plus member auth metadata for race RPCs.

Expected API/profile behavior:

- `HA3_WRAPPER_API_URL` must be `https://...`.
- Broker and game-token RPCs go to the secure central API target derived from that URL.
- Sandbox backend endpoints returned by broker discovery are used as plain gRPC targets exactly like the Python wrapper.

## Running the Bot

The Python template entrypoint is:

```bash
python -m bot
```

The C++ equivalent is the built `bot` executable.

Run from the repository root so `user/.env` is discovered correctly.

For CLion run/debug configurations on Windows:

- keep the toolchain on `Visual Studio` with Architecture `amd64`
- set the working directory to the repository root if you want `user/.env` loading to behave like the Python template
- if you only set environment variables directly in the run configuration, running from `build/bin` is still fine

Windows Release:

```powershell
.\build\bin\Release\bot.exe
```

Windows Release with explicit sandbox:

```powershell
.\build\bin\Release\bot.exe --sandbox_id <sandbox-id>
```

Windows official mode:

```powershell
.\build\bin\Release\bot.exe --official
```

Linux Release:

```bash
./build/bin/bot
```

Linux Release with explicit sandbox:

```bash
./build/bin/bot --sandbox_id <sandbox-id>
```

Linux official mode:

```bash
./build/bin/bot --official
```

macOS Release:

```bash
./build/bin/bot
```

macOS Release with explicit sandbox:

```bash
./build/bin/bot --sandbox_id <sandbox-id>
```

macOS official mode:

```bash
./build/bin/bot --official
```

If `--sandbox_id` is omitted:

- interactive terminals prompt for sandbox selection
- non-interactive runs fail and list available sandbox IDs
- `--official` cannot be combined with `--sandbox_id`
- in official mode, the wrapper calls `PrepareOfficialJoin`, preloads `TrackData`, opens the prefixed participant stream, and fails fast on `map_id` mismatch

## Python to C++ Mapping

- `src/hackarena3/__init__.py` -> `include/hackarena3/hackarena3.hpp`
- `src/hackarena3/client.py` -> `src/client.cpp`
- `src/hackarena3/config.py` -> `src/config.cpp`
- `src/hackarena3/auth.py` -> `src/auth.cpp`
- `src/hackarena3/game_token.py` -> `src/game_token.cpp`
- `src/hackarena3/runtime_common.py` -> `src/runtime_common.cpp` and `src/detail/constants.hpp`
- `src/hackarena3/runtime_discovery.py` -> `src/runtime_discovery.cpp`
- `src/hackarena3/runtime_race.py` -> `src/runtime_race.cpp`
- `src/hackarena3/runtime_convert.py` -> `src/runtime_convert.cpp`
- `src/hackarena3/runtime_loop.py` -> `src/runtime_loop.cpp`
- `src/hackarena3/runtime.py` -> `src/runtime.cpp`
- `src/hackarena3/types.py` -> `include/hackarena3/types.hpp` and `src/types.cpp`
- `template/system/manifest.toml` -> `template/system/manifest.toml`
- `template/user/src/bot/__main__.py` -> `template/user/src/bot/main.cpp`

## Known Gaps and Differences

- The wrapper is a CMake project, not an installable Python package, so version reporting is compile-time instead of package-metadata driven.
- The release equivalent of the Python wheel is a self-contained C++ SDK package for CMake consumers, not a language-specific package-manager artifact.
- The Python wrapper catches `KeyboardInterrupt` and returns `130`; the C++ port currently relies on normal process signal behavior for `Ctrl+C`.
- The wrapper keeps parity with the Python `BotContext` data model (`car_id`, `map_id`, `car_dimensions`, `requested_hz`, `track`, `effective_hz`, `tick`).
