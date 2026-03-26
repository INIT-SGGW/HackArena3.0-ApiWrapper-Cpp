#include "runtime_loop.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <grpcpp/client_context.h>
#include <grpcpp/impl/rpc_method.h>
#include <grpcpp/support/sync_stream.h>

#include "detail/constants.hpp"
#include "detail/grpc_utils.hpp"
#include "runtime_convert.hpp"
#include "hackarena3/errors.hpp"

namespace {

using Clock = std::chrono::steady_clock;

struct PendingAck {
    Clock::time_point started_at;
};

struct PendingCommandAck {
    Clock::time_point started_at;
    std::string command_kind;
};

enum class OutboundCommandKind {
    BackToTrack,
    EmergencyPitstop,
    SetNextPitTireType,
};

struct OutboundCommand {
    OutboundCommandKind kind {};
    std::optional<race::v1::TireType> next_tire_type;
};

struct SessionState {
    std::mutex mutex;
    std::condition_variable snapshot_cv;
    std::condition_variable outbound_cv;
    std::atomic_bool stop {false};
    bool snapshot_ready {false};
    bool outbound_ready {false};
    std::optional<hackarena3::RaceSnapshot> latest_snapshot;
    std::uint64_t latest_snapshot_version {0};
    std::optional<hackarena3::Controls> desired_controls;
    bool controls_dirty {false};
    std::deque<OutboundCommand> pending_commands;
    std::uint64_t next_client_seq {0};
    std::unordered_map<std::uint64_t, PendingAck> pending_acks;
    std::unordered_map<std::uint64_t, PendingCommandAck> pending_command_acks;
    std::optional<std::string> fatal_error;
    std::optional<std::uint32_t> effective_hz;
    std::optional<std::string> map_id;
};

std::string status_name(const grpc::Status& status) {
    return std::string(hackarena3::detail::grpc_status_code_name(
        static_cast<grpc::StatusCode>(status.error_code())
    ));
}

double ack_latency_warn_threshold_s() {
    return 2.0 / static_cast<double>(hackarena3::detail::kRequestedHz);
}

void request_stop(SessionState& state) {
    {
        std::lock_guard lock(state.mutex);
        state.stop.store(true);
        state.snapshot_ready = true;
        state.outbound_ready = true;
    }
    state.snapshot_cv.notify_all();
    state.outbound_cv.notify_all();
}

void set_fatal(SessionState& state, const std::string& message) {
    {
        std::lock_guard lock(state.mutex);
        if (!state.fatal_error.has_value()) {
            state.fatal_error = message;
        }
        state.stop.store(true);
        state.snapshot_ready = true;
        state.outbound_ready = true;
    }
    state.snapshot_cv.notify_all();
    state.outbound_cv.notify_all();
}

double clamp(double value, double lower, double upper) {
    return std::max(lower, std::min(upper, value));
}

hackarena3::Controls normalize_controls(const hackarena3::Controls& controls) {
    const auto normalized = hackarena3::Controls {
        .throttle = clamp(controls.throttle, 0.0, 1.0),
        .brake = clamp(controls.brake, 0.0, 1.0),
        .steering = clamp(controls.steering, -1.0, 1.0),
        .gear_shift = controls.gear_shift,
        .brake_balancer = clamp(controls.brake_balancer, 0.0, 1.0),
        .differential_lock = clamp(controls.differential_lock, 0.0, 1.0),
    };

    if (normalized.throttle != controls.throttle || normalized.brake != controls.brake ||
        normalized.steering != controls.steering ||
        normalized.brake_balancer != controls.brake_balancer ||
        normalized.differential_lock != controls.differential_lock) {
        std::cerr << "[ha3-wrapper] Bot set_controls out-of-range values were clamped "
                  << "(thr=" << controls.throttle
                  << ", brk=" << controls.brake
                  << ", str=" << controls.steering
                  << ", bb=" << controls.brake_balancer
                  << ", dl=" << controls.differential_lock << ").\n";
    }

    return normalized;
}

race::v1::GearShift normalize_gear_shift(hackarena3::GearShift gear_shift) {
    switch (gear_shift) {
        case hackarena3::GearShift::Upshift:
            return race::v1::GEAR_SHIFT_UPSHIFT;
        case hackarena3::GearShift::Downshift:
            return race::v1::GEAR_SHIFT_DOWNSHIFT;
        case hackarena3::GearShift::None:
        case hackarena3::GearShift::Unspecified:
        default:
            return race::v1::GEAR_SHIFT_NONE;
    }
}

void set_desired_controls(SessionState& state, const hackarena3::Controls& controls) {
    {
        std::lock_guard lock(state.mutex);
        state.desired_controls = controls;
        state.controls_dirty = true;
        state.outbound_ready = true;
    }
    state.outbound_cv.notify_one();
}

void enqueue_command(SessionState& state, OutboundCommand command) {
    {
        std::lock_guard lock(state.mutex);
        state.pending_commands.push_back(std::move(command));
        state.outbound_ready = true;
    }
    state.outbound_cv.notify_one();
}

void handle_ack(SessionState& state, const race::v1::ParticipantControlsAck& ack) {
    std::optional<PendingAck> pending;
    {
        std::lock_guard lock(state.mutex);
        const auto it = state.pending_acks.find(ack.client_seq());
        if (it != state.pending_acks.end()) {
            pending = it->second;
            state.pending_acks.erase(it);
        }
    }

    if (!pending.has_value()) {
        return;
    }

    const auto elapsed =
        std::chrono::duration<double>(Clock::now() - pending->started_at).count();
    if (elapsed > ack_latency_warn_threshold_s()) {
        std::cerr << "[ha3-wrapper] Controls ack latency warning: seq=" << ack.client_seq()
                  << " rtt_ms=" << (elapsed * 1000.0)
                  << " threshold_ms=" << (ack_latency_warn_threshold_s() * 1000.0) << '\n';
    }
}

void handle_command_ack(SessionState& state, const race::v1::ParticipantCommandAck& ack) {
    std::optional<PendingCommandAck> pending;
    {
        std::lock_guard lock(state.mutex);
        const auto it = state.pending_command_acks.find(ack.client_seq());
        if (it != state.pending_command_acks.end()) {
            pending = it->second;
            state.pending_command_acks.erase(it);
        }
    }

    if (!pending.has_value()) {
        return;
    }

    const auto elapsed =
        std::chrono::duration<double>(Clock::now() - pending->started_at).count();
    if (elapsed > ack_latency_warn_threshold_s()) {
        std::cerr << "[ha3-wrapper] Command ack latency warning: seq=" << ack.client_seq()
                  << " command=" << pending->command_kind
                  << " rtt_ms=" << (elapsed * 1000.0)
                  << " threshold_ms=" << (ack_latency_warn_threshold_s() * 1000.0) << '\n';
    }

    if (ack.status() == race::v1::PARTICIPANT_COMMAND_STATUS_REJECTED) {
        std::cerr << "[ha3-wrapper] Participant command rejected: seq=" << ack.client_seq()
                  << " command=" << pending->command_kind
                  << " command_type=" << race::v1::ParticipantCommandType_Name(ack.command_type())
                  << " reason="
                  << race::v1::ParticipantCommandRejectReason_Name(ack.rejected_reason())
                  << " cooldown_remaining_ms=" << ack.cooldown_remaining_ms() << '\n';
    }
}

race::v1::ParticipantClientMessage stream_init_message() {
    race::v1::ParticipantClientMessage message;
    auto* init = message.mutable_init();
    init->set_wrapper_type(race::v1::PARTICIPANT_WRAPPER_TYPE_CPP);
    init->set_wrapper_version(HACKARENA3_WRAPPER_VERSION);
    return message;
}

void reader_loop(
    grpc::ClientReaderWriterInterface<race::v1::ParticipantClientMessage, race::v1::ParticipantServerEvent>* stream,
    SessionState& state,
    hackarena3::BotContext& ctx,
    const std::optional<std::string>& expected_map_id
) {
    std::optional<std::uint32_t> last_effective_hz;
    std::optional<std::string> last_map_id;

    try {
        race::v1::ParticipantServerEvent event;
        while (stream->Read(&event)) {
            switch (event.payload_case()) {
                case race::v1::ParticipantServerEvent::kSettings: {
                    const auto effective_hz = static_cast<std::uint32_t>(event.settings().effective_hz());
                    const auto map_id = event.settings().map_id();
                    if (expected_map_id.has_value() && !expected_map_id->empty() && !map_id.empty() &&
                        map_id != *expected_map_id) {
                        set_fatal(
                            state,
                            "Stream map_id mismatch: prepared='" + *expected_map_id +
                                "', stream='" + map_id + "'."
                        );
                        break;
                    }
                    std::optional<std::uint32_t> current_effective_hz;
                    std::optional<std::string> current_map_id;
                    {
                        std::lock_guard lock(state.mutex);
                        state.effective_hz = effective_hz > 0 ? std::make_optional(effective_hz) : std::nullopt;
                        if (!map_id.empty()) {
                            state.map_id = map_id;
                        }
                        current_effective_hz = state.effective_hz;
                        current_map_id = state.map_id;
                    }
                    if (last_effective_hz != current_effective_hz || last_map_id != current_map_id) {
                        std::cerr << "[ha3-wrapper] Stream settings: effective_hz="
                                  << (effective_hz > 0 ? std::to_string(effective_hz) : "0");
                        if (!map_id.empty()) {
                            std::cerr << " map_id=" << map_id;
                        }
                        std::cerr << '\n';
                        last_effective_hz = current_effective_hz;
                        last_map_id = current_map_id;
                    }
                    break;
                }
                case race::v1::ParticipantServerEvent::kAck:
                    handle_ack(state, event.ack());
                    break;
                case race::v1::ParticipantServerEvent::kCommandAck:
                    handle_command_ack(state, event.command_ack());
                    break;
                case race::v1::ParticipantServerEvent::kBootstrap:
                    ctx.car_dimensions = hackarena3::detail::build_car_dimensions(
                        event.bootstrap().car_dimensions()
                    );
                    std::cerr << "[ha3-wrapper] Stream bootstrap: car_width_m="
                              << ctx.car_dimensions.width_m
                              << " car_depth_m=" << ctx.car_dimensions.depth_m << '\n';
                    break;
                case race::v1::ParticipantServerEvent::kSnapshot: {
                    auto snapshot = hackarena3::detail::build_race_snapshot(event.snapshot());
                    {
                        std::lock_guard lock(state.mutex);
                        state.latest_snapshot = std::move(snapshot);
                        ++state.latest_snapshot_version;
                        state.snapshot_ready = true;
                    }
                    state.snapshot_cv.notify_one();
                    break;
                }
                case race::v1::ParticipantServerEvent::PAYLOAD_NOT_SET:
                    break;
            }
        }
        request_stop(state);
    } catch (const std::exception& exc) {
        set_fatal(state, std::string("Reader loop failed: ") + exc.what());
    }
}

void callback_loop(
    hackarena3::BotProtocol& bot,
    SessionState& state,
    hackarena3::BotContext& ctx
) {
    std::uint64_t processed_version = 0;

    for (;;) {
        std::optional<hackarena3::RaceSnapshot> snapshot;
        std::uint64_t version = 0;
        std::optional<std::uint32_t> effective_hz;
        std::optional<std::string> map_id;

        {
            std::unique_lock lock(state.mutex);
            state.snapshot_cv.wait_for(lock, hackarena3::detail::kRuntimePollInterval, [&] {
                return state.stop.load() || state.snapshot_ready;
            });

            if (state.stop.load() && !state.snapshot_ready) {
                break;
            }

            state.snapshot_ready = false;
            snapshot = state.latest_snapshot;
            version = state.latest_snapshot_version;
            effective_hz = state.effective_hz;
            map_id = state.map_id;
        }

        if (!snapshot.has_value() || version == processed_version) {
            if (state.stop.load()) {
                break;
            }
            continue;
        }

        processed_version = version;
        ctx.tick = snapshot->tick;
        ctx.effective_hz = effective_hz;
        if (map_id.has_value()) {
            ctx.map_id = *map_id;
        }

        try {
            bot.on_tick(*snapshot, ctx);
        } catch (const std::exception& exc) {
            set_fatal(state, std::string("Bot on_tick failed: ") + exc.what());
            break;
        } catch (...) {
            set_fatal(state, "Bot on_tick failed with a non-standard exception.");
            break;
        }
    }
}

void writer_loop(
    grpc::ClientReaderWriterInterface<race::v1::ParticipantClientMessage, race::v1::ParticipantServerEvent>* stream,
    SessionState& state
) {
    if (!stream->Write(stream_init_message())) {
        request_stop(state);
        stream->WritesDone();
        return;
    }

    for (;;) {
        std::optional<race::v1::ParticipantClientMessage> message;

        {
            std::unique_lock lock(state.mutex);
            state.outbound_cv.wait_for(lock, hackarena3::detail::kRuntimePollInterval, [&] {
                return state.stop.load() || state.outbound_ready || state.controls_dirty ||
                    !state.pending_commands.empty();
            });

            state.outbound_ready = false;
            if (state.stop.load() && state.pending_commands.empty() && !state.controls_dirty) {
                break;
            }

            if (!state.pending_commands.empty()) {
                const auto command = state.pending_commands.front();
                state.pending_commands.pop_front();
                const auto client_seq = ++state.next_client_seq;
                state.pending_command_acks.emplace(
                    client_seq,
                    PendingCommandAck {
                        .started_at = Clock::now(),
                        .command_kind = command.kind == OutboundCommandKind::BackToTrack
                            ? "back_to_track"
                            : (command.kind == OutboundCommandKind::EmergencyPitstop
                                   ? "emergency_pitstop"
                                   : "set_next_pit_tire_type"),
                    }
                );

                race::v1::ParticipantClientMessage outbound;
                switch (command.kind) {
                    case OutboundCommandKind::BackToTrack:
                        outbound.mutable_back_to_track()->set_client_seq(client_seq);
                        break;
                    case OutboundCommandKind::EmergencyPitstop:
                        outbound.mutable_emergency_pitstop()->set_client_seq(client_seq);
                        break;
                    case OutboundCommandKind::SetNextPitTireType:
                        if (!command.next_tire_type.has_value()) {
                            lock.unlock();
                            set_fatal(
                                state,
                                "Missing next_tire_type for set_next_pit_tire_type command."
                            );
                            stream->WritesDone();
                            return;
                        }
                        outbound.mutable_set_next_pit_tire_type()->set_client_seq(client_seq);
                        outbound.mutable_set_next_pit_tire_type()->set_next_tire_type(
                            *command.next_tire_type
                        );
                        break;
                }
                message = std::move(outbound);
            } else if (state.desired_controls.has_value() && state.controls_dirty) {
                state.controls_dirty = false;
                const auto client_seq = ++state.next_client_seq;
                const auto normalized = normalize_controls(*state.desired_controls);

                race::v1::ParticipantClientMessage outbound;
                auto* controls = outbound.mutable_controls();
                controls->set_client_seq(client_seq);
                controls->set_throttle(static_cast<float>(normalized.throttle));
                controls->set_brake(static_cast<float>(normalized.brake));
                controls->set_steering(static_cast<float>(normalized.steering));
                controls->set_gear_shift(normalize_gear_shift(normalized.gear_shift));
                controls->set_brake_balancer(static_cast<float>(normalized.brake_balancer));
                controls->set_differential_lock(static_cast<float>(normalized.differential_lock));

                state.pending_acks.emplace(
                    client_seq,
                    PendingAck {.started_at = Clock::now()}
                );
                message = std::move(outbound);
            }
        }

        if (!message.has_value()) {
            if (state.stop.load()) {
                break;
            }
            continue;
        }

        if (!stream->Write(*message)) {
            request_stop(state);
            break;
        }

        {
            std::lock_guard lock(state.mutex);
            if (!state.pending_commands.empty() || state.controls_dirty) {
                state.outbound_ready = true;
            }
        }
        state.outbound_cv.notify_one();
    }

    stream->WritesDone();
}

}  // namespace

