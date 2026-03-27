#pragma once

#include "hackarena3/types.hpp"

#include "race/v1/race.pb.h"
#include "race/v1/telemetry.pb.h"
#include "race/v1/track.pb.h"

namespace hackarena3::detail {

CarDimensions build_car_dimensions(const race::v1::CarDimensions& raw);
RaceSnapshot build_race_snapshot(const race::v1::ParticipantSnapshot& raw);
TrackLayout build_track_layout(const race::v1::TrackData& track_data);

}  // namespace hackarena3::detail
