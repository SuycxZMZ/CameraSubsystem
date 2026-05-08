#include "codec_server/recording_session_manager.h"

#include <utility>

namespace camera_subsystem::extensions::codec_server {
namespace {

bool IsValidStreamId(const std::string& stream_id)
{
    if (stream_id.empty() || stream_id.size() > 128)
    {
        return false;
    }
    for (char c : stream_id)
    {
        if (c == '/' || c == '\\' || c == '\0')
        {
            return false;
        }
    }
    return true;
}

} // namespace

RecordingSessionManager::RecordingSessionManager(RecordingSessionConfig config)
    : config_(std::move(config))
{
}

RecordingSessionManager::RecordingSessionPtr
RecordingSessionManager::GetOrCreateSessionLocked(const std::string& stream_id)
{
    auto it = sessions_.find(stream_id);
    if (it != sessions_.end())
    {
        return it->second;
    }

    auto session = std::make_shared<RecordingSession>();
    session->stream_id = stream_id;
    sessions_.emplace(stream_id, session);
    return session;
}

RecordingSessionManager::RecordingSessionPtr
RecordingSessionManager::FindSessionLocked(const std::string& stream_id) const
{
    auto it = sessions_.find(stream_id);
    if (it == sessions_.end())
    {
        return nullptr;
    }
    return it->second;
}

CodecControlStatus RecordingSessionManager::StartRecording(
    const CodecControlRequest& request)
{
    RecordingSessionPtr session;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        session = GetOrCreateSessionLocked(request.stream_id);
    }

    std::lock_guard<std::mutex> lock(session->mutex);

    if (session->state == "recording" || session->state == "starting" ||
        session->state == "stopping")
    {
        return BuildStatusLocked(*session, request, "already_recording");
    }

    if (!IsValidStreamId(request.stream_id))
    {
        session->state = "error";
        session->stream_id = request.stream_id;
        session->last_error = "invalid_stream_id";
        return BuildStatusLocked(*session, request, session->last_error);
    }

    session->state = "starting";
    session->stream_id = request.stream_id;
    session->active_profile = {};
    session->active_profile.fps = request.profile.fps > 0 ? request.profile.fps : config_.fps;
    session->active_profile.bitrate =
        request.profile.bitrate > 0 ? request.profile.bitrate : config_.bitrate;
    session->active_profile.gop = request.profile.gop > 0 ? request.profile.gop : config_.gop;
    session->started_at = std::chrono::steady_clock::now();
    session->last_duration_ms = 0;
    session->encoded_frames.store(0);
    session->dropped_frames.store(0);
    session->input_frames = 0;
    session->decoded_frames.store(0);
    session->decode_failures.store(0);
    session->last_error.clear();
    session->active_container =
        request.container == "mp4" ? ActiveContainer::kMp4 : ActiveContainer::kRawH264;

    session->output_dir =
        request.output_dir.empty() ? config_.default_output_dir : request.output_dir;
    if (session->active_container == ActiveContainer::kRawH264)
    {
        RecordingFileWriterOptions writer_options;
        writer_options.file_extension = ".h264";
        const WriterResult result =
            session->writer.Open(request.stream_id, session->output_dir, writer_options);
        if (result != WriterResult::kOk)
        {
            session->state = "error";
            session->last_error = MapWriterError(result);
            session->file_path.clear();
            return BuildStatusLocked(*session, request, session->last_error);
        }
        session->file_path = session->writer.GetFilePath();
    }
    else
    {
        session->file_path.clear();
    }
    if (config_.enable_camera_subscriber)
    {
        CameraStreamSubscriberConfig subscriber_config = config_.subscriber;
        subscriber_config.client_id = "camera_codec_server_" + request.stream_id;
        std::weak_ptr<RecordingSession> weak_session = session;
        subscriber_config.frame_callback =
            [this, weak_session](const camera_subsystem::ipc::CameraDataFrameHeader& header,
                   const std::vector<uint8_t>& payload) {
                if (auto session = weak_session.lock())
                {
                    HandleInputFrame(*session, header, payload);
                }
            };
        if (!session->subscriber.Start(subscriber_config))
        {
            (void)session->writer.Close();
            (void)session->mp4_writer.Close();
            session->state = "error";
            session->last_error = "stream_not_found";
            return BuildStatusLocked(*session, request, session->last_error);
        }
    }

    session->state = "recording";
    return BuildStatusLocked(*session, request, std::string());
}