namespace hackarena3::detail {

void run_participant_loop(
    BotProtocol& bot,
    RaceApi& api,
    BotContext& ctx,
    const StreamMetadataProvider& metadata_provider,
    GameTokenProvider* token_provider,
    bool allow_auth_refresh,
    const std::optional<std::string>& stream_method,
    const std::optional<std::string>& expected_map_id
) {
    if (allow_auth_refresh && token_provider == nullptr) {
        throw RuntimeError(
            "Internal error: token_provider is required when allow_auth_refresh=true."
        );
    }

    int retry_attempt = 0;
    std::optional<Controls> latest_controls;

    for (;;) {
        SessionState state;
        state.desired_controls = latest_controls;
        state.controls_dirty = latest_controls.has_value();
        state.outbound_ready = state.controls_dirty;

        ctx.attach_runtime_callbacks(
            [&](const Controls& controls) {
                latest_controls = controls;
                set_desired_controls(state, controls);
            },
            [&] {
                enqueue_command(
                    state,
                    OutboundCommand {
                        .kind = OutboundCommandKind::BackToTrack,
                        .next_tire_type = std::nullopt,
                    }
                );
            },
            [&] {
                enqueue_command(
                    state,
                    OutboundCommand {
                        .kind = OutboundCommandKind::EmergencyPitstop,
                        .next_tire_type = std::nullopt,
                    }
                );
            },
            [&](TireType tire_type) {
                enqueue_command(
                    state,
                    OutboundCommand {
                        .kind = OutboundCommandKind::SetNextPitTireType,
                        .next_tire_type = static_cast<race::v1::TireType>(tire_type),
                    }
                );
            }
        );

        grpc::ClientContext client_context;
        std::vector<std::pair<std::string, std::string>> stream_metadata;
        try {
            stream_metadata = metadata_provider();
        } catch (const RuntimeError&) {
            throw;
        } catch (const std::exception& exc) {
            throw RuntimeError(std::string("Failed to prepare stream metadata: ") + exc.what());
        }
        for (const auto& [key, value] : stream_metadata) {
            client_context.AddMetadata(key, value);
        }

        std::unique_ptr<grpc::ClientReaderWriterInterface<
            race::v1::ParticipantClientMessage,
            race::v1::ParticipantServerEvent>>
            stream;
        if (stream_method.has_value()) {
            grpc::internal::RpcMethod rpc_method(
                stream_method->c_str(),
                grpc::internal::RpcMethod::BIDI_STREAMING,
                api.channel
            );
            stream.reset(grpc::internal::ClientReaderWriterFactory<
                         race::v1::ParticipantClientMessage,
                         race::v1::ParticipantServerEvent>::Create(
                api.channel.get(),
                rpc_method,
                &client_context
            ));
        } else {
            stream = api.participant->Stream(&client_context);
        }
        if (!stream) {
            throw RuntimeError("Race participant stream open failed.");
        }

        std::thread reader(
            reader_loop,
            stream.get(),
            std::ref(state),
            std::ref(ctx),
            std::cref(expected_map_id)
        );
        std::thread callback(callback_loop, std::ref(bot), std::ref(state), std::ref(ctx));
        std::thread writer(writer_loop, stream.get(), std::ref(state));

        bool token_rotated = false;
        for (;;) {
            {
                std::lock_guard lock(state.mutex);
                if (state.stop.load()) {
                    break;
                }
            }
            std::this_thread::sleep_for(detail::kRuntimePollInterval);
            try {
                if (allow_auth_refresh &&
                    token_provider != nullptr &&
                    token_provider->ensure_fresh(detail::kTokenRefreshSkewSeconds)) {
                    token_rotated = true;
                    request_stop(state);
                    break;
                }
            } catch (const GameTokenError& exc) {
                set_fatal(state, std::string("Game token refresh failed: ") + exc.what());
                break;
            }
        }

        {
            std::lock_guard lock(state.mutex);
            if (state.fatal_error.has_value() || token_rotated) {
                client_context.TryCancel();
            }
        }

        reader.join();
        writer.join();
        callback.join();

        const auto final_status = stream->Finish();

        if (state.fatal_error.has_value()) {
            throw RuntimeError(*state.fatal_error);
        }

        if (token_rotated) {
            retry_attempt = 0;
            continue;
        }

        if (final_status.ok()) {
            throw RuntimeError("Participant stream stopped unexpectedly.");
        }

        if (final_status.error_code() == grpc::StatusCode::UNIMPLEMENTED) {
            throw RuntimeError(
                "Required participant stream method is unavailable (UNIMPLEMENTED)."
            );
        }

        if (final_status.error_code() == grpc::StatusCode::UNAUTHENTICATED ||
            final_status.error_code() == grpc::StatusCode::PERMISSION_DENIED) {
            if (!allow_auth_refresh || token_provider == nullptr) {
                throw RuntimeError(
                    "Authentication failed (" + status_name(final_status) + "): " +
                    (final_status.error_message().empty()
                         ? std::string("no details")
                         : final_status.error_message())
                );
            }
            token_provider->refresh();
            retry_attempt = 0;
            continue;
        }

        if ((final_status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED ||
             final_status.error_code() == grpc::StatusCode::UNAVAILABLE) &&
            retry_attempt < static_cast<int>(detail::kRetryBackoffSeconds.size())) {
            const auto delay = detail::kRetryBackoffSeconds[retry_attempt++];
            std::this_thread::sleep_for(std::chrono::duration<double>(delay));
            continue;
        }

        throw RuntimeError(
            "gRPC error " + status_name(final_status) + ": " +
            (final_status.error_message().empty() ? "no details" : final_status.error_message())
        );
    }
}

}  // namespace hackarena3::detail
