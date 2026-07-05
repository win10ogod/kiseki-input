#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "platform/result.hpp"

namespace kiseki::platform::teach {

struct RecordOptions {
    std::filesystem::path output_directory;
    std::filesystem::path video_file;
    std::filesystem::path audio_file;
    std::filesystem::path transcript_file;
    std::filesystem::path state_file;
    std::filesystem::path stop_file;
    std::uint32_t duration_ms;
    std::uint32_t frame_interval_ms;
    std::uint32_t event_poll_ms;
    std::uint32_t stop_timeout_ms;
    std::uint32_t video_keyframe_interval_ms;
    std::uint32_t video_keyframe_max;
    bool worker;
    bool no_video_keyframes;
    std::string title;
    std::string instruction_text;
};

struct AnnotateOptions {
    std::filesystem::path session_directory;
    int frame_index;
    int event_index;
    bool has_frame_index;
    bool has_event_index;
    std::string text;
};

struct TranscribeOptions {
    std::filesystem::path audio_file;
    std::filesystem::path output_path;
    std::filesystem::path model_path;
    std::filesystem::path script_path;
    std::string model_id;
    std::string language;
    std::string device;
    std::string compute_type;
};

OperationResult record_teaching_session(const RecordOptions& options);
OperationResult add_text_annotation(const AnnotateOptions& options);
OperationResult transcribe_audio(const TranscribeOptions& options);

}