CodecControlStatus RecordingSessionManager::StopRecording(
    const CodecControlRequest& request)
{
    RecordingSessionPtr session;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        session = FindSessionLocked(request.stream_id);
    }
    if (!session)
    {
        CodecControlStatus status;
        status.request_id = request.request_id;
        status.stream_id = request.stream_id;
        status.state = "idle";
        status.codec = request.codec.empty() ? "h264" : request.codec;
        status.container = request.container == "mp4" ? "mp4" : "raw_h264";
        status.error = "not_recording";
        return status;
    }

    std::lock_guard<std::mutex> lock(session->mutex);

    if (session->state == "idle")
    {
        return BuildStatusLocked(*session, request, "not_recording");
    }
    if (session->state == "error")
    {
        session->state = "idle";
        return BuildStatusLocked(*session, request, session->last_error);
    }

    session->state = "stopping";
    session->last_duration_ms = GetDurationMsLocked(*session);
    session->subscriber.Stop();
    {
        std::lock_guard<std::mutex> pipeline_lock(session->pipeline_mutex);
        session->h264_encoder.Close();
    }
    if (session->active_container == ActiveContainer::kMp4)
    {
        const Mp4WriterResult close_result = session->mp4_writer.Close();
        if (close_result != Mp4WriterResult::kOk)
        {
            session->state = "error";
            session->last_error = MapMp4WriterError(close_result);
            return BuildStatusLocked(*session, request, session->last_error);
        }
    }
    else
    {
        const WriterResult close_result = session->writer.Close();
        if (close_result != WriterResult::kOk)
        {
            session->state = "error";
            session->last_error = MapWriterError(close_result);
            return BuildStatusLocked(*session, request, session->last_error);
        }
    }

    session->state = "idle";
    return BuildStatusLocked(*session, request, std::string());
}

CodecControlStatus RecordingSessionManager::GetStatus(
    const CodecControlRequest& request) const
{
    RecordingSessionPtr session;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        session = FindSessionLocked(request.stream_id);
    }
    if (!session)
    {
        CodecControlStatus status;
        status.request_id = request.request_id;
        status.stream_id = request.stream_id;
        status.state = "idle";
        status.codec = request.codec.empty() ? "h264" : request.codec;
        status.container = request.container == "mp4" ? "mp4" : "raw_h264";
        return status;
    }

    std::lock_guard<std::mutex> lock(session->mutex);
    return BuildStatusLocked(*session, request, std::string());
}

CodecControlStatus RecordingSessionManager::BuildStatusLocked(
    RecordingSession& session,
    const CodecControlRequest& request,
    const std::string& error) const
{
    const CameraStreamSubscriberStats subscriber_stats = session.subscriber.GetStats();
    std::lock_guard<std::mutex> pipeline_lock(session.pipeline_mutex);
    const WriterStats stats = GetActiveWriterStatsLocked(session);
    CodecControlStatus status;
    status.request_id = request.request_id;
    status.stream_id = session.stream_id.empty() ? request.stream_id : session.stream_id;
    status.recording = session.state == "recording";
    status.state = session.state;
    status.codec = request.codec.empty() ? "h264" : request.codec;
    status.container = GetActiveContainerName(session);
    status.file = session.file_path;
    status.encoded_frames = session.encoded_frames.load();
    status.decoded_frames = session.decoded_frames.load();
    status.dropped_frames = session.dropped_frames.load();
    status.input_frames = config_.enable_camera_subscriber
                              ? subscriber_stats.input_frames
                              : session.input_frames;
    status.duration_ms = GetDurationMsLocked(session);
    status.bytes_written = stats.bytes_written;
    status.packets_written = stats.packets_written;
    status.decode_failures = session.decode_failures.load();
    status.write_failures = stats.write_failures + subscriber_stats.read_failures;
    status.error = error;
    status.profile = session.active_profile;
    return status;
}

void RecordingSessionManager::HandleInputFrame(
    RecordingSession& session,
    const camera_subsystem::ipc::CameraDataFrameHeader& header,
    const std::vector<uint8_t>& payload)
{
    (void)header;

    std::lock_guard<std::mutex> pipeline_lock(session.pipeline_mutex);

    DecodedImageFrame decoded;
    const JpegDecodeResult decode_result =
        session.jpeg_decoder.Decode(payload.data(), payload.size(), &decoded);
    if (decode_result == JpegDecodeResult::kOk)
    {
        session.decoded_frames.fetch_add(1);
    }
    else
    {
        session.decode_failures.fetch_add(1);
        return;
    }

    if (!session.h264_encoder.IsOpen())
    {
        const H264EncodeResult open_result =
            session.h264_encoder.Open(BuildEncoderConfig(session, decoded));
        if (open_result != H264EncodeResult::kOk)
        {
            session.dropped_frames.fetch_add(1);
            return;
        }
    }

    std::vector<EncodedPacket> packets;
    const H264EncodeResult encode_result = session.h264_encoder.EncodeFrame(decoded, &packets);
    if (encode_result != H264EncodeResult::kOk)
    {
        session.dropped_frames.fetch_add(1);
        return;
    }

    for (const EncodedPacket& packet : packets)
    {
        if (session.active_container == ActiveContainer::kMp4 &&
            !EnsureMp4WriterOpenLocked(session, decoded))
        {
            session.dropped_frames.fetch_add(1);
            return;
        }
        if (!WritePacketLocked(session, packet))
        {
            session.dropped_frames.fetch_add(1);
            return;
        }
    }
    session.encoded_frames.fetch_add(1);
}

