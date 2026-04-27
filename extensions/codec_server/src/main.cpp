#include "codec_server/codec_server_app.h"
#include "codec_server/codec_server_config.h"

#include <utility>

int main(int argc, char* argv[])
{
    camera_subsystem::extensions::codec_server::CodecServerConfig config;
    const auto parse_result =
        camera_subsystem::extensions::codec_server::ParseCodecServerConfig(
            argc, argv, &config);

    if (parse_result == camera_subsystem::extensions::codec_server::ParseResult::kHelp)
    {
        return 0;
    }
    if (parse_result == camera_subsystem::extensions::codec_server::ParseResult::kError)
    {
        return 1;
    }

    camera_subsystem::extensions::codec_server::CodecServerApp app(std::move(config));
    return app.Run();
}
