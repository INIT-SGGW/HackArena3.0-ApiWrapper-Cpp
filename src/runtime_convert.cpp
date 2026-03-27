#include "runtime_convert.hpp"

#include <memory>
#include <string>
#include <vector>

#include "hackarena3/errors.hpp"

namespace {

hackarena3::Vec3 vec3_from_proto(const race::v1::Vector3& value) {
    return hackarena3::Vec3 {.x = value.x(), .y = value.y(), .z = value.z()};
}

hackarena3::Quaternion quaternion_from_proto(const race::v1::Quaternion& value) {
    return hackarena3::Quaternion {.x = value.x(), .y = value.y(), .z = value.z(), .w = value.w()};
}

hackarena3::GhostModePhase ghost_mode_phase_from_raw(std::int32_t raw) {
    return static_cast<hackarena3::GhostModePhase>(raw);
}

hackarena3::GhostModeBlocker ghost_mode_blocker_from_raw(std::int32_t raw) {
    return static_cast<hackarena3::GhostModeBlocker>(raw);
}

hackarena3::GroundType ground_type_from_raw(std::int32_t raw) {
    return static_cast<hackarena3::GroundType>(raw);
}

hackarena3::GroundWidth ground_width_from_proto(const race::v1::GroundWidth& value) {
    const auto raw = static_cast<std::int32_t>(value.ground_type());
    return hackarena3::GroundWidth {
        .width_m = value.width_m(),
        .ground_type = ground_type_from_raw(raw),
    };
}

hackarena3::CenterlinePoint centerline_point_from_proto(const race::v1::CenterlineSample& sample) {
    hackarena3::CenterlinePoint point {
        .s_m = sample.s_m(),
        .position = vec3_from_proto(sample.position()),
        .tangent = vec3_from_proto(sample.tangent()),
        .normal = vec3_from_proto(sample.normal()),
        .right = vec3_from_proto(sample.right()),
        .left_width_m = sample.left_width_m(),
        .right_width_m = sample.right_width_m(),
        .curvature_1pm = sample.curvature_1pm(),
        .grade_rad = sample.grade_rad(),
        .bank_rad = sample.bank_rad(),
        .max_left_width_m = sample.max_left_width_m(),
        .max_right_width_m = sample.max_right_width_m(),
        .left_grounds = {},
        .right_grounds = {},
    };

    point.left_grounds.reserve(static_cast<std::size_t>(sample.left_grounds_size()));
    for (const auto& ground : sample.left_grounds()) {
        point.left_grounds.push_back(ground_width_from_proto(ground));
    }
    point.right_grounds.reserve(static_cast<std::size_t>(sample.right_grounds_size()));
    for (const auto& ground : sample.right_grounds()) {
        point.right_grounds.push_back(ground_width_from_proto(ground));
    }

    return point;
}

hackarena3::GhostModeState ghost_mode_from_proto(const race::v1::GhostModeState& value) {
    hackarena3::GhostModeState state {
        .can_collide_now = value.can_collide_now(),
        .phase = ghost_mode_phase_from_raw(static_cast<std::int32_t>(value.phase())),
        .blockers = {},
        .exit_delay_remaining_ms = static_cast<std::int32_t>(value.exit_delay_remaining_ms()),
    };
    state.blockers.reserve(static_cast<std::size_t>(value.blockers_size()));
    for (const auto blocker : value.blockers()) {
        const auto raw = static_cast<std::int32_t>(blocker);
        state.blockers.push_back(ghost_mode_blocker_from_raw(raw));
    }
    return state;
}

hackarena3::PitEntrySource pit_entry_source_from_raw(std::int32_t raw) {
    return static_cast<hackarena3::PitEntrySource>(raw);
}

hackarena3::DriveGear drive_gear_from_raw(std::int32_t raw) {
    return static_cast<hackarena3::DriveGear>(raw);
}

hackarena3::TireType tire_type_from_raw(std::int32_t raw) {
    return static_cast<hackarena3::TireType>(raw);
}

hackarena3::TireWearPerWheel tire_wear_from_proto(const race::v1::TireWearPerWheel& value) {
    return hackarena3::TireWearPerWheel {
        .front_left = value.front_left(),
        .front_right = value.front_right(),
        .rear_left = value.rear_left(),
        .rear_right = value.rear_right(),
    };
}

hackarena3::TireTemperaturePerWheel tire_temperature_from_proto(
    const race::v1::TireTemperaturePerWheel& value
) {
    return hackarena3::TireTemperaturePerWheel {
        .front_left_celsius = value.front_left_celsius(),
        .front_right_celsius = value.front_right_celsius(),
        .rear_left_celsius = value.rear_left_celsius(),
        .rear_right_celsius = value.rear_right_celsius(),
    };
}

hackarena3::TireSlipPerWheel tire_slip_from_proto(const race::v1::TireSlipPerWheel& value) {
    return hackarena3::TireSlipPerWheel {
        .front_left = value.front_left(),
        .front_right = value.front_right(),
        .rear_left = value.rear_left(),
        .rear_right = value.rear_right(),
    };
}

hackarena3::CommandCooldownState command_cooldowns_from_proto(
    const race::v1::CommandCooldownState& value
) {
    return hackarena3::CommandCooldownState {
        .back_to_track_remaining_ms = value.back_to_track_remaining_ms(),
        .emergency_pitstop_remaining_ms = value.emergency_pitstop_remaining_ms(),
    };
}

}  // namespace

