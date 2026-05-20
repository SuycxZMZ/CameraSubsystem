#include "detection_server/detection_config.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace camera_subsystem::extensions::detection_server {
namespace {

bool ParseUint32(const std::string& value, uint32_t* out)
{
    if (!out)
    {
        return false;
    }

    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0' ||
        parsed > static_cast<unsigned long>(std::numeric_limits<uint32_t>::max()))
    {
        return false;
    }

    *out = static_cast<uint32_t>(parsed);
    return true;
}

bool ParseDouble(const std::string& value, double* out)
{
    if (!out)
    {
        return false;
    }

    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0')
    {
        return false;
    }

    *out = parsed;
    return true;
}

bool ParseBoolFlag(const std::string& value, bool* out)
{
    if (!out)
    {
        return false;
    }

    if (value == "1" || value == "true")
    {
        *out = true;
        return true;
    }
    if (value == "0" || value == "false")
    {
        *out = false;
        return true;
    }

    return false;
}

} // namespace

bool DetectionServerConfig::IsValid(std::string* reason) const
{
    auto fail = [&](const char* message) -> bool
    {
        if (reason)
        {
            *reason = message;
        }
        return false;
    };

    if (stream_id.empty())
    {
        return fail("stream_id must not be empty");
    }
    if (model_path.empty())
    {
        return fail("model_path must not be empty");
    }
    if (labels_path.empty())
    {
        return fail("labels_path must not be empty");
    }
    if (!IsSupportedCoreMask(npu_core_mask))
    {
        return fail("npu_core_mask must be one of 1,2,3,4,7");
    }
    if (score_threshold < 0.0 || score_threshold > 1.0)
    {
        return fail("score_threshold must be in [0.0, 1.0]");
    }
    if (nms_threshold < 0.0 || nms_threshold > 1.0)
    {
        return fail("nms_threshold must be in [0.0, 1.0]");
    }
    if (infer_every_n_frames == 0)
    {
        return fail("infer_every_n_frames must be >= 1");
    }
    if (max_queue_depth < 1 || max_queue_depth > 4)
    {
        return fail("max_queue_depth must be in [1, 4]");
    }
    if (metrics_interval_ms == 0)
    {
        return fail("metrics_interval_ms must be >= 1");
    }
    if (performance_profile == PerformanceProfile::kFull)
    {
        return fail("performance_profile=full is not supported in phase 1");
    }

    if (reason)
    {
        reason->clear();
    }
    return true;
}

void PrintDetectionServerUsage(const char* program_name)
{
    std::cout
        << "Usage: " << program_name << " [options]\n"
        << "\nOptions:\n"
        << "  --control-socket <path>   Detection control socket path\n"
        << "  --result-socket <path>    Detection result socket path\n"
        << "  --camera-control-socket <path> Camera publisher control socket path\n"
        << "  --camera-data-socket <path> Camera publisher data socket path\n"
        << "  --stream-id <id>          Camera stream id, default default0\n"
        << "  --device <path>           Camera device path, default /dev/video45\n"
        << "  --client-id <id>          Detection subscriber client id\n"
        << "  --camera-id <id>          Camera endpoint numeric id\n"
        << "  --model-path <path>       RKNN model path\n"
        << "  --labels-path <path>      Labels path\n"
        << "  --npu-core-mask <mask>    1|2|3|4|7, default 1\n"
        << "  --score-threshold <f>     [0.0, 1.0], default 0.25\n"
        << "  --nms-threshold <f>       [0.0, 1.0], default 0.45\n"
        << "  --output-mode <mode>      metadata_only|metadata_and_annotated_frame\n"
        << "  --infer-every-n-frames <n>  >=1, default 1\n"
        << "  --draw-boxes <0|1>        default 1\n"
        << "  --max-queue-depth <n>     1..4, default 2\n"
        << "  --max-frame-size <bytes>  default 67108864\n"
        << "  --performance-profile <profile> none|npu|npu-cpu, default npu-cpu\n"
        << "  --allow-performance-profile-failure <0|1>  default 0\n"
        << "  --metrics-interval-ms <n> default 1000\n"
        << "  --help                    Show this help\n";
}

