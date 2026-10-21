#include "playback.h"

Playback::~Playback() { close(); }
bool Playback::attachWindow(void *hwnd) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!player_ && gvfg_playback_create(&player_) != GVFG_PLAYBACK_OK) return false;
    return gvfg_playback_attach_window(player_, hwnd) == GVFG_PLAYBACK_OK;
}
bool Playback::start(int channel, const gvfg_audio_format_t &format, bool audio, LogCallback log) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!player_ && gvfg_playback_create(&player_) != GVFG_PLAYBACK_OK) return false;
    channel_ = channel; log_ = std::move(log); statistics_ = {};
    gvfg_playback_config_t config{}; gvfg_playback_default_config(&config);
    config.mode = audio ? GVFG_PLAYBACK_AUDIO_VIDEO : GVFG_PLAYBACK_VIDEO_ONLY;
    config.audio_format = {format.sample_rate, static_cast<uint16_t>(format.channels), static_cast<uint16_t>(format.bits_per_sample)};
    // Internal diagnostics exercise synchronization; customer builds use
    // independent playback unless the application explicitly opts in.
    auto status = gvfg_playback_set_sync_enabled(player_, GVFG_INTERNAL_DIAGNOSTICS ? 1 : 0);
    if (status == GVFG_PLAYBACK_OK) status = gvfg_playback_start(player_, &config);
    if (status != GVFG_PLAYBACK_OK && log_) log_(QStringLiteral("CH%1 [PLAYBACK] start failed | %2").arg(channel_).arg(QString::fromUtf8(gvfg_playback_strerror(status))));
    return status == GVFG_PLAYBACK_OK;
}
void Playback::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (player_) gvfg_playback_stop(player_);
}
void Playback::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    gvfg_playback_destroy(player_); player_ = nullptr; log_ = {};
}
bool Playback::enqueue(std::vector<uint8_t> pcm, uint64_t ts) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto status = gvfg_playback_submit_audio(player_, pcm.data(), static_cast<uint32_t>(pcm.size()), ts);
    if (status != GVFG_PLAYBACK_OK) {
        ++statistics_.outputFailedFrames;
        if (log_) log_(QStringLiteral("CH%1 [PLAYBACK] audio not accepted | %2 | capture stopped").arg(channel_).arg(QString::fromUtf8(gvfg_playback_strerror(status))));
    }
    return status == GVFG_PLAYBACK_OK;
}
gvfg_playback_status_t Playback::submitVideo(const gvfg_preview_frame_t &frame, uint64_t ts) {
    std::lock_guard<std::mutex> lock(mutex_);
    gvfg_playback_video_frame_t video{frame, ts};
    const auto status = gvfg_playback_submit_video(player_, &video);
    if (status != GVFG_PLAYBACK_OK && log_)
        log_(QStringLiteral("CH%1 [PLAYBACK] video not accepted | %2 | capture stopped")
            .arg(channel_).arg(QString::fromUtf8(gvfg_playback_strerror(status))));
    return status;
}
void Playback::resetTimeline() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (player_) {
        const auto status = gvfg_playback_reset_timeline(player_);
        if (status != GVFG_PLAYBACK_OK && log_) log_(QString::fromUtf8(gvfg_playback_strerror(status)));
    }
}
void Playback::recordReceivedFrame() { std::lock_guard<std::mutex> lock(mutex_); ++statistics_.receivedFrames; }
void Playback::recordReleaseFailure() { std::lock_guard<std::mutex> lock(mutex_); ++statistics_.releaseFailedFrames; }
Playback::Statistics Playback::statistics() const { std::lock_guard<std::mutex> lock(mutex_); return statistics_; }
gvfg_playback_status_t Playback::previewInfo(gvfg_preview_info_t *s) const { std::lock_guard<std::mutex> lock(mutex_); return gvfg_playback_get_preview_info(player_, s); }
gvfg_playback_status_t Playback::previewStats(gvfg_preview_stats_t *s) const { std::lock_guard<std::mutex> lock(mutex_); return gvfg_playback_get_preview_stats(player_, s); }
gvfg_playback_status_t Playback::deliveryStats(gvfg_preview_delivery_stats_t *s) const { std::lock_guard<std::mutex> lock(mutex_); return gvfg_playback_get_delivery_stats(player_, s); }
