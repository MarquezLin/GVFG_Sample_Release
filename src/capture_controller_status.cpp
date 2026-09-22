#include "capture_controller.h"

#include <QMetaObject>
#include <QStringList>

#include <chrono>

namespace
{
QString frameText(bool valid, int width, int height, const char *pixelFormat, int bitDepth)
{
    if (!valid || width <= 0 || height <= 0)
        return QStringLiteral("--");
    const QString format = pixelFormat && pixelFormat[0] != '\0'
                               ? QString::fromUtf8(pixelFormat) : QStringLiteral("--");
    const QString bit = bitDepth > 0 ? QString::number(bitDepth) : QStringLiteral("--");
    return QStringLiteral("%1x%2 %3 %4-bit").arg(width).arg(height).arg(format, bit);
}

QString signalFrameText(const gvfg_signal_status_t &signal)
{
    if (!signal.connected)
        return QStringLiteral("No signal");
    const QString resolution = signal.width > 0 && signal.height > 0
                                   ? QStringLiteral("%1x%2").arg(signal.width).arg(signal.height)
                                   : QStringLiteral("--");
    const QString format = signal.pixel_format != GVFG_PIXFMT_UNKNOWN
                               ? QString::fromLatin1(gvfg_pixel_format_name(signal.pixel_format))
                               : QStringLiteral("--");
    const QString bit = signal.bit_depth > 0 ? QString::number(signal.bit_depth) : QStringLiteral("--");
    const QString interfaceName = signal.video_interface == GVFG_INPUT_INTERFACE_SDI
                                      ? QStringLiteral("SDI")
                                  : signal.video_interface == GVFG_INPUT_INTERFACE_HDMI
                                      ? QStringLiteral("HDMI")
                                      : QStringLiteral("--");
    return QStringLiteral("%1 %2 %3-bit %4").arg(resolution, format, bit, interfaceName);
}
}

void CaptureController::refreshSdiInfo(int channel)
{
    gvfg_sdi_info_t info{};
    if (gvfg_get_channel_sdi_info(handle_, channel, &info) != GVFG_OK)
        return;

    const QString text = QStringLiteral("CH%1 SDI Info | %2 | %3 | %4 | %5 | %6 | %7 | %8 | %9 | %10 | ErrorCount=%11")
                             .arg(channel)
                             .arg(QString::fromUtf8(info.signal_lock_name))
                             .arg(QString::fromUtf8(info.mode_name))
                             .arg(QString::fromUtf8(info.resolution_name))
                             .arg(QString::fromUtf8(info.fps_name))
                             .arg(QString::fromUtf8(info.scan_name))
                             .arg(QString::fromUtf8(info.st352_format_name))
                             .arg(QString::fromUtf8(info.st352_fps_name))
                             .arg(QString::fromUtf8(info.st352_chroma_name))
                             .arg(QString::fromUtf8(info.st352_bit_depth_name))
                             .arg(info.error_count);

    ChannelRuntime &runtime = channels_[channel];
    if (runtime.cachedSdiInfoText == text)
        return;
    runtime.cachedSdiInfoText = text;
    if (channel == GVFG_CHANNEL_0)
        emit sdiInfoChanged(text);
}

void CaptureController::updateSignalStatus(bool queryHardware)
{
    if (handle_ == nullptr)
        return;

    QStringList statusLines;
    for (int channelIndex = GVFG_CHANNEL_0; channelIndex <= GVFG_CHANNEL_1; ++channelIndex)
    {
        if (!channelStatusVisible_[channelIndex])
            continue;

        ChannelRuntime &channel = channels_[channelIndex];
        if (!channel.opened)
        {
            statusLines << QStringLiteral("CH%1 | Not opened | %2")
                               .arg(channelIndex)
                               .arg(channel.zeroCopy ? QStringLiteral("Zero-copy") : QStringLiteral("Copy"));
            continue;
        }
        if (queryHardware)
        {
            gvfg_signal_status_t signal{};
            if (gvfg_get_channel_signal_status(handle_, channelIndex, &signal) == GVFG_OK)
            {
                channel.cachedSignalStatus = signal;
                channel.haveCachedSignalStatus = true;
            }
        }
        if (!channel.haveCachedSignalStatus)
        {
            statusLines << QStringLiteral("CH%1 | Signal unavailable").arg(channelIndex);
            continue;
        }

        const gvfg_signal_status_t &signal = channel.cachedSignalStatus;
        const QString inputStatus = signal.connected
                                        ? QStringLiteral("CH%1 [SDK] Video Locked | %2")
                                              .arg(channelIndex).arg(signalFrameText(signal))
                                        : QStringLiteral("CH%1 [SDK] Video Undetected").arg(channelIndex);
        if (inputStatus != channel.lastLoggedInputStatus)
        {
            appendLog(inputStatus);
            channel.lastLoggedInputStatus = inputStatus;
        }
        if (channel.previewVisible.load(std::memory_order_acquire) && signal.width > 0 && signal.height > 0)
            emit previewSourceSizeChanged(channelIndex, signal.width, signal.height);

        gvfg_preview_info_t previewInfo{};
        const bool previewInfoOk = channel.previewHandle &&
            gvfg_preview_get_info(channel.previewHandle, &previewInfo) == GVFG_PREVIEW_OK &&
            previewInfo.active;
        gvfg_preview_stats_t previewStats{};
        const bool previewStatsOk = channel.previewHandle &&
            gvfg_preview_get_stats(channel.previewHandle, &previewStats) == GVFG_PREVIEW_OK;
        const QString previewFps = previewStatsOk && previewStats.present_fps > 0.0
                                       ? QString::number(previewStats.present_fps, 'f', 2)
                                       : QStringLiteral("--");
        const QString previewFrame = previewInfoOk
                                         ? frameText(true, previewInfo.width, previewInfo.height,
                                                     previewInfo.pixel_format, previewInfo.bit_depth)
                                         : QStringLiteral("--");
        statusLines << QStringLiteral("CH%1 | %2 | %3 | %4")
                           .arg(QString::number(channelIndex),
                                channel.running.load(std::memory_order_acquire)
                                    ? QStringLiteral("Running") : QStringLiteral("Stopped"),
                                signalFrameText(signal),
                                channel.zeroCopy ? QStringLiteral("Zero-copy") : QStringLiteral("Copy"));
        if (channel.audioEnabled)
        {
            const AudioPlayback::Statistics audioStats = channel.audioPlayback.statistics();
            statusLines << QStringLiteral("CH%1 Audio | %2 Hz %3 ch %4-bit | received=%5 frames")
                               .arg(channelIndex)
                               .arg(channel.audioFormat.sample_rate)
                               .arg(channel.audioFormat.channels)
                               .arg(channel.audioFormat.bits_per_sample)
                               .arg(static_cast<qulonglong>(audioStats.receivedFrames));
        }
        statusLines << QStringLiteral("CH%1 Preview | %2 FPS | %3")
                           .arg(channelIndex).arg(previewFps, previewFrame);
        logDeliveryStatus(channelIndex);
    }

    const QString statusText = statusLines.join(QLatin1Char('\n'));
    if (lastSignalStatusText_ != statusText)
    {
        emit statusChanged(statusText);
        lastSignalStatusText_ = statusText;
    }
    emit stateChanged();
}

