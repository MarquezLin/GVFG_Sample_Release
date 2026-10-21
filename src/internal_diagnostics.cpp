#include "internal_diagnostics.h"

#include <algorithm>

namespace {
uint64_t counterDelta(uint64_t value, uint64_t baseline) {
    return value >= baseline ? value - baseline : 0;
}
}

#if GVFG_INTERNAL_DIAGNOSTICS
#include <gvfg_debug.h>
#endif

void InternalDiagnostics::beginStart(int channel)
{
    if (channel < 0 || channel >= static_cast<int>(metrics_.size()))
        return;
    metrics_[channel].startTime = std::chrono::steady_clock::now();
}

void InternalDiagnostics::finishStartCall(int channel)
{
    if (channel < 0 || channel >= static_cast<int>(metrics_.size()))
        return;
    ChannelMetrics &metrics = metrics_[channel];
    metrics.startCallMs = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - metrics.startTime)
                              .count();
}

void InternalDiagnostics::resetChannel(gvfg_handle handle, int channel)
{
    if (channel < 0 || channel >= static_cast<int>(baselines_.size()))
        return;

    baselines_[channel] = {};
    baselines_[channel].warmupStart = std::chrono::steady_clock::now();
    lastAudioReceived_[channel] = 0;
    ChannelMetrics &metrics = metrics_[channel];
    metrics.successfulFrames = 0;
    metrics.windowSamples = 0;
    metrics.windowMaximumMs = 0.0;
    metrics.startupLatencyReported = false;
    metrics.videoReceived.store(0, std::memory_order_relaxed);
    metrics.readSamples.store(0, std::memory_order_relaxed);
    metrics.readAverageMs.store(0.0, std::memory_order_relaxed);
    metrics.readMaximumMs.store(0.0, std::memory_order_relaxed);
    metrics.readWindowMaximumMs.store(0.0, std::memory_order_relaxed);

    (void)handle;
}

#if GVFG_INTERNAL_DIAGNOSTICS
bool InternalDiagnostics::prepareCounters(const gvfg_debug_backend_stats_t &s, int channel) const
{
    auto &b = baselines_[channel];
    if (std::chrono::steady_clock::now() - b.warmupStart < std::chrono::seconds(3))
        return false;
    const bool reset = s.video_dma_event_wakes < b.videoDmaEventWakes ||
        s.extra_video_event_wakes < b.extraVideoEventWakes || s.video_frames_from_lib < b.videoFramesFromLibrary ||
        s.audio_dma_event_wakes < b.audioDmaEventWakes || s.extra_audio_event_wakes < b.extraAudioEventWakes ||
        s.audio_frames_from_lib < b.audioFramesFromLibrary ||
        (s.audio_driver_frame_count_valid && (!b.driverValid || s.audio_driver_frame_count < b.audioDriverFrameCount));
    if (!b.ready || reset) {
        b.videoDmaEventWakes = s.video_dma_event_wakes; b.extraVideoEventWakes = s.extra_video_event_wakes;
        b.videoFramesFromLibrary = s.video_frames_from_lib; b.audioDmaEventWakes = s.audio_dma_event_wakes;
        b.extraAudioEventWakes = s.extra_audio_event_wakes; b.audioFramesFromLibrary = s.audio_frames_from_lib;
        b.audioDriverFrameCount = s.audio_driver_frame_count; b.driverValid = s.audio_driver_frame_count_valid != 0;
        b.audioReceived = lastAudioReceived_[channel];
        b.videoReceived = metrics_[channel].videoReceived.load(std::memory_order_relaxed);
        b.ready = true;
    }
    return true;
}
#endif

