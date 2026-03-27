#include "runtime.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

#include <grpcpp/client_context.h>

#include "auth.hpp"
#include "detail/constants.hpp"
#include "detail/grpc_utils.hpp"
#include "detail/string_utils.hpp"
#include "game_token.hpp"
#include "runtime_common.hpp"
#include "runtime_convert.hpp"
#include "runtime_discovery.hpp"
#include "runtime_loop.hpp"
#include "runtime_race.hpp"
#include "hackarena3/errors.hpp"

namespace {

constexpr char kOfficialStreamSuffix[] = "/race.v1.RaceParticipantService/Stream";

void add_metadata(
    grpc::ClientContext& context,
    const std::vector<std::pair<std::string, std::string>>& metadata
) {
    for (const auto& [key, value] : metadata) {
        context.AddMetadata(key, value);
    }
}

std::string status_name(const grpc::Status& status) {
    return std::string(hackarena3::detail::grpc_status_code_name(
        static_cast<grpc::StatusCode>(status.error_code())
    ));
}

void log_startup_diagnostics(const hackarena3::RuntimeConfig& config) {
    const auto parsed_api_target = hackarena3::detail::parse_api_addr(config.api_addr);
    const auto cwd = std::filesystem::current_path().string();
    std::string api_dial_target = "<unresolved>";
    std::string api_resolved_addresses = "<unresolved>";
    try {
        const auto resolved_api_target = hackarena3::detail::resolve_target(parsed_api_target);
        api_dial_target = resolved_api_target.dial_target;
        std::ostringstream joined;
        for (std::size_t index = 0; index < resolved_api_target.resolved_addresses.size(); ++index) {
            if (index != 0) {
                joined << ",";
            }
            joined << resolved_api_target.resolved_addresses[index];
        }
        api_resolved_addresses = joined.str();
    } catch (const std::exception& exc) {
        api_resolved_addresses = std::string("<resolution failed: ") + exc.what() + ">";
    }

    std::cerr << "[ha3-wrapper] Startup config: cwd=" << cwd
              << " api_addr=" << config.api_addr
              << " api_target=" << parsed_api_target.target()
              << " api_dial_target=" << api_dial_target
              << " api_resolved_addresses=" << api_resolved_addresses
              << " sandbox_id="
              << (config.sandbox_id.has_value() ? *config.sandbox_id : "<interactive>")
              << " ha_auth_bin="
              << (config.ha_auth_bin.has_value() ? *config.ha_auth_bin : "<auto>")
              << '\n';
}

std::string official_stream_method(const std::string& rpc_prefix) {
    auto prefix = hackarena3::detail::trim(rpc_prefix);
    while (!prefix.empty() && prefix.back() == '/') {
        prefix.pop_back();
    }
    if (prefix.empty() || prefix == "/") {
        throw hackarena3::RuntimeError(
            "Official rpc_prefix is empty; cannot build stream RPC method."
        );
    }
    if (!prefix.starts_with('/')) {
        prefix.insert(prefix.begin(), '/');
    }
    return prefix + std::string(kOfficialStreamSuffix);
}

}  // namespace

