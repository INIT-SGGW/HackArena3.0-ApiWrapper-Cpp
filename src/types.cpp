#include "hackarena3/errors.hpp"
#include "hackarena3/types.hpp"

#include <utility>

namespace hackarena3 {

RuntimeError::RuntimeError(const std::string& message) : std::runtime_error(message) {}

ConfigError::ConfigError(const std::string& message) : RuntimeError(message) {}

AuthError::AuthError(const std::string& message) : RuntimeError(message) {}

GameTokenError::GameTokenError(const std::string& message) : RuntimeError(message) {}

void BotContext::set_controls(
    double throttle,
    double brake,
    double steer,
    GearShift gear_shift,
    double brake_balancer,
    double differential_lock
) {
    set_controls_impl_(Controls {
        .throttle = throttle,
        .brake = brake,
        .steering = steer,
        .gear_shift = gear_shift,
        .brake_balancer = brake_balancer,
        .differential_lock = differential_lock,
    });
}

void BotContext::request_back_to_track() {
    request_back_to_track_impl_();
}

void BotContext::request_emergency_pitstop() {
    request_emergency_pitstop_impl_();
}

void BotContext::set_next_pit_tire_type(TireType tire_type) {
    set_next_pit_tire_type_impl_(tire_type);
}

void BotContext::attach_runtime_callbacks(
    std::function<void(const Controls&)> set_controls_impl,
    std::function<void()> request_back_to_track_impl,
    std::function<void()> request_emergency_pitstop_impl,
    std::function<void(TireType)> set_next_pit_tire_type_impl
) {
    set_controls_impl_ = std::move(set_controls_impl);
    request_back_to_track_impl_ = std::move(request_back_to_track_impl);
    request_emergency_pitstop_impl_ = std::move(request_emergency_pitstop_impl);
    set_next_pit_tire_type_impl_ = std::move(set_next_pit_tire_type_impl);
}

}  // namespace hackarena3