void InternalDiagnostics::recordVideoRead(int channel, double elapsedMs)
{
    constexpr uint64_t kTimingWarmupFrames = 30;
    constexpr uint64_t kTimingWindowFrames = 300;

    if (channel < 0 || channel >= static_cast<int>(metrics_.size()))
        return;

    ChannelMetrics &metrics = metrics_[channel];
    metrics.videoReceived.fetch_add(1, std::memory_order_relaxed);
    if (++metrics.successfulFrames <= kTimingWarmupFrames)
        return;

    ++metrics.windowSamples;
    metrics.windowMaximumMs = (std::max)(metrics.windowMaximumMs, elapsedMs);
    const uint64_t totalSamples = metrics.readSamples.fetch_add(1, std::memory_order_relaxed) + 1;
    const double previousAverage = metrics.readAverageMs.load(std::memory_order_relaxed);
    metrics.readAverageMs.store(
        previousAverage + (elapsedMs - previousAverage) / static_cast<double>(totalSamples),
        std::memory_order_relaxed);
    double observedMaximum = metrics.readMaximumMs.load(std::memory_order_relaxed);
    while (elapsedMs > observedMaximum &&
           !metrics.readMaximumMs.compare_exchange_weak(
               observedMaximum, elapsedMs, std::memory_order_relaxed))
    {
    }
    if (totalSamples <= kTimingWindowFrames)
        metrics.readWindowMaximumMs.store(metrics.windowMaximumMs, std::memory_order_relaxed);
    if (metrics.windowSamples >= kTimingWindowFrames)
    {
        metrics.readWindowMaximumMs.store(metrics.windowMaximumMs, std::memory_order_relaxed);
        metrics.windowSamples = 0;
        metrics.windowMaximumMs = 0.0;
    }
}

QString InternalDiagnostics::takeStartupLatencyLine(int channel,
                                                    double firstReadMs,
                                                    double firstRenderMs,
                                                    uint64_t frameId)
{
    if (channel < 0 || channel >= static_cast<int>(metrics_.size()))
        return {};

    ChannelMetrics &metrics = metrics_[channel];
    if (metrics.startupLatencyReported)
        return {};
    metrics.startupLatencyReported = true;

    const double totalMs = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - metrics.startTime)
                               .count();
    return QStringLiteral("CH%1 Startup latency | Start -> start_channel return=%2 ms | "
                          "first read_frame call -> return=%3 ms | "
                          "first playback submit_video call -> return=%4 ms | "
                          "Start -> first submit_video return=%5 ms | SDK_frame_id=%6")
        .arg(channel)
        .arg(metrics.startCallMs, 0, 'f', 3)
        .arg(firstReadMs, 0, 'f', 3)
        .arg(firstRenderMs, 0, 'f', 3)
        .arg(totalMs, 0, 'f', 3)
        .arg(static_cast<qulonglong>(frameId));
}

void InternalDiagnostics::appendAudioStatusLine(QStringList &lines,
                                                gvfg_handle handle,
                                                int channel,
                                                uint64_t audioReceived) const
{
#if GVFG_INTERNAL_DIAGNOSTICS
    if (handle == nullptr || channel < 0 || channel >= static_cast<int>(baselines_.size()))
        return;

    gvfg_debug_backend_stats_t stats{};
    if (gvfg_debug_get_channel_backend_stats(handle, channel, &stats) != GVFG_OK)
        return;

    lastAudioReceived_[channel] = audioReceived;
    const bool ready = prepareCounters(stats, channel);
    const BackendBaseline &baseline = baselines_[channel];
    const QString driverFrames = !ready ? QStringLiteral("warming up (3 s)") : stats.audio_driver_frame_count_valid
        ? QString::number(static_cast<qulonglong>(
              counterDelta(stats.audio_driver_frame_count, baseline.audioDriverFrameCount)))
        : QStringLiteral("unavailable");
    lines << QStringLiteral("CH%1 Audio State | enabled=%2 running=%3 | event DMA=%4 Extra=%5 | driver_frames=%6")
                 .arg(channel)
                 .arg(stats.audio_enabled ? QStringLiteral("yes") : QStringLiteral("no"))
                 .arg(stats.stream_running ? QStringLiteral("yes") : QStringLiteral("no"))
                 .arg(stats.audio_frame_event_valid ? QStringLiteral("valid") : QStringLiteral("NULL"))
                 .arg(stats.audio_extra_event_valid ? QStringLiteral("valid") : QStringLiteral("NULL"))
                 .arg(driverFrames);
    if (!ready) {
        lines << QStringLiteral("CH%1 Audio Debug | warming up (3 s)").arg(channel);
        return;
    }
    lines << QStringLiteral("CH%1 Audio Debug | LIB events DMA=%2 Extra=%3 | LIB->SDK %4 frames | SDK->APP %5 frames")
                 .arg(channel)
                 .arg(static_cast<qulonglong>(counterDelta(stats.audio_dma_event_wakes, baseline.audioDmaEventWakes)))
                 .arg(static_cast<qulonglong>(counterDelta(stats.extra_audio_event_wakes, baseline.extraAudioEventWakes)))
                 .arg(static_cast<qulonglong>(counterDelta(stats.audio_frames_from_lib, baseline.audioFramesFromLibrary)))
                 .arg(static_cast<qulonglong>(counterDelta(audioReceived, baseline.audioReceived)));
#else
    (void)lines;
    (void)handle;
    (void)channel;
    (void)audioReceived;
#endif
}

