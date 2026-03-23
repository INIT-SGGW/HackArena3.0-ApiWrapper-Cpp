#pragma once

#include <grpcpp/grpcpp.h>
#include <grpcpp/impl/proto_utils.h>
#include <grpcpp/impl/client_unary_call.h>
#include <grpcpp/impl/rpc_method.h>

#include <memory>
#include <string>
#include <string_view>

namespace hackarena3::detail {

inline std::string_view grpc_status_code_name(grpc::StatusCode code) {
    switch (code) {
        case grpc::StatusCode::OK:
            return "OK";
        case grpc::StatusCode::CANCELLED:
            return "CANCELLED";
        case grpc::StatusCode::UNKNOWN:
            return "UNKNOWN";
        case grpc::StatusCode::INVALID_ARGUMENT:
            return "INVALID_ARGUMENT";
        case grpc::StatusCode::DEADLINE_EXCEEDED:
            return "DEADLINE_EXCEEDED";
        case grpc::StatusCode::NOT_FOUND:
            return "NOT_FOUND";
        case grpc::StatusCode::ALREADY_EXISTS:
            return "ALREADY_EXISTS";
        case grpc::StatusCode::PERMISSION_DENIED:
            return "PERMISSION_DENIED";
        case grpc::StatusCode::RESOURCE_EXHAUSTED:
            return "RESOURCE_EXHAUSTED";
        case grpc::StatusCode::FAILED_PRECONDITION:
            return "FAILED_PRECONDITION";
        case grpc::StatusCode::ABORTED:
            return "ABORTED";
        case grpc::StatusCode::OUT_OF_RANGE:
            return "OUT_OF_RANGE";
        case grpc::StatusCode::UNIMPLEMENTED:
            return "UNIMPLEMENTED";
        case grpc::StatusCode::INTERNAL:
            return "INTERNAL";
        case grpc::StatusCode::UNAVAILABLE:
            return "UNAVAILABLE";
        case grpc::StatusCode::DATA_LOSS:
            return "DATA_LOSS";
        case grpc::StatusCode::UNAUTHENTICATED:
            return "UNAUTHENTICATED";
        default:
            return "UNKNOWN_STATUS_CODE";
    }
}

template <typename Request, typename Response>
grpc::Status unary_rpc_call(
    const std::shared_ptr<grpc::Channel>& channel,
    grpc::ClientContext& context,
    std::string_view method,
    const Request& request,
    Response& response
) {
    const auto method_name = std::string(method);
    return grpc::internal::BlockingUnaryCall<Request, Response>(
        channel.get(),
        grpc::internal::RpcMethod(
            method_name.c_str(),
            nullptr,
            grpc::internal::RpcMethod::NORMAL_RPC
        ),
        &context,
        request,
        &response
    );
}

}  // namespace hackarena3::detail