namespace hackarena3::detail {

CarDimensions build_car_dimensions(const race::v1::CarDimensions& raw) {
    return CarDimensions {
        .width_m = raw.width_m(),
        .depth_m = raw.depth_m(),
    };
}

RaceSnapshot build_race_snapshot(const race::v1::ParticipantSnapshot& raw) {
    std::vector<OpponentState> opponents;
    opponents.reserve(static_cast<std::size_t>(raw.opponents_size()));
    for (const auto& opponent : raw.opponents()) {
        opponents.push_back(OpponentState {
            .car_id = opponent.car_id(),
            .position = vec3_from_proto(opponent.kinematics().position()),
            .orientation = quaternion_from_proto(opponent.kinematics().orientation()),
            .ghost_mode = ghost_mode_from_proto(opponent.ghost_mode()),
        });
    }

    const auto tire_type = tire_type_from_raw(static_cast<std::int32_t>(raw.self().telemetry().tire_type()));
    const auto next_pit_tire_type =
        tire_type_from_raw(static_cast<std::int32_t>(raw.self().telemetry().next_pit_tire_type()));
    const auto tire_wear = tire_wear_from_proto(raw.self().telemetry().tire_wear());
    const auto tire_temperature =
        tire_temperature_from_proto(raw.self().telemetry().tire_temperature_celsius());
    const auto tire_slip = tire_slip_from_proto(raw.self().telemetry().tire_slip());
    const auto command_cooldowns =
        command_cooldowns_from_proto(raw.self().telemetry().command_cooldowns());
    const auto& pit_runtime = raw.self().telemetry().pit_runtime();

    return RaceSnapshot {
        .tick = raw.tick(),
        .server_time_ms = raw.server_time_ms(),
        .car = CarState {
            .car_id = raw.self().car_id(),
            .position = vec3_from_proto(raw.self().kinematics().position()),
            .orientation = quaternion_from_proto(raw.self().kinematics().orientation()),
            .speed_mps = raw.self().telemetry().speed_mps(),
            .gear = drive_gear_from_raw(raw.self().telemetry().gear()),
            .engine_rpm = raw.self().telemetry().engine_rpm(),
            .last_applied_client_seq = raw.self().telemetry().last_applied_client_seq(),
            .pitstop_zone_flags = static_cast<PitstopZoneFlag>(
                raw.self().telemetry().pitstop_zone_flags()
            ),
            .wheels_in_pitstop = raw.self().telemetry().wheels_in_pitstop(),
            .ghost_mode = ghost_mode_from_proto(raw.self().telemetry().ghost_mode()),
            .tire_type = tire_type,
            .next_pit_tire_type = next_pit_tire_type,
            .tire_wear = tire_wear,
            .tire_temperature_celsius = tire_temperature,
            .tire_slip = tire_slip,
            .pit_request_active = pit_runtime.pit_request_active(),
            .pit_emergency_lock_remaining_ms = pit_runtime.emergency_lock_remaining_ms(),
            .last_pit_time_ms = pit_runtime.last_pit_time_ms(),
            .last_pit_source = pit_entry_source_from_raw(static_cast<std::int32_t>(pit_runtime.last_pit_source())),
            .last_pit_lap = pit_runtime.last_pit_lap(),
            .command_cooldowns = command_cooldowns,
        },
        .opponents = std::move(opponents),
        .raw = std::make_shared<race::v1::ParticipantSnapshot>(raw),
    };
}

TrackLayout build_track_layout(const race::v1::TrackData& track_data) {
    TrackLayout layout {
        .map_id = track_data.map_id(),
        .lap_length_m = track_data.lap_length_m(),
        .centerline = {},
        .pitstop = {},
    };

    layout.centerline.reserve(static_cast<std::size_t>(track_data.centerline_samples_size()));
    for (const auto& sample : track_data.centerline_samples()) {
        layout.centerline.push_back(centerline_point_from_proto(sample));
    }

    const auto& pit = track_data.pitstop_data();
    layout.pitstop.length_m = pit.length_m();
    layout.pitstop.enter.reserve(static_cast<std::size_t>(pit.enter_centerline_samples_size()));
    for (const auto& sample : pit.enter_centerline_samples()) {
        layout.pitstop.enter.push_back(centerline_point_from_proto(sample));
    }
    layout.pitstop.fix.reserve(static_cast<std::size_t>(pit.fix_centerline_samples_size()));
    for (const auto& sample : pit.fix_centerline_samples()) {
        layout.pitstop.fix.push_back(centerline_point_from_proto(sample));
    }
    layout.pitstop.exit.reserve(static_cast<std::size_t>(pit.exit_centerline_samples_size()));
    for (const auto& sample : pit.exit_centerline_samples()) {
        layout.pitstop.exit.push_back(centerline_point_from_proto(sample));
    }

    return layout;
}

}  // namespace hackarena3::detail
