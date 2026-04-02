# HackArena3 C++ Bot Guide

This template gives you a minimal C++ bot project using the released `hackarena3` SDK.

Your bot class must implement:

- `void on_tick(const hackarena3::RaceSnapshot& snapshot, hackarena3::BotContext& ctx)`

The runtime calls this method every simulation tick.

## Core Types

### `RaceSnapshot` (read game state)

Important fields:

- `tick`, `server_time_ms`
- `car` (`CarState`):
  - `position`, `orientation`
  - `speed_mps` and `speed_kmh()`
  - `gear`
  - `tire_wear`, `tire_temperature_celsius`, `tire_slip`
  - `ghost_mode`
- `opponents` (`std::vector<OpponentState>`)

### `BotContext` (send commands)

Important fields:

- `car_id`
- `map_id`
- `track`
- `effective_hz`
- `tick`

Methods:

- `set_controls(throttle, brake, steer, gear_shift, brake_balancer, differential_lock)`
- `request_back_to_track()`
- `request_emergency_pitstop()`
- `set_next_pit_tire_type(tire_type)`

## Example Bot

```cpp
#include <algorithm>
#include <cstdint>

#include "hackarena3/hackarena3.hpp"

class ExampleBot final : public hackarena3::BotProtocol {
public:
    void on_tick(const hackarena3::RaceSnapshot& snapshot, hackarena3::BotContext& ctx) override {
        ++tick_;

        if (tick_ <= 50) {
            return;
        }

        if (((tick_ / 100) % 2) != 0) {
            if (snapshot.car.gear != hackarena3::DriveGear::Reverse) {
                ctx.set_controls(0.0, 0.5, 0.0, hackarena3::GearShift::Downshift);
                return;
            }
        } else {
            if (snapshot.car.gear == hackarena3::DriveGear::Reverse ||
                snapshot.car.gear == hackarena3::DriveGear::Neutral) {
                ctx.set_controls(0.0, 0.5, 0.0, hackarena3::GearShift::Upshift);
                return;
            }
        }

        const auto max_slip = std::max(
            {snapshot.car.tire_slip.front_left,
             snapshot.car.tire_slip.front_right,
             snapshot.car.tire_slip.rear_left,
             snapshot.car.tire_slip.rear_right}
        );
        if (max_slip > 1.0) {
            ctx.set_controls(0.0, 0.1, 0.0);
            return;
        }

        ctx.set_controls(0.55, 0.0, 0.0);
    }

private:
    std::uint64_t tick_ {0};
};

int main(int argc, char** argv) {
    ExampleBot bot;
    return hackarena3::run_bot(bot, argc, argv);
}
```

## Build and Run

From the repository root:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DHACKARENA3_SDK_ROOT=C:\sdk\hackarena3-cpp-sdk-0.1.0-Windows-AMD64
cmake --build build --config Release
.\build\Release\bot.exe
```

Environment variables used by the wrapper:

- `HA3_WRAPPER_API_URL` (required for sandbox mode)
- `HA_AUTH_PROFILE` (optional, passed through to `ha-auth`)
- `HA3_WRAPPER_BACKEND_ENDPOINT`, `HA3_WRAPPER_TEAM_TOKEN`, `HA3_WRAPPER_AUTH_TOKEN` (official mode)
