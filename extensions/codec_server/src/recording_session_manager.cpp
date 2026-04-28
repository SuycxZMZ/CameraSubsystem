#include "codec_server/recording_session_manager.h"

#include <utility>

namespace camera_subsystem::extensions::codec_server {

RecordingSessionManager::RecordingSessionManager(RecordingSessionConfig config)
    : config_(std::move(config))
{
}

CodecControlStatus RecordingSessionManager::StartRecording(
    const CodecControlRequest& request)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ == "recording" || state_ == "starting" || state_ == "stopping")
    {
        return BuildStatusLocked(request, "already_recording");
    }

    state_ = "starting";
    stream_id_ = request.stream_id;
    encoded_frames_ = 0;
    dropped_frames_ = 0;
    input_frames_ = 0;
    last_error_.clear();

    const std::string output_dir =
        request.output_dir.empty() ? config_.default_output_dir : request.output_dir;
    const WriterResult result = writer_.Open(request.stream_id, output_dir);
    if (result != WriterResult::kOk)
    {
        state_ = "error";
        last_error_ = MapWriterError(result);
        file_path_.clear();
        return BuildStatusLocked(request, last_error_);
    }

    file_path_ = writer_.GetFilePath();
    if (config_.enable_camera_subscriber)
    {
        CameraStreamSubscriberConfig subscriber_config = config_.subscriber;
        subscriber_config.client_id = "camera_codec_server_" + request.stream_id;
        if (!subscriber_.Start(subscriber_config))
        {
            (void)writer_.Close();
            state_ = "error";
            last_error_ = "stream_not_found";
            return BuildStatusLocked(request, last_error_);
        }
    }

    state_ = "recording";
    return BuildStatusLocked(request, std::string());
}

CodecControlStatus RecordingSessionManager::StopRecording(
    const CodecControlRequest& request)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ == "idle")
    {
        return BuildStatusLocked(request, "not_recording");
    }
    if (state_ == "error")
    {
        state_ = "idle";
        return BuildStatusLocked(request, last_error_);
    }

    state_ = "stopping";
    subscriber_.Stop();
    const WriterResult close_result = writer_.Close();
    if (close_result != WriterResult::kOk)
    {
        state_ = "error";
        last_error_ = MapWriterError(close_result);
        return BuildStatusLocked(request, last_error_);
    }

    state_ = "idle";
    return BuildStatusLocked(request, std::string());
}

CodecControlStatus RecordingSessionManager::GetStatus(
    const CodecControlRequest& request) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return BuildStatusLocked(request, std::string());
}

CodecControlStatus RecordingSessionManager::BuildStatusLocked(
    const CodecControlRequest& request,
    const std::string& error) const
{
    const WriterStats stats = writer_.GetStats();
    const CameraStreamSubscriberStats subscriber_stats = subscriber_.GetStats();
    CodecControlStatus status;
    status.request_id = request.request_id;
    status.stream_id = stream_id_.empty() ? request.stream_id : stream_id_;
    status.recording = state_ == "recording";
    status.state = state_;
    status.codec = request.codec.empty() ? "h264" : request.codec;
    status.container = request.container.empty() ? "raw_h264" : request.container;
    status.file = file_path_;
    status.encoded_frames = encoded_frames_;
    status.dropped_frames = dropped_frames_;
    status.input_frames = config_.enable_camera_subscriber
                              ? subscriber_stats.input_frames
                              : input_frames_;
    status.write_failures = stats.write_failures + subscriber_stats.read_failures;
    status.error = error;
    return status;
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

} // namespace camera_subsystem::extensions::codec_server
