#ifndef CODEC_SERVER_CODEC_SERVER_APP_H
#define CODEC_SERVER_CODEC_SERVER_APP_H

#include "codec_server/codec_server_config.h"
#include "codec_server/recording_session_manager.h"

namespace camera_subsystem::extensions::codec_server {

class CodecServerApp
{
public:
    explicit CodecServerApp(CodecServerConfig config);

    int Run();

private:
    CodecServerConfig config_;
    RecordingSessionManager session_manager_;
};

} // namespace camera_subsystem::extensions::codec_server

#endif // CODEC_SERVER_CODEC_SERVER_APP_H