ParseResult ParseDetectionServerConfig(int argc, char* argv[], DetectionServerConfig* config)
{
    if (!config)
    {
        return ParseResult::kError;
    }

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        auto require_value = [&](std::string* value) -> bool
        {
            if (i + 1 >= argc)
            {
                std::cerr << "missing value for " << arg << "\n";
                return false;
            }
            *value = argv[++i];
            return true;
        };

        std::string value;
        if (arg == "--help")
        {
            PrintDetectionServerUsage(argv[0]);
            return ParseResult::kHelp;
        }
        if (arg == "--control-socket")
        {
            if (!require_value(&config->control_socket))
            {
                return ParseResult::kError;
            }
        }
        else if (arg == "--result-socket")
        {
            if (!require_value(&config->result_socket))
            {
                return ParseResult::kError;
            }
        }
        else if (arg == "--camera-control-socket")
        {
            if (!require_value(&config->camera_control_socket))
            {
                return ParseResult::kError;
            }
        }
        else if (arg == "--camera-data-socket")
        {
            if (!require_value(&config->camera_data_socket))
            {
                return ParseResult::kError;
            }
        }
        else if (arg == "--stream-id")
        {
            if (!require_value(&config->stream_id) || config->stream_id.empty())
            {
                std::cerr << "invalid --stream-id value\n";
                return ParseResult::kError;
            }
        }
        else if (arg == "--device")
        {
            if (!require_value(&config->device_path))
            {
                return ParseResult::kError;
            }
        }
        else if (arg == "--client-id")
        {
            if (!require_value(&config->client_id) || config->client_id.empty())
            {
                std::cerr << "invalid --client-id value\n";
                return ParseResult::kError;
            }
        }
        else if (arg == "--camera-id")
        {
            if (!require_value(&value) || !ParseUint32(value, &config->camera_id))
            {
                std::cerr << "invalid --camera-id value\n";
                return ParseResult::kError;
            }
        }
        else if (arg == "--model-path")
        {
            if (!require_value(&config->model_path))
            {
                return ParseResult::kError;
            }
        }
        else if (arg == "--labels-path")
        {
            if (!require_value(&config->labels_path))
            {
                return ParseResult::kError;
            }
        }
        else if (arg == "--npu-core-mask")
        {
            if (!require_value(&value) || !ParseUint32(value, &config->npu_core_mask))
            {
                std::cerr << "invalid --npu-core-mask value\n";
                return ParseResult::kError;
            }
        }
        else if (arg == "--score-threshold")
        {
            if (!require_value(&value) || !ParseDouble(value, &config->score_threshold))
            {
                std::cerr << "invalid --score-threshold value\n";
                return ParseResult::kError;
            }
        }
        else if (arg == "--nms-threshold")
        {
            if (!require_value(&value) || !ParseDouble(value, &config->nms_threshold))
            {
                std::cerr << "invalid --nms-threshold value\n";
                return ParseResult::kError;
            }
        }
        else if (arg == "--output-mode")
        {
            if (!require_value(&value) || !ParseDetectionOutputMode(value, &config->output_mode))
            {
                std::cerr << "invalid --output-mode value\n";
                return ParseResult::kError;
            }
        }
        else if (arg == "--infer-every-n-frames")
        {
            if (!require_value(&value) || !ParseUint32(value, &config->infer_every_n_frames))
            {
                std::cerr << "invalid --infer-every-n-frames value\n";
                return ParseResult::kError;
            }
        }
        else if (arg == "--draw-boxes")
        {
            if (!require_value(&value) || !ParseBoolFlag(value, &config->draw_boxes))
            {
                std::cerr << "invalid --draw-boxes value\n";
                return ParseResult::kError;
            }
        }
        else if (arg == "--max-queue-depth")
        {
            if (!require_value(&value) || !ParseUint32(value, &config->max_queue_depth))
            {
                std::cerr << "invalid --max-queue-depth value\n";
                return ParseResult::kError;
            }
        }
        else if (arg == "--max-frame-size")
        {
            if (!require_value(&value) || !ParseUint32(value, &config->max_frame_size) ||
                config->max_frame_size == 0)
            {
                std::cerr << "invalid --max-frame-size value\n";
                return ParseResult::kError;
            }
        }
        else if (arg == "--performance-profile")
        {
            if (!require_value(&value) ||
                !ParsePerformanceProfile(value, &config->performance_profile))
            {
                std::cerr << "invalid --performance-profile value\n";
                return ParseResult::kError;
            }
        }
        else if (arg == "--allow-performance-profile-failure")
        {
            if (!require_value(&value) ||
                !ParseBoolFlag(value, &config->allow_performance_profile_failure))
            {
                std::cerr << "invalid --allow-performance-profile-failure value\n";
                return ParseResult::kError;
            }
        }
        else if (arg == "--metrics-interval-ms")
        {
            if (!require_value(&value) || !ParseUint32(value, &config->metrics_interval_ms))
            {
                std::cerr << "invalid --metrics-interval-ms value\n";
                return ParseResult::kError;
            }
        }
        else
        {
            std::cerr << "unknown argument: " << arg << "\n";
            return ParseResult::kError;
        }
    }

    std::string reason;
    if (!config->IsValid(&reason))
    {
        std::cerr << "invalid detection server config: " << reason << "\n";
        return ParseResult::kError;
    }

    return ParseResult::kOk;
}

} // namespace camera_subsystem::extensions::detection_server
