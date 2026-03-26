#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <google/protobuf/message.h>

#include "race/v1/race.pb.h"
#include "race/v1/telemetry.pb.h"
#include "race/v1/track.pb.h"

namespace hackarena3 {

enum class GearShift : std::int32_t {
    Unspecified = race::v1::GEAR_SHIFT_UNSPECIFIED,
    None = race::v1::GEAR_SHIFT_NONE,
    Upshift = race::v1::GEAR_SHIFT_UPSHIFT,
    Downshift = race::v1::GEAR_SHIFT_DOWNSHIFT,
};

enum class DriveGear : std::int32_t {
    Reverse = -1,
    Neutral = 0,
    First = 1,
    Second = 2,
    Third = 3,
    Fourth = 4,
    Fifth = 5,
    Sixth = 6,
    Seventh = 7,
    Eighth = 8,
};

enum class TireType : std::int32_t {
    Unspecified = race::v1::TIRE_TYPE_UNSPECIFIED,
    Hard = race::v1::TIRE_TYPE_HARD,
    Soft = race::v1::TIRE_TYPE_SOFT,
    Wet = race::v1::TIRE_TYPE_WET,
};

enum class GroundType : std::int32_t {
    Asphalt = race::v1::GROUND_TYPE_ASPHALT,
    Grass = race::v1::GROUND_TYPE_GRASS,
    Gravel = race::v1::GROUND_TYPE_GRAVEL,
    Wall = race::v1::GROUND_TYPE_WALL,
    Kerb = race::v1::GROUND_TYPE_KERB,
};

enum class GhostModePhase : std::int32_t {
    Unspecified = race::v1::GHOST_MODE_PHASE_UNSPECIFIED,
    Inactive = race::v1::GHOST_MODE_PHASE_INACTIVE,
    Active = race::v1::GHOST_MODE_PHASE_ACTIVE,
    PendingExit = race::v1::GHOST_MODE_PHASE_PENDING_EXIT,
};

enum class GhostModeBlocker : std::int32_t {
    Unspecified = race::v1::GHOST_MODE_BLOCKER_UNSPECIFIED,
    LapsRequirementNotMet = race::v1::GHOST_MODE_BLOCKER_LAPS_REQUIREMENT_NOT_MET,
    ExitSpeedNotMet = race::v1::GHOST_MODE_BLOCKER_EXIT_SPEED_NOT_MET,
    ExitDelayRunning = race::v1::GHOST_MODE_BLOCKER_EXIT_DELAY_RUNNING,
    VehicleOverlapActive = race::v1::GHOST_MODE_BLOCKER_VEHICLE_OVERLAP_ACTIVE,
    OverlapExitDelayRunning = race::v1::GHOST_MODE_BLOCKER_OVERLAP_EXIT_DELAY_RUNNING,
    InPit = race::v1::GHOST_MODE_BLOCKER_IN_PIT,
};

enum class PitstopZoneFlag : std::uint32_t {
    None = 0,
    Enter = race::v1::PITSTOP_ZONE_FLAG_ENTER,
    Fix = race::v1::PITSTOP_ZONE_FLAG_FIX,
    Exit = race::v1::PITSTOP_ZONE_FLAG_EXIT,
};

enum class PitEntrySource : std::int32_t {
    Unspecified = race::v1::PIT_ENTRY_SOURCE_UNSPECIFIED,
    BotDecision = race::v1::PIT_ENTRY_SOURCE_BOT_DECISION,
    Requested = race::v1::PIT_ENTRY_SOURCE_REQUESTED,
    Emergency = race::v1::PIT_ENTRY_SOURCE_EMERGENCY,
};

struct Vec3 {
    double x {};
    double y {};
    double z {};
};

struct CarDimensions {
    double width_m {};
    double depth_m {};
};

struct GroundWidth {
    double width_m {};
    std::int32_t ground_type_raw {};
    std::optional<GroundType> ground_type;
};

struct CenterlinePoint {
    double s_m {};
    Vec3 position;
    Vec3 tangent;
    Vec3 normal;
    Vec3 right;
    double left_width_m {};
    double right_width_m {};
    double curvature_1pm {};
    double grade_rad {};
    double bank_rad {};
    double max_left_width_m {};
    double max_right_width_m {};
    std::vector<GroundWidth> left_grounds;
    std::vector<GroundWidth> right_grounds;
};

struct PitstopLayout {
    std::vector<CenterlinePoint> enter;
    std::vector<CenterlinePoint> fix;
    std::vector<CenterlinePoint> exit;
    double length_m {};
};

struct TrackLayout {
    std::string map_id;
    double lap_length_m {};
    std::vector<CenterlinePoint> centerline;
    PitstopLayout pitstop;
};

struct Controls {
    double throttle {};
    double brake {};
    double steering {};
    GearShift gear_shift {GearShift::None};
    double brake_balancer {0.5};
    double differential_lock {};
};

struct Quaternion {
    double x {};
    double y {};
    double z {};
    double w {1.0};
};

struct GhostModeState {
    bool can_collide_now {};
    std::int32_t phase_raw {};
    std::optional<GhostModePhase> phase;
    std::vector<std::int32_t> blockers_raw;
    std::vector<GhostModeBlocker> blockers;
    std::int32_t exit_delay_remaining_ms {};