namespace hackarena3::detail {

namespace {

void run_runtime_sandbox(BotProtocol& bot, const RuntimeConfig& config) {
    log_startup_diagnostics(config);
    const auto member_jwt = fetch_member_jwt(config.ha_auth_bin);
    auto broker_api = create_broker_api(config);
    const auto discovered = discover_team_sandboxes(broker_api, member_jwt);
    const auto selected = choose_sandbox(discovered, config.sandbox_id);

    GameTokenProvider token_provider(config.api_addr, member_jwt);
    try {
        token_provider.refresh();
    } catch (const GameTokenError& exc) {
        throw RuntimeError(
            std::string("Failed to obtain game token before LocalSandboxJoin: ") + exc.what()
        );
    }

    auto api = create_backend_api(selected.backend);

    grpc::ClientContext join_context;
    join_context.set_deadline(
        std::chrono::system_clock::now() + std::chrono::seconds(detail::kRpcTimeoutSeconds)
    );
    add_metadata(join_context, race_metadata(token_provider));

    race::v1::LocalSandboxJoinRequest join_request;
    join_request.set_sandbox_id(selected.sandbox_id);

    race::v1::LocalSandboxJoinResponse join_response;
    const auto join_status = api.participant->LocalSandboxJoin(&join_context, join_request, &join_response);
    if (!join_status.ok()) {
        if (join_status.error_code() == grpc::StatusCode::UNIMPLEMENTED) {
            throw RuntimeError(
                "LocalSandboxJoin unavailable (UNIMPLEMENTED). Check backend/api compatibility."
            );
        }
        throw RuntimeError(
            "LocalSandboxJoin failed: " + status_name(join_status) + " " + join_status.error_message()
        );
    }

    const auto track_data = fetch_track_data(api, token_provider, join_response.map_id());
    const auto track_layout = build_track_layout(track_data);
    const auto pit_count =
        track_layout.pitstop.enter.size() + track_layout.pitstop.fix.size() +
        track_layout.pitstop.exit.size();

    std::cerr << "[ha3-wrapper] Loaded track data: map_id=" << track_data.map_id()
              << " samples=" << track_data.centerline_samples_size()
              << " lap_length_m=" << track_data.lap_length_m()
              << " pit_samples=" << pit_count
              << " pit_length_m=" << track_layout.pitstop.length_m << '\n';

    BotContext ctx;
    ctx.car_id = join_response.car_id();
    ctx.map_id = join_response.map_id();
    ctx.car_dimensions = CarDimensions {};
    ctx.requested_hz = detail::kRequestedHz;
    ctx.track = track_layout;
    ctx.effective_hz = std::nullopt;
    ctx.tick = 0;

    run_participant_loop(
        bot,
        api,
        ctx,
        [&token_provider] { return race_metadata(token_provider); },
        &token_provider,
        true
    );
}

void run_runtime_official(BotProtocol& bot, const OfficialRuntimeConfig& config) {
    auto api = create_official_backend_api(config.grpc_target);
    const auto metadata = race_metadata_official(config.team_token, config.auth_token);
    const auto prepare_response = prepare_official_join(api, config.rpc_prefix, metadata);
    const auto track_data = fetch_track_data_official(
        api,
        config.rpc_prefix,
        metadata,
        prepare_response.map_id()
    );
    const auto track_layout = build_track_layout(track_data);
    const auto pit_count =
        track_layout.pitstop.enter.size() + track_layout.pitstop.fix.size() +
        track_layout.pitstop.exit.size();

    std::cerr << "[ha3-wrapper] Official mode prepared: car_id=" << prepare_response.car_id()
              << " map_id=" << prepare_response.map_id()
              << " target=" << config.grpc_target
              << " rpc_prefix=" << config.rpc_prefix
              << " samples=" << track_data.centerline_samples_size()
              << " lap_length_m=" << track_data.lap_length_m()
              << " pit_samples=" << pit_count
              << " pit_length_m=" << track_layout.pitstop.length_m << '\n';

    BotContext ctx;
    ctx.car_id = prepare_response.car_id();
    ctx.map_id = prepare_response.map_id();
    ctx.car_dimensions = CarDimensions {};
    ctx.requested_hz = detail::kRequestedHz;
    ctx.track = track_layout;
    ctx.effective_hz = std::nullopt;
    ctx.tick = 0;

    run_participant_loop(
        bot,
        api,
        ctx,
        [metadata] { return metadata; },
        nullptr,
        false,
        official_stream_method(config.rpc_prefix),
        prepare_response.map_id()
    );
}

}  // namespace

void run_runtime(
    BotProtocol& bot,
    const RuntimeConfig& config,
    const std::optional<OfficialRuntimeConfig>& official_config
) {
    if (official_config.has_value()) {
        run_runtime_official(bot, *official_config);
        return;
    }
    run_runtime_sandbox(bot, config);
}

}  // namespace hackarena3::detail