void InternalDiagnostics::appendVideoStatusLines(QStringList &lines,
                                                 gvfg_handle handle,
                                                 int channel,
                                                 bool zeroCopy) const
{
#if GVFG_INTERNAL_DIAGNOSTICS
    if (handle == nullptr || channel < 0 || channel >= static_cast<int>(baselines_.size()))
        return;

    const ChannelMetrics &metrics = metrics_[channel];
    const uint64_t readSamples = metrics.readSamples.load(std::memory_order_relaxed);
    lines << (readSamples >= 300
                  ? QStringLiteral("CH%1 Read | avg=%2 max300=%3 max=%4 ms samples=%5")
                        .arg(channel)
                        .arg(metrics.readAverageMs.load(std::memory_order_relaxed), 0, 'f', 3)
                        .arg(metrics.readWindowMaximumMs.load(std::memory_order_relaxed), 0, 'f', 3)
                        .arg(metrics.readMaximumMs.load(std::memory_order_relaxed), 0, 'f', 3)
                        .arg(static_cast<qulonglong>(readSamples))
                  : QStringLiteral("CH%1 Read | measuring").arg(channel));

    gvfg_debug_backend_stats_t stats{};
    if (gvfg_debug_get_channel_backend_stats(handle, channel, &stats) != GVFG_OK)
        return;

    const bool ready = prepareCounters(stats, channel);
    const BackendBaseline &baseline = baselines_[channel];
    lines << (stats.get_frame_timing_samples >= 300
                  ? QStringLiteral("CH%1 %2 | avg=%3 max300=%4 max=%5 us samples=%6")
                        .arg(channel)
                        .arg(zeroCopy
                                 ? QStringLiteral("[LIB] GvfgGetVideoFrameZeroCopy")
                                 : QStringLiteral("[LIB] GvfgGetVideoFrame"))
                        .arg(stats.get_frame_timing_average_us, 0, 'f', 3)
                        .arg(stats.get_frame_timing_max300_us, 0, 'f', 3)
                        .arg(stats.get_frame_timing_max_us, 0, 'f', 3)
                        .arg(static_cast<qulonglong>(stats.get_frame_timing_samples))
                  : QStringLiteral("CH%1 [LIB] GetVideoFrame | measuring").arg(channel));
    if (!ready) {
        lines << QStringLiteral("CH%1 Video Debug | warming up (3 s)").arg(channel);
        return;
    }
    lines << QStringLiteral("CH%1 Video Debug | LIB events DMA=%2 Extra=%3 | LIB->SDK %4 frames | SDK->APP %5 frames")
                 .arg(channel)
                 .arg(static_cast<qulonglong>(counterDelta(stats.video_dma_event_wakes, baseline.videoDmaEventWakes)))
                 .arg(static_cast<qulonglong>(counterDelta(stats.extra_video_event_wakes, baseline.extraVideoEventWakes)))
                 .arg(static_cast<qulonglong>(counterDelta(stats.video_frames_from_lib, baseline.videoFramesFromLibrary)))
                 .arg(static_cast<qulonglong>(counterDelta(metrics.videoReceived.load(std::memory_order_relaxed), baseline.videoReceived)));
#else
    (void)lines;
    (void)handle;
    (void)channel;
    (void)zeroCopy;
#endif
}