    [[nodiscard]] bool is_ghost() const noexcept {
        return !can_collide_now;
    }
};

struct TireWearPerWheel {
    double front_left {};
    double front_right {};
    double rear_left {};
    double rear_right {};
};

struct TireTemperaturePerWheel {
    double front_left_celsius {};
    double front_right_celsius {};
    double rear_left_celsius {};
    double rear_right_celsius {};
};

struct TireSlipPerWheel {
    double front_left {};
    double front_right {};
    double rear_left {};
    double rear_right {};
};

struct CommandCooldownState {
    std::uint32_t back_to_track_remaining_ms {};
    std::uint32_t emergency_pitstop_remaining_ms {};
};

struct CarState {
    std::uint64_t car_id {};
    Vec3 position;
    Quaternion orientation;
    double speed_mps {};
    std::int32_t gear_raw {};
    DriveGear gear {DriveGear::Neutral};
    double engine_rpm {};
    std::uint64_t last_applied_client_seq {};
    std::uint32_t pitstop_zone_flags {};
    std::uint32_t wheels_in_pitstop {};
    std::optional<GhostModeState> ghost_mode;
    std::int32_t tire_type_raw {};
    TireType tire_type {TireType::Unspecified};
    std::int32_t next_pit_tire_type_raw {};
    TireType next_pit_tire_type {TireType::Unspecified};
    TireWearPerWheel tire_wear;
    TireTemperaturePerWheel tire_temperature_celsius;
    TireSlipPerWheel tire_slip;
    bool pit_request_active {};
    std::uint32_t pit_emergency_lock_remaining_ms {};
    std::uint64_t last_pit_time_ms {};
    std::int32_t last_pit_source_raw {};
    std::optional<PitEntrySource> last_pit_source;
    std::uint32_t last_pit_lap {};
    CommandCooldownState command_cooldowns;

    [[nodiscard]] double speed_kmh() const noexcept {
        return speed_mps * 3.6;
    }
};

struct OpponentState {
    std::uint64_t car_id {};
    Vec3 position;
    Quaternion orientation;
    std::optional<GhostModeState> ghost_mode;
};

struct RaceSnapshot {
    std::uint64_t tick {};
    std::uint64_t server_time_ms {};
    CarState car;
    std::vector<OpponentState> opponents;
    std::int32_t tire_type_raw {};
    TireType tire_type {TireType::Unspecified};
    TireWearPerWheel tire_wear;
    TireTemperaturePerWheel tire_temperature_celsius;
    std::shared_ptr<const race::v1::ParticipantSnapshot> raw;
};

class BotContext {
public:
    std::uint64_t car_id {};
    std::string map_id;
    CarDimensions car_dimensions;
    std::uint32_t requested_hz {};
    std::shared_ptr<const race::v1::TrackData> track_data;
    TrackLayout track;
    std::optional<std::uint32_t> effective_hz;
    std::uint64_t tick {};
    std::shared_ptr<const google::protobuf::Message> raw;

    void set_controls(
        double throttle,
        double brake,
        double steer,
        GearShift gear_shift = GearShift::None,
        double brake_balancer = 0.5,
        double differential_lock = 0.0
    );

    void request_back_to_track();
    void request_emergency_pitstop();
    void set_next_pit_tire_type(TireType tire_type);
    void attach_runtime_callbacks(
        std::function<void(const Controls&)> set_controls_impl,
        std::function<void()> request_back_to_track_impl,
        std::function<void()> request_emergency_pitstop_impl,
        std::function<void(TireType)> set_next_pit_tire_type_impl
    );

private:
    std::function<void(const Controls&)> set_controls_impl_ = [](const Controls&) {
        throw std::runtime_error("BotContext is not attached to an active runtime.");
    };
    std::function<void()> request_back_to_track_impl_ = [] {
        throw std::runtime_error("BotContext is not attached to an active runtime.");
    };
    std::function<void()> request_emergency_pitstop_impl_ = [] {
        throw std::runtime_error("BotContext is not attached to an active runtime.");
    };
    std::function<void(TireType)> set_next_pit_tire_type_impl_ = [](TireType) {
        throw std::runtime_error("BotContext is not attached to an active runtime.");
    };
};

class BotProtocol {
public:
    virtual ~BotProtocol() = default;
    virtual void on_tick(const RaceSnapshot& snapshot, BotContext& ctx) = 0;
};

}  // namespace hackarena3
