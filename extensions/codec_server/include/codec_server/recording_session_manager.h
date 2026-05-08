#ifndef CODEC_SERVER_RECORDING_SESSION_MANAGER_H
#define CODEC_SERVER_RECORDING_SESSION_MANAGER_H

#include "codec_server/codec_control_protocol.h"
#include "codec_server/camera_stream_subscriber.h"
#include "codec_server/h264_mpp_encoder.h"
#include "codec_server/jpeg_decode_stage.h"
#include "codec_server/mp4_file_writer.h"
#include "codec_server/recording_file_writer.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "camera_subsystem/ipc/camera_data_ipc.h"

namespace camera_subsystem::extensions::codec_server {

struct RecordingSessionConfig
{
    std::string default_output_dir = "/home/luckfox/CameraSubsystem/recordings";
    CameraStreamSubscriberConfig subscriber;
    bool enable_camera_subscriber = false;
    uint32_t fps = 30;
    uint32_t bitrate = 4000000;
    uint32_t gop = 60;
};

class RecordingSessionManager
{
public:
    explicit RecordingSessionManager(RecordingSessionConfig config);

    CodecControlStatus StartRecording(const CodecControlRequest& request);
    CodecControlStatus StopRecording(const CodecControlRequest& request);
    CodecControlStatus GetStatus(const CodecControlRequest& request) const;

private:
    enum class ActiveContainer
    {
        kRawH264,
        kMp4,
    };

    struct RecordingSession
    {
        mutable std::mutex mutex;
        mutable std::mutex pipeline_mutex;
        RecordingFileWriter writer;
        Mp4FileWriter mp4_writer;
        CameraStreamSubscriber subscriber;
        JpegDecodeStage jpeg_decoder;
        H264MppEncoder h264_encoder;
        std::string state = "idle";
        std::string stream_id;
        std::string file_path;
        std::string output_dir;
        ActiveContainer active_container = ActiveContainer::kRawH264;
        CodecControlProfile active_profile;
        std::chrono::steady_clock::time_point started_at{};
        uint64_t last_duration_ms = 0;
        std::atomic<uint64_t> encoded_frames{0};
        std::atomic<uint64_t> dropped_frames{0};
        uint64_t input_frames = 0;
        std::atomic<uint64_t> decoded_frames{0};
        std::atomic<uint64_t> decode_failures{0};
        std::string last_error;
    };

    using RecordingSessionPtr = std::shared_ptr<RecordingSession>;

    RecordingSessionPtr GetOrCreateSessionLocked(const std::string& stream_id);
    RecordingSessionPtr FindSessionLocked(const std::string& stream_id) const;
    CodecControlStatus BuildStatusLocked(RecordingSession& session,
                                         const CodecControlRequest& request,
                                         const std::string& error) const;
    void HandleInputFrame(RecordingSession& session,
                          const camera_subsystem::ipc::CameraDataFrameHeader& header,
                          const std::vector<uint8_t>& payload);
    H264EncoderConfig BuildEncoderConfig(const RecordingSession& session,
                                         const DecodedImageFrame& frame) const;
    WriterStats GetActiveWriterStatsLocked(const RecordingSession& session) const;
    static std::string GetActiveContainerName(const RecordingSession& session);
    bool EnsureMp4WriterOpenLocked(RecordingSession& session, const DecodedImageFrame& frame);
    bool WritePacketLocked(RecordingSession& session, const EncodedPacket& packet);
    static uint64_t GetDurationMsLocked(const RecordingSession& session);
    static std::string MapWriterError(WriterResult result);
    static std::string MapMp4WriterError(Mp4WriterResult result);

    RecordingSessionConfig config_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, RecordingSessionPtr> sessions_;
};

} // namespace camera_subsystem::extensions::codec_server

#endif // CODEC_SERVER_RECORDING_SESSION_MANAGER_H
