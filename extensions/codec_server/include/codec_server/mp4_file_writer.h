#ifndef CODEC_SERVER_MP4_FILE_WRITER_H
#define CODEC_SERVER_MP4_FILE_WRITER_H

#include "codec_server/recording_file_writer.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace camera_subsystem::extensions::codec_server {

enum class Mp4WriterResult
{
    kOk,
    kOutputDirNotWritable,
    kFileCreateFailed,
    kInvalidStreamId,
    kFileNotOpen,
    kMissingParameterSets,
    kInvalidPacket,
    kRecordingIoError,
};

struct Mp4WriterConfig
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fps = 30;
};

class Mp4FileWriter
{
public:
    struct Sample
    {
        std::vector<uint8_t> payload;
        bool is_sync = false;
    };

    Mp4FileWriter() = default;
    ~Mp4FileWriter();

    Mp4FileWriter(const Mp4FileWriter&) = delete;
    Mp4FileWriter& operator=(const Mp4FileWriter&) = delete;

    Mp4WriterResult Open(const std::string& stream_id,
                         const std::string& output_dir,
                         const Mp4WriterConfig& config);
    Mp4WriterResult WriteAnnexBPacket(const uint8_t* data, size_t size);
    Mp4WriterResult Close();

    WriterStats GetStats() const;
    std::string GetFilePath() const;

private:
    Mp4WriterResult WriteFile();
    Mp4WriterResult WriteBytes(const std::vector<uint8_t>& bytes);
    void CloseHandle();

    RecordingFileWriter path_writer_;
    std::FILE* file_handle_ = nullptr;
    std::string file_path_;
    Mp4WriterConfig config_;
    std::vector<uint8_t> sps_;
    std::vector<uint8_t> pps_;
    std::vector<Sample> samples_;
    WriterStats stats_;
    bool is_open_ = false;
};

const char* ToErrorString(Mp4WriterResult result);

} // namespace camera_subsystem::extensions::codec_server

#endif // CODEC_SERVER_MP4_FILE_WRITER_H
