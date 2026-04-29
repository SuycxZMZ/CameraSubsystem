#include "web_preview/gateway_config.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace web_preview {
namespace {

bool ParseUint16(const std::string& value, uint16_t* out)
{
    if (!out)
    {
        return false;
    }
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0' ||
        parsed > static_cast<unsigned long>(std::numeric_limits<uint16_t>::max()))
    {
        return false;
    }
    *out = static_cast<uint16_t>(parsed);
    return true;
}

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

} // namespace

void PrintUsage(const char* program_name)
{
    std::cout
        << "Usage: " << program_name << " [options]\n"
        << "\nOptions:\n"
        << "  --bind <host>             Bind address, default 0.0.0.0\n"
        << "  --port <port>             HTTP/WebSocket port, default 8080\n"
        << "  --control-socket <path>   Camera control socket path\n"
        << "  --data-socket <path>      Camera data socket path\n"
        << "  --codec-socket <path>     Codec server control socket path\n"
        << "  --device <path>           Camera device path requested via control IPC\n"
        << "  --static-root <path>      Frontend dist directory\n"
        << "  --client-id <id>          Control IPC client id\n"
        << "  --output-dir <path>       Recording output directory\n"
        << "  --camera-id <id>          Camera id, default 0\n"
        << "  --max-fps <fps>           Preview max fps, default 15\n"
        << "  --help                    Show this help\n";
}

bool ParseGatewayConfig(int argc, char* argv[], GatewayConfig* config)
{
    if (!config)
    {
        return false;
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
            PrintUsage(argv[0]);
            return false;
        }
        if (arg == "--bind")
        {
            if (!require_value(&config->bind_host))
            {
                return false;
            }
        }
        else if (arg == "--port")
        {
            if (!require_value(&value) || !ParseUint16(value, &config->http_port))
            {
                std::cerr << "invalid --port value\n";
                return false;
            }
        }
        else if (arg == "--control-socket")
        {
            if (!require_value(&config->control_socket))
            {
                return false;
            }
        }
        else if (arg == "--data-socket")
        {
            if (!require_value(&config->data_socket))
            {
                return false;
            }
        }
        else if (arg == "--codec-socket")
        {
            if (!require_value(&config->codec_socket))
            {
                return false;
            }
        }
        else if (arg == "--device")
        {
            if (!require_value(&config->device_path))
            {
                return false;
            }
        }
        else if (arg == "--static-root")
        {
            if (!require_value(&config->static_root))
            {
                return false;
            }
        }
        else if (arg == "--client-id")
        {
            if (!require_value(&config->client_id))
            {
                return false;
            }
        }
        else if (arg == "--output-dir")
        {
            if (!require_value(&config->output_dir))
            {
                return false;
            }
        }
        else if (arg == "--camera-id")
        {
            if (!require_value(&value) || !ParseUint32(value, &config->camera_id))
            {
                std::cerr << "invalid --camera-id value\n";
                return false;
            }
        }
        else if (arg == "--max-fps")
        {
            if (!require_value(&value) || !ParseUint32(value, &config->max_preview_fps) ||
                config->max_preview_fps == 0)
            {
                std::cerr << "invalid --max-fps value\n";
                return false;
            }
        }
        else
        {
            std::cerr << "unknown argument: " << arg << "\n";
            return false;
        }
    }

    return true;
}

} // namespace web_preview
