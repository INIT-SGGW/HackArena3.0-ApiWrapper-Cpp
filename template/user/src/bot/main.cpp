#include <algorithm>
#include <cstdint>
#include <iostream>

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
