#pragma once

#include <gvfg_capture.h>

#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class AudioPlayback final
{
public:
    struct Statistics
    {
        uint64_t receivedFrames = 0;
        uint64_t releaseFailedFrames = 0;
        uint64_t outputFailedFrames = 0;
    };

    using LogCallback = std::function<void(const QString &)>;

    AudioPlayback() = default;
    ~AudioPlayback();

    AudioPlayback(const AudioPlayback &) = delete;
    AudioPlayback &operator=(const AudioPlayback &) = delete;

    void start(int channel, const gvfg_audio_format_t &format, LogCallback logCallback);
    void stop();
    bool enqueue(std::vector<uint8_t> pcm);
    void recordReceivedFrame();
    void recordReleaseFailure();
    Statistics statistics() const;

private:
    void playbackLoop();
    void waitBeforeRetry();
    void log(const QString &message) const;

    static constexpr size_t kMaxQueuedFrames = 10;

    int channel_ = 0;
    gvfg_audio_format_t format_{};
    LogCallback logCallback_;
    std::atomic<bool> stopRequested_{true};
    std::thread thread_;
    mutable std::mutex mutex_;
    std::condition_variable queueReady_;
    std::deque<std::vector<uint8_t>> queue_;
    Statistics statistics_{};
};
