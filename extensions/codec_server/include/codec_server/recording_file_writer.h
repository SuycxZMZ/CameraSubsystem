#ifndef CODEC_SERVER_RECORDING_FILE_WRITER_H
#define CODEC_SERVER_RECORDING_FILE_WRITER_H

#include <cstdint>
#include <cstdio>
#include <string>

namespace camera_subsystem::extensions::codec_server {

enum class WriterResult
{
    kOk,
    kOutputDirNotWritable,
    kFileCreateFailed,
    kInvalidStreamId,
    kFileNotOpen,
    kRecordingIoError,
};

struct WriterStats
{
    uint64_t bytes_written = 0;
    uint64_t packets_written = 0;
    uint64_t write_failures = 0;
};

class RecordingFileWriter
{
public:
    RecordingFileWriter() = default;
    ~RecordingFileWriter();

    RecordingFileWriter(const RecordingFileWriter&) = delete;
    RecordingFileWriter& operator=(const RecordingFileWriter&) = delete;
    RecordingFileWriter(RecordingFileWriter&&) noexcept;
    RecordingFileWriter& operator=(RecordingFileWriter&&) noexcept;

    WriterResult Open(const std::string& stream_id,
                      const std::string& output_dir);
    WriterResult Write(const uint8_t* data, size_t size);
    WriterResult Flush();
    WriterResult Close();

    WriterStats GetStats() const;
    std::string GetFilePath() const;
    void ResetStats();

private:
    std::FILE* file_handle_ = nullptr;
    std::string file_path_;
    bool is_open_ = false;
    WriterStats stats_;

    WriterResult EnsureOutputDir(const std::string& output_dir);
    WriterResult ValidateStreamId(const std::string& stream_id) const;
    std::string GenerateFileName(const std::string& stream_id) const;
    WriterResult DoFlush();
    void CloseHandle();
};

} // namespace camera_subsystem::extensions::codec_server

#endif // CODEC_SERVER_RECORDING_FILE_WRITER_H