H264EncoderConfig RecordingSessionManager::BuildEncoderConfig(
    const RecordingSession& session,
    const DecodedImageFrame& frame) const
{
    H264EncoderConfig config;
    config.width = frame.width;
    config.height = frame.height;
    config.hor_stride = frame.hor_stride;
    config.ver_stride = frame.ver_stride;
    config.fps = session.active_profile.fps > 0 ? session.active_profile.fps : config_.fps;
    config.bitrate =
        session.active_profile.bitrate > 0 ? session.active_profile.bitrate : config_.bitrate;
    config.gop = session.active_profile.gop > 0 ? session.active_profile.gop : config_.gop;
    return config;
}

WriterStats RecordingSessionManager::GetActiveWriterStatsLocked(
    const RecordingSession& session) const
{
    if (session.active_container == ActiveContainer::kMp4)
    {
        return session.mp4_writer.GetStats();
    }
    return session.writer.GetStats();
}

std::string RecordingSessionManager::GetActiveContainerName(const RecordingSession& session)
{
    return session.active_container == ActiveContainer::kMp4 ? "mp4" : "raw_h264";
}

bool RecordingSessionManager::EnsureMp4WriterOpenLocked(RecordingSession& session,
                                                        const DecodedImageFrame& frame)
{
    if (!session.file_path.empty())
    {
        return true;
    }

    Mp4WriterConfig mp4_config;
    mp4_config.width = frame.width;
    mp4_config.height = frame.height;
    mp4_config.fps = session.active_profile.fps > 0 ? session.active_profile.fps : config_.fps;
    const Mp4WriterResult result =
        session.mp4_writer.Open(session.stream_id, session.output_dir, mp4_config);
    if (result != Mp4WriterResult::kOk)
    {
        session.last_error = MapMp4WriterError(result);
        return false;
    }
    session.file_path = session.mp4_writer.GetFilePath();
    return true;
}

bool RecordingSessionManager::WritePacketLocked(RecordingSession& session,
                                                const EncodedPacket& packet)
{
    if (session.active_container == ActiveContainer::kMp4)
    {
        const Mp4WriterResult write_result =
            session.mp4_writer.WriteAnnexBPacket(packet.payload.data(), packet.payload.size());
        if (write_result != Mp4WriterResult::kOk)
        {
            session.last_error = MapMp4WriterError(write_result);
            return false;
        }
        return true;
    }

    const WriterResult write_result =
        session.writer.Write(packet.payload.data(), packet.payload.size());
    if (write_result != WriterResult::kOk)
    {
        session.last_error = MapWriterError(write_result);
        return false;
    }
    return true;
}

uint64_t RecordingSessionManager::GetDurationMsLocked(const RecordingSession& session)
{
    if (session.started_at == std::chrono::steady_clock::time_point{})
    {
        return session.last_duration_ms;
    }
    if (session.state == "recording" ||
        session.state == "starting" ||
        session.state == "stopping")
    {
        const auto elapsed = std::chrono::steady_clock::now() - session.started_at;
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
    }
    return session.last_duration_ms;
}

std::string RecordingSessionManager::MapWriterError(WriterResult result)
{
    switch (result)
    {
    case WriterResult::kOk:
        return std::string();
    case WriterResult::kOutputDirNotWritable:
        return "output_dir_not_writable";
    case WriterResult::kInvalidStreamId:
        return "invalid_stream_id";
    case WriterResult::kFileCreateFailed:
        return "recording_file_create_failed";
    case WriterResult::kFileNotOpen:
    case WriterResult::kRecordingIoError:
        return "recording_io_error";
    }
    return "recording_io_error";
}

std::string RecordingSessionManager::MapMp4WriterError(Mp4WriterResult result)
{
    switch (result)
    {
    case Mp4WriterResult::kOk:
        return std::string();
    case Mp4WriterResult::kOutputDirNotWritable:
        return "output_dir_not_writable";
    case Mp4WriterResult::kInvalidStreamId:
        return "invalid_stream_id";
    case Mp4WriterResult::kFileCreateFailed:
        return "recording_file_create_failed";
    case Mp4WriterResult::kMissingParameterSets:
        return "missing_h264_parameter_sets";
    case Mp4WriterResult::kInvalidPacket:
        return "invalid_h264_packet";
    case Mp4WriterResult::kFileNotOpen:
    case Mp4WriterResult::kRecordingIoError:
        return "recording_io_error";
    }
    return "recording_io_error";
}

} // namespace camera_subsystem::extensions::codec_server
