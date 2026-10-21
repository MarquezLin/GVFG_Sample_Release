#pragma once
#include <gvfg_capture.h>
#include <gvfg_playback.h>
#include <QString>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

// Thin sample adapter: all clocks, buffering and synchronization live in SDK.
class Playback final {
public:
    struct Statistics { uint64_t receivedFrames=0, releaseFailedFrames=0, outputFailedFrames=0; };
    using LogCallback = std::function<void(const QString &)>;
    ~Playback();
    bool attachWindow(void *hwnd);
    gvfg_playback_status_t setSignalConnected(bool connected);
    bool start(int channel, const gvfg_audio_format_t &format, bool audio, LogCallback log);
    void stop();
    void close();
    bool enqueue(std::vector<uint8_t> pcm, uint64_t timestampNs);
    gvfg_playback_status_t submitVideo(const gvfg_preview_frame_t &frame, uint64_t timestampNs);
    void resetTimeline();
    void recordReceivedFrame();
    void recordReleaseFailure();
    Statistics statistics() const;
    gvfg_playback_status_t previewInfo(gvfg_preview_info_t *s) const;
    gvfg_playback_status_t previewStats(gvfg_preview_stats_t *s) const;
    gvfg_playback_status_t deliveryStats(gvfg_preview_delivery_stats_t *s) const;
private:
    mutable std::mutex mutex_;
    gvfg_playback player_ = nullptr;
    Statistics statistics_{};
    LogCallback log_;
    int channel_ = 0;
};
