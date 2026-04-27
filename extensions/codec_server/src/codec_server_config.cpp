#include "codec_server/codec_server_config.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace camera_subsystem::extensions::codec_server {
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

bool IsSupportedInputFormat(const std::string& value)
{
    return value == "auto" || value == "mjpeg" || value == "jpeg" ||
           value == "yuyv" || value == "nv12";
}

bool IsSupportedDataPlane(const std::string& value)
{
    return value == "v1" || value == "v2";
}

} // namespace

void PrintCodecServerUsage(const char* program_name)
{
    std::cout
        << "Usage: " << program_name << " [options]\n"
        << "\nOptions:\n"
        << "  --control-socket <path>   Camera control socket path\n"
        << "  --data-socket <path>      Camera data socket path\n"
        << "  --release-socket <path>   DataPlaneV2 release socket path\n"
        << "  --codec-socket <path>     Codec control socket path\n"
        << "  --device <path>           Camera subscribe request device path, default /dev/video45\n"
        << "  --camera-id <id>          Camera stream id, default default_camera\n"
        << "  --output-dir <path>       Recording output directory\n"
        << "  --input-format <format>   auto|mjpeg|jpeg|yuyv|nv12, default auto\n"
        << "  --codec <codec>           h264, default h264\n"
        << "  --data-plane <version>    v1|v2, default v1\n"
        << "  --width <pixels>          Encode width, default 1920\n"
        << "  --height <pixels>         Encode height, default 1080\n"
        << "  --fps <fps>               Target fps, default 30\n"
        << "  --bitrate <bps>           Target bitrate, default 4000000\n"
        << "  --gop <frames>            GOP length, default 60\n"
        << "  --help                    Show this help\n";
}

ParseResult ParseCodecServerConfig(int argc, char* argv[], CodecServerConfig* config)
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
            PrintCodecServerUsage(argv[0]);
            return ParseResult::kHelp;
        }
        if (arg == "--control-socket")
        {
            if (!require_value(&config->control_socket))
            {
                return ParseResult::kError;
            }
        }
        else if (arg == "--data-socket")
        {
            if (!require_value(&config->data_socket))
            {
                return ParseResult::kError;
            }
        }
        else if (arg == "--release-socket")
        {
            if (!require_value(&config->release_socket))
            {
                return ParseResult::kError;
            }
        }
        else if (arg == "--codec-socket")
        {
            if (!require_value(&config->codec_socket))
            {
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
        else if (arg == "--camera-id")
        {
            if (!require_value(&config->camera_id))
            {
                return ParseResult::kError;
            }
        }
        else if (arg == "--output-dir")
        {
            if (!require_value(&config->output_dir))
            {
                return ParseResult::kError;
            }
        }
        else if (arg == "--input-format")
        {
            if (!require_value(&config->input_format) ||
                !IsSupportedInputFormat(config->input_format))
            {
                std::cerr << "invalid --input-format value\n";
                return ParseResult::kError;
            }
        }
        else if (arg == "--codec")
        {
            if (!require_value(&config->codec) || config->codec != "h264")
            {
                std::cerr << "invalid --codec value\n";
                return ParseResult::kError;
            }
        }
        else if (arg == "--data-plane")
        {
            if (!require_value(&config->data_plane) ||
                !IsSupportedDataPlane(config->data_plane))
            {
                std::cerr << "invalid --data-plane value\n";
                return ParseResult::kError;
            }
        }
        else if (arg == "--width")
        {
            if (!require_value(&value) || !ParseUint32(value, &config->width) ||
                config->width == 0)
            {
                std::cerr << "invalid --width value\n";
                return ParseResult::kError;
            }
        }
        else if (arg == "--height")
        {
            if (!require_value(&value) || !ParseUint32(value, &config->height) ||
                config->height == 0)
            {
                std::cerr << "invalid --height value\n";
                return ParseResult::kError;
            }
        }
        else if (arg == "--fps")
        {
            if (!require_value(&value) || !ParseUint32(value, &config->fps) ||
                config->fps == 0)
            {
                std::cerr << "invalid --fps value\n";
                return ParseResult::kError;
            }
        }
        else if (arg == "--bitrate")
        {
            if (!require_value(&value) || !ParseUint32(value, &config->bitrate) ||
                config->bitrate == 0)
            {
                std::cerr << "invalid --bitrate value\n";
                return ParseResult::kError;
            }
        }
        else if (arg == "--gop")
        {
            if (!require_value(&value) || !ParseUint32(value, &config->gop) ||
                config->gop == 0)
            {
                std::cerr << "invalid --gop value\n";
                return ParseResult::kError;
            }
        }
        else
        {
            std::cerr << "unknown argument: " << arg << "\n";
            return ParseResult::kError;
        }
    }

    return ParseResult::kOk;
}

} // namespace camera_subsystem::extensions::codec_server
