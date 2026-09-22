#include "audio_playback.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>

#include <chrono>
#include <utility>

AudioPlayback::~AudioPlayback()
{
    stop();
}

void AudioPlayback::start(int channel, const gvfg_audio_format_t &format,
                          LogCallback logCallback)
{
    stop();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        channel_ = channel;
        format_ = format;
        logCallback_ = std::move(logCallback);
        queue_.clear();
        statistics_ = {};
    }
    stopRequested_.store(false, std::memory_order_release);
    thread_ = std::thread([this] { playbackLoop(); });
}

void AudioPlayback::stop()
{
    stopRequested_.store(true, std::memory_order_release);
    queueReady_.notify_all();
    if (thread_.joinable())
        thread_.join();

    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
    logCallback_ = {};
}

bool AudioPlayback::enqueue(std::vector<uint8_t> pcm)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopRequested_.load(std::memory_order_acquire))
            return false;
        if (queue_.size() >= kMaxQueuedFrames)
            queue_.pop_front();
        queue_.push_back(std::move(pcm));
    }
    queueReady_.notify_one();
    return true;
}

void AudioPlayback::recordReceivedFrame()
{
    std::lock_guard<std::mutex> lock(mutex_);
    ++statistics_.receivedFrames;
}

void AudioPlayback::recordReleaseFailure()
{
    std::lock_guard<std::mutex> lock(mutex_);
    ++statistics_.releaseFailedFrames;
}

AudioPlayback::Statistics AudioPlayback::statistics() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return statistics_;
}

void AudioPlayback::waitBeforeRetry()
{
    std::unique_lock<std::mutex> lock(mutex_);
    queue_.clear();
    queueReady_.wait_for(lock, std::chrono::milliseconds(500), [this] {
        return stopRequested_.load(std::memory_order_acquire);
    });
}

void AudioPlayback::log(const QString &message) const
{
    LogCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = logCallback_;
    }
    if (callback)
        callback(message);
}

void AudioPlayback::playbackLoop()
{
    QAudioFormat outputFormat;
    outputFormat.setSampleRate(format_.sample_rate);
    outputFormat.setChannelCount(format_.channels);
    outputFormat.setSampleFormat(QAudioFormat::Int16);

    bool recovering = false;
    while (!stopRequested_.load(std::memory_order_acquire))
    {
        const QAudioDevice outputDevice = QMediaDevices::defaultAudioOutput();
        if (outputDevice.isNull() || !outputDevice.isFormatSupported(outputFormat))
        {
            if (!recovering)
                log(QStringLiteral("CH%1 [APP] WARNING audio output unavailable | retrying").arg(channel_));
            recovering = true;
            waitBeforeRetry();
            continue;
        }

        QAudioSink audioSink(outputDevice, outputFormat);
        QIODevice *audioOutput = audioSink.start();
        if (!audioOutput)
        {
            if (!recovering)
                log(QStringLiteral("CH%1 [APP] Audio output start failed | retrying").arg(channel_));
            recovering = true;
            waitBeforeRetry();
            continue;
        }

        if (recovering)
            log(QStringLiteral("CH%1 [APP] Audio output recovered").arg(channel_));
        recovering = false;
        bool restartPlayback = false;
        while (!stopRequested_.load(std::memory_order_acquire) && !restartPlayback)
        {
            std::vector<uint8_t> pcm;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                queueReady_.wait(lock, [this] {
                    return stopRequested_.load(std::memory_order_acquire) || !queue_.empty();
                });
                if (stopRequested_.load(std::memory_order_acquire))
                    break;
                pcm = std::move(queue_.front());
                queue_.pop_front();
            }

            qint64 written = 0;
            auto lastProgress = std::chrono::steady_clock::now();
            while (written < static_cast<qint64>(pcm.size()) &&
                   !stopRequested_.load(std::memory_order_acquire))
            {
                const qint64 result = audioOutput->write(
                    reinterpret_cast<const char *>(pcm.data()) + written,
                    static_cast<qint64>(pcm.size()) - written);
                const auto now = std::chrono::steady_clock::now();
                const bool stalled = result == 0 &&
                    now - lastProgress >= std::chrono::seconds(2);
                if (result > 0)
                {
                    written += result;
                    lastProgress = now;
                    continue;
                }
                if (result < 0 || stalled)
                {
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        ++statistics_.outputFailedFrames;
                    }
                    log(stalled
                        ? QStringLiteral("CH%1 [APP] Audio output stalled | duration_ms=2000").arg(channel_)
                        : QStringLiteral("CH%1 [APP] Audio output write failed").arg(channel_));
                    restartPlayback = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        audioSink.stop();
        if (restartPlayback)
        {
            recovering = true;
            waitBeforeRetry();
        }
    }
}
