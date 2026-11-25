#include "capture_controller.h"

#include <QMetaObject>
#include <QStringList>

#include <chrono>
#include <cstddef>

namespace
{
constexpr qint64 kDeliveryLogIntervalMs = 5000;
constexpr std::size_t kSdkErrorDetailCapacity = 512;

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

QString queryInputInfo(gvfg_handle handle, int channel, int videoInterface)
{
    QString text = QStringLiteral("CH%1 Input Info: --").arg(channel);
    if (videoInterface == GVFG_INPUT_INTERFACE_HDMI)
    {
        gvfg_hdmi_info_t info{};
        text = QStringLiteral("CH%1 HDMI Video Info").arg(channel);
        if (gvfg_get_channel_hdmi_info(handle, channel, &info) != GVFG_OK)
            text += QStringLiteral(" | Unavailable");
        else if (!info.connected)
            text += QStringLiteral(" | No signal");
        else
            text += QStringLiteral(" | Locked | %1x%2 | %3 fps | %4 | %5 | %6-bit")
                        .arg(info.width).arg(info.height).arg(info.fps)
                        .arg(QString::fromUtf8(info.color_format_name),
                             QString::fromUtf8(info.color_space_name))
                        .arg(info.bit_depth);

        gvfg_hdmi_audio_info_t audio{};
        text += QStringLiteral("\nCH%1 HDMI Audio Info").arg(channel);
        if (gvfg_get_channel_hdmi_audio_info(handle, channel, &audio) != GVFG_OK)
            text += QStringLiteral(" | Unavailable");
        else if (!audio.connected)
            text += QStringLiteral(" | No signal");
        else
            text += QStringLiteral(" | Locked | %1 | %2-bit | %3 ch | %4")
                        .arg(QString::fromUtf8(audio.sample_frequency_name))
                        .arg(audio.bits_per_sample)
                        .arg(audio.channels)
                        .arg(QString::fromUtf8(audio.format_name));
    }
    else if (videoInterface == GVFG_INPUT_INTERFACE_SDI)
    {
        gvfg_sdi_info_t info{};
        if (gvfg_get_channel_sdi_info(handle, channel, &info) != GVFG_OK)
            text = QStringLiteral("CH%1 SDI Info | Unavailable").arg(channel);
        else
            text = QStringLiteral("CH%1 SDI Info | %2 | %3 | %4 | %5 | %6 | %7 | %8 | %9 | %10 | ErrorCount=%11")
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
    }

    return text;
}
}

void CaptureController::refreshInputInfo(bool refreshAfterPending)
{
    if (!handle_ || closingDevice_)
        return;
    if (inputInfoThread_.joinable())
    {
        inputInfoRefreshPending_ |= refreshAfterPending;
        return;
    }

    std::array<int, 2> interfaces{{-1, -1}};
    for (int channel = GVFG_CHANNEL_0; channel <= GVFG_CHANNEL_1; ++channel)
    {
        const ChannelRuntime &runtime = channels_[channel];
        if (runtime.running.load(std::memory_order_acquire))
            interfaces[channel] = runtime.inputInfoInterface;
    }
    if (interfaces[0] < 0 && interfaces[1] < 0)
        return;

    const uint64_t request = ++inputInfoRequest_;
    const gvfg_handle handle = handle_;
    inputInfoThread_ = std::thread([this, handle, interfaces, request] {
        std::array<QString, 2> results;
        for (int channel = GVFG_CHANNEL_0; channel <= GVFG_CHANNEL_1; ++channel)
        {
            if (interfaces[channel] >= 0)
                results[channel] = queryInputInfo(handle, channel, interfaces[channel]);
        }
        QMetaObject::invokeMethod(this, [this, request, results] {
            if (request != inputInfoRequest_)
                return;
            const bool refreshPending = inputInfoRefreshPending_;
            joinInputInfoThread();
            for (int channel = GVFG_CHANNEL_0; channel <= GVFG_CHANNEL_1; ++channel)
            {
                if (!results[channel].isEmpty())
                    channels_[channel].cachedInputInfoText = results[channel];
            }
            publishInputInfo();
            if (refreshPending)
                refreshInputInfo();
        }, Qt::QueuedConnection);
    });
}

void CaptureController::joinInputInfoThread()
{
    ++inputInfoRequest_;
    inputInfoRefreshPending_ = false;
    if (inputInfoThread_.joinable())
        inputInfoThread_.join();
}

