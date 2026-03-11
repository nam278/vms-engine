#pragma once

#include "engine/core/config/config_types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace engine::core::pipeline {

enum class RuntimeSourceOperation {
    List,
    Add,
    Remove,
};

enum class RuntimeSourceErrorCode {
    None,
    InvalidRequest,
    PipelineNotFound,
    UnsupportedSourceMode,
    DuplicateCameraId,
    CameraNotFound,
    MaxSourcesReached,
    BuildSourceFailed,
    RequestPadFailed,
    LinkSourceFailed,
    OperationTimeout,
    DotDumpFailed,
    InternalError,
};

struct RuntimeSourceInfo {
    std::string camera_id;
    std::string uri;
    uint32_t source_index = 0;
    bool is_seeded = false;
    std::string state = "active";
};

struct RuntimeSourceMutationRequest {
    RuntimeSourceOperation operation = RuntimeSourceOperation::List;
    std::string camera_id;
    std::optional<engine::core::config::CameraConfig> camera;
};

struct RuntimeSourceMutationResult {
    bool success = false;
    int http_status = 500;
    RuntimeSourceErrorCode error_code = RuntimeSourceErrorCode::InternalError;
    std::string message;
    std::string camera_id;
    int source_index = -1;
    int active_source_count = 0;
    std::string dot_file_path;
    std::string dot_dump_warning;
    std::vector<RuntimeSourceInfo> sources;
    std::optional<RuntimeSourceInfo> source;
};

inline const char* to_string(RuntimeSourceOperation operation) {
    switch (operation) {
        case RuntimeSourceOperation::List:
            return "list_sources";
        case RuntimeSourceOperation::Add:
            return "add_source";
        case RuntimeSourceOperation::Remove:
            return "remove_source";
        default:
            return "unknown";
    }
}

inline const char* to_string(RuntimeSourceErrorCode code) {
    switch (code) {
        case RuntimeSourceErrorCode::None:
            return "SRCCTL_NONE";
        case RuntimeSourceErrorCode::InvalidRequest:
            return "SRCCTL_INVALID_REQUEST";
        case RuntimeSourceErrorCode::PipelineNotFound:
            return "SRCCTL_PIPELINE_NOT_FOUND";
        case RuntimeSourceErrorCode::UnsupportedSourceMode:
            return "SRCCTL_UNSUPPORTED_SOURCE_MODE";
        case RuntimeSourceErrorCode::DuplicateCameraId:
            return "SRCCTL_DUPLICATE_CAMERA_ID";
        case RuntimeSourceErrorCode::CameraNotFound:
            return "SRCCTL_CAMERA_NOT_FOUND";
        case RuntimeSourceErrorCode::MaxSourcesReached:
            return "SRCCTL_MAX_SOURCES_REACHED";
        case RuntimeSourceErrorCode::BuildSourceFailed:
            return "SRCCTL_BUILD_SOURCE_FAILED";
        case RuntimeSourceErrorCode::RequestPadFailed:
            return "SRCCTL_REQUEST_PAD_FAILED";
        case RuntimeSourceErrorCode::LinkSourceFailed:
            return "SRCCTL_LINK_SOURCE_FAILED";
        case RuntimeSourceErrorCode::OperationTimeout:
            return "SRCCTL_OPERATION_TIMEOUT";
        case RuntimeSourceErrorCode::DotDumpFailed:
            return "SRCCTL_DOT_DUMP_FAILED";
        case RuntimeSourceErrorCode::InternalError:
            return "SRCCTL_INTERNAL_ERROR";
        default:
            return "SRCCTL_INTERNAL_ERROR";
    }
}

inline const char* to_error_name(RuntimeSourceErrorCode code) {
    switch (code) {
        case RuntimeSourceErrorCode::None:
            return "none";
        case RuntimeSourceErrorCode::InvalidRequest:
            return "invalid_request";
        case RuntimeSourceErrorCode::PipelineNotFound:
            return "pipeline_not_found";
        case RuntimeSourceErrorCode::UnsupportedSourceMode:
            return "unsupported_source_mode";
        case RuntimeSourceErrorCode::DuplicateCameraId:
            return "duplicate_camera_id";
        case RuntimeSourceErrorCode::CameraNotFound:
            return "camera_not_found";
        case RuntimeSourceErrorCode::MaxSourcesReached:
            return "max_sources_reached";
        case RuntimeSourceErrorCode::BuildSourceFailed:
            return "build_source_failed";
        case RuntimeSourceErrorCode::RequestPadFailed:
            return "request_pad_failed";
        case RuntimeSourceErrorCode::LinkSourceFailed:
            return "link_source_failed";
        case RuntimeSourceErrorCode::OperationTimeout:
            return "operation_timeout";
        case RuntimeSourceErrorCode::DotDumpFailed:
            return "dot_dump_failed";
        case RuntimeSourceErrorCode::InternalError:
            return "internal_error";
        default:
            return "internal_error";
    }
}

}  // namespace engine::core::pipeline