void CaptureController::reportError(const QString &apiName, gvfg_status_t status, int channel)
{
    QString message = QStringLiteral("[SDK API] %1 failed | %2")
                          .arg(apiName, QString::fromUtf8(gvfg_strerror(status)));
    if (isValidChannel(channel))
        message.prepend(QStringLiteral("CH%1 ").arg(channel));
    if (status < 0 && handle_ != nullptr && isValidChannel(channel))
    {
        char detail[512] = {};
        if (gvfg_get_channel_last_sdk_error_detail(handle_, channel, detail, sizeof(detail)) == GVFG_OK &&
            detail[0] != '\0')
            message += QStringLiteral(" | %1").arg(QString::fromUtf8(detail));
    }
    appendLog(message);
}

void CaptureController::appendLog(const QString &message)
{
    emit logMessage(message);
}

void CaptureController::postLog(const QString &message)
{
    QMetaObject::invokeMethod(
        this, [this, message] { appendLog(message); }, Qt::QueuedConnection);
}

void CaptureController::logDeliveryStatus(int channelIndex, bool finalSnapshot)
{
    ChannelRuntime &channel = channels_[channelIndex];
    if (!finalSnapshot && !channel.running.load())
        return;
    const qint64 now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (!finalSnapshot && now - channel.lastDeliveryLogMs < 5000)
        return;

    const auto count = [](uint64_t value) { return QString::number(static_cast<qulonglong>(value)); };
    gvfg_preview_delivery_stats_t previewStats{};
    const bool previewOk = channel.previewHandle &&
        gvfg_preview_get_delivery_stats(channel.previewHandle, &previewStats) == GVFG_PREVIEW_OK;
    const uint64_t failed =
        (previewOk ? previewStats.failed - channel.previewBaseline.failed : 0) + channel.videoFailed.load();
    const AudioPlayback::Statistics audioStats = channel.audioPlayback.statistics();

    if (failed != channel.lastLoggedPreviewFailures)
        appendLog(QStringLiteral("CH%1 [APP] ERROR preview submission | failures=%2")
                      .arg(QString::number(channelIndex), count(failed)));
    if (audioStats.releaseFailedFrames != channel.lastLoggedAudioReleaseFailures)
        appendLog(QStringLiteral("CH%1 [SDK API] ERROR audio frame release | failed_frames=%2")
                      .arg(QString::number(channelIndex), count(audioStats.releaseFailedFrames)));
    if (audioStats.outputFailedFrames != channel.lastLoggedAudioOutputFailures)
        appendLog(QStringLiteral("CH%1 [APP] ERROR audio output write | failed_frames=%2")
                      .arg(QString::number(channelIndex), count(audioStats.outputFailedFrames)));

    if (finalSnapshot || failed != channel.lastLoggedPreviewFailures ||
        audioStats.releaseFailedFrames != channel.lastLoggedAudioReleaseFailures ||
        audioStats.outputFailedFrames != channel.lastLoggedAudioOutputFailures)
        channel.lastDeliveryLogMs = now;
    channel.lastLoggedPreviewFailures = failed;
    channel.lastLoggedAudioReleaseFailures = audioStats.releaseFailedFrames;
    channel.lastLoggedAudioOutputFailures = audioStats.outputFailedFrames;
}