void CaptureController::publishInputInfo()
{
    QStringList lines;
    for (const ChannelRuntime &input : channels_)
    {
        if (!input.cachedInputInfoText.isEmpty())
            lines << input.cachedInputInfoText;
    }
    if (!chipVersionText_.isEmpty())
        lines << chipVersionText_;
    const QString text = lines.isEmpty() ? QStringLiteral("Input Info: --") : lines.join(QLatin1Char('\n'));
    if (lastInputInfoText_ == text)
        return;
    lastInputInfoText_ = text;
    emit inputInfoChanged(text);
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
                if (signal.connected)
                    channel.inputInfoInterface = signal.video_interface;
            }
        }
        if (!channel.haveCachedSignalStatus)
        {
            statusLines << QStringLiteral("CH%1 | Signal unavailable").arg(channelIndex);
            continue;
        }

        const gvfg_signal_status_t &signal = channel.cachedSignalStatus;
        const auto cardStatus = channel.playback.setSignalConnected(signal.connected != 0);
        if (cardStatus != GVFG_PLAYBACK_OK)
            appendLog(QStringLiteral("CH%1 [PLAYBACK] signal state failed | %2")
                .arg(channelIndex).arg(QString::fromUtf8(gvfg_playback_strerror(cardStatus))));
        const QString inputStatus = signal.connected
                                        ? QStringLiteral("CH%1 [SDK] Input connected (SDK state) | %2")
                                              .arg(channelIndex).arg(signalFrameText(signal))
                                        : QStringLiteral("CH%1 [SDK] Video Undetected").arg(channelIndex);
        if (inputStatus != channel.lastLoggedInputStatus)
        {
            appendLog(inputStatus);
            channel.lastLoggedInputStatus = inputStatus;
        }
        if (channel.previewVisible.load(std::memory_order_acquire) && signal.width > 0 && signal.height > 0)
            emit previewSourceSizeChanged(channelIndex, signal.width, signal.height);

        statusLines << QStringLiteral("CH%1 | %2 | %3 | %4")
                           .arg(QString::number(channelIndex),
                                channel.running.load(std::memory_order_acquire)
                                    ? QStringLiteral("Running") : QStringLiteral("Stopped"),
                                signalFrameText(signal),
                                channel.zeroCopy ? QStringLiteral("Zero-copy") : QStringLiteral("Copy"));
        const uint64_t videoReceived = channel.videoReceived.load();
        QString videoFps = QStringLiteral("--");
        if (channel.running.load(std::memory_order_acquire))
        {
            const qint64 elapsedMs = channel.videoFpsTimer.elapsed();
            if (elapsedMs >= 1000)
            {
                const uint64_t frames = videoReceived - channel.videoFpsStartFrames;
                channel.videoFpsText = QString::number(1000.0 * frames / elapsedMs, 'f', 2);
                channel.videoFpsStartFrames = videoReceived;
                channel.videoFpsTimer.restart();
            }
            videoFps = channel.videoFpsText;
        }
        statusLines << QStringLiteral("CH%1 Video | %2 FPS | received=%3 frames")
                           .arg(channelIndex)
                           .arg(videoFps)
                           .arg(static_cast<qulonglong>(videoReceived));
        if (channel.audioEnabled)
        {
            const Playback::Statistics audioStats = channel.playback.statistics();
            QString audioFps = QStringLiteral("--");
            if (channel.running.load(std::memory_order_acquire))
            {
                const qint64 elapsedMs = channel.audioFpsTimer.elapsed();
                if (elapsedMs >= 1000)
                {
                    const uint64_t frames = audioStats.receivedFrames - channel.audioFpsStartFrames;
                    channel.audioFpsText = QString::number(1000.0 * frames / elapsedMs, 'f', 2);
                    channel.audioFpsStartFrames = audioStats.receivedFrames;
                    channel.audioFpsTimer.restart();
                }
                audioFps = channel.audioFpsText;
            }
            statusLines << QStringLiteral("CH%1 Audio | %2 Hz %3 ch %4-bit | %5 FPS | received=%6 frames")
                               .arg(channelIndex)
                               .arg(channel.audioFormat.sample_rate)
                               .arg(channel.audioFormat.channels)
                               .arg(channel.audioFormat.bits_per_sample)
                               .arg(audioFps)
                               .arg(static_cast<qulonglong>(audioStats.receivedFrames));
        }
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
        char detail[kSdkErrorDetailCapacity] = {};
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
    if (!finalSnapshot && now - channel.lastDeliveryLogMs < kDeliveryLogIntervalMs)
        return;

    const auto count = [](uint64_t value) { return QString::number(static_cast<qulonglong>(value)); };
    const uint64_t failed = channel.videoFailed.load();
    const Playback::Statistics audioStats = channel.playback.statistics();

    if (failed != channel.lastLoggedPreviewFailures)
        appendLog(QStringLiteral("CH%1 [APP] ERROR preview submission | failures=%2")
                      .arg(QString::number(channelIndex), count(failed)));
    if (audioStats.releaseFailedFrames != channel.lastLoggedAudioReleaseFailures)
        appendLog(QStringLiteral("CH%1 [SDK API] ERROR audio frame release | failed_frames=%2")
                      .arg(QString::number(channelIndex), count(audioStats.releaseFailedFrames)));
    if (audioStats.outputFailedFrames != channel.lastLoggedAudioOutputFailures)
        appendLog(QStringLiteral("CH%1 [APP] ERROR audio playback submission | failed_frames=%2")
                      .arg(QString::number(channelIndex), count(audioStats.outputFailedFrames)));

    channel.lastDeliveryLogMs = now;
    channel.lastLoggedPreviewFailures = failed;
    channel.lastLoggedAudioReleaseFailures = audioStats.releaseFailedFrames;
    channel.lastLoggedAudioOutputFailures = audioStats.outputFailedFrames;
}
