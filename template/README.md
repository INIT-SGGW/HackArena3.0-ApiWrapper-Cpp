# HackArena3 C++ Template Bot

This template is intended to be used together with the released `hackarena3` C++ SDK zip.

Expected release assets:

- `hackarena3-cpp-sdk-<version>-<platform>.zip`
- `hackarena3-cpp-template-<version>.zip`

## Build From Release Zips

1. Extract the SDK zip somewhere, for example:

   `C:\sdk\hackarena3-cpp-sdk-0.1.0b4-Windows-AMD64`

2. Extract this template zip somewhere, for example:

   `C:\work\hackarena3-cpp-template`

3. Configure the template:

   ```powershell
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DHACKARENA3_SDK_ROOT=C:\sdk\hackarena3-cpp-sdk-0.1.0b4-Windows-AMD64
   ```

4. Build:

   ```powershell
   cmake --build build --config Release
   ```

The template uses:

```cmake
find_package(hackarena3 CONFIG REQUIRED)
target_link_libraries(bot PRIVATE hackarena3::hackarena3)
```

If the SDK package exposes `hackarena3_copy_runtime_dlls`, the template uses it automatically on Windows so the resulting `bot.exe` can run with the bundled SDK DLLs.

## Run

```powershell
$env:HA3_WRAPPER_API_URL='https://ha3-api.dev.hackarena.pl/'
$env:HA_AUTH_PROFILE='preprod'
.\build\Release\bot.exe
```

## CLion

Use the `Visual Studio` toolchain with Architecture `amd64`.

CMake options:

```text
-DHACKARENA3_SDK_ROOT=C:\sdk\hackarena3-cpp-sdk-0.1.0b4-Windows-AMD64
```
