#include "capture_controller.h"
#include <QMetaObject>
#include <QStringList>
#include <QTimer>

#if GVFG_INTERNAL_DIAGNOSTICS
#include <gvfg_debug.h>
#endif

#include <chrono>
#include <cstring>
#include <utility>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace
{
    constexpr int kRuntimeStatusIntervalMs = 200;
    constexpr uint32_t kFrameReadTimeoutMs = 200;

    QString eventTypeText(gvfg_event_type_t type)
    {
        switch (type)
        {
        case GVFG_EVENT_VIDEO_FORMAT_CHANGED: return QStringLiteral("VIDEO_FORMAT_CHANGED");
        case GVFG_EVENT_VIDEO_INPUT_PLUGIN: return QStringLiteral("VIDEO_INPUT_PLUGIN");
        case GVFG_EVENT_VIDEO_INPUT_UNPLUG: return QStringLiteral("VIDEO_INPUT_UNPLUG");
        default: return QStringLiteral("UNKNOWN");
        }
    }
}
bool CaptureController::isValidChannel(int channel)
{
    return channel >= GVFG_CHANNEL_0 && channel <= GVFG_CHANNEL_1;
}

CaptureController::CaptureController(QObject *parent) : QObject(parent)
{
    runtimeStatusTimer_ = new QTimer(this);
    runtimeStatusTimer_->setInterval(kRuntimeStatusIntervalMs);
    connect(runtimeStatusTimer_, &QTimer::timeout, this, [this] {
        processPendingEvents();
        updateSignalStatus(false);
    });
}

CaptureController::~CaptureController()
{
    closeDevice();
}

void CaptureController::setChannelOptions(int channel, bool zeroCopy,
                                          gvfg_pixel_format_t format, bool audioEnabled)
{
    if (channel < GVFG_CHANNEL_0 || channel > GVFG_CHANNEL_1)
        return;
    channels_[channel].zeroCopy = zeroCopy;
    channels_[channel].requestedFormat = format;
    channels_[channel].requestedAudio = channel == GVFG_CHANNEL_0 && audioEnabled;
}

void CaptureController::setChannelStatusVisible(int channel, bool visible)
{
    if (channel >= GVFG_CHANNEL_0 && channel <= GVFG_CHANNEL_1)
        channelStatusVisible_[channel] = visible;
}

void CaptureController::setPreviewTarget(int channel, void *nativeWindow)
{
    if (channel >= GVFG_CHANNEL_0 && channel <= GVFG_CHANNEL_1)
        channels_[channel].previewTarget = nativeWindow;
}

void CaptureController::setPreviewVisible(int channel, bool visible)
{
    if (isValidChannel(channel)) {
        channels_[channel].previewVisible.store(visible, std::memory_order_release);
        if (channels_[channel].running.load())
            channels_[channel].playback.attachWindow(visible ? channels_[channel].previewTarget : nullptr);
    }
}

bool CaptureController::channelOpened(int channel) const
{
    return channel >= GVFG_CHANNEL_0 && channel <= GVFG_CHANNEL_1 && channels_[channel].opened;
}

bool CaptureController::channelRunning(int channel) const
{
    return channel >= GVFG_CHANNEL_0 && channel <= GVFG_CHANNEL_1 &&
           channels_[channel].running.load(std::memory_order_acquire);
}

bool CaptureController::frameAvailable(int channel) const
{
    return channel >= GVFG_CHANNEL_0 && channel <= GVFG_CHANNEL_1 &&
           channels_[channel].frameAvailable.load(std::memory_order_acquire);
}

bool CaptureController::cachedSignalStatus(int channel, gvfg_signal_status_t *status) const
{
    if (!status || channel < GVFG_CHANNEL_0 || channel > GVFG_CHANNEL_1 ||
        !channels_[channel].haveCachedSignalStatus)
        return false;
    *status = channels_[channel].cachedSignalStatus;
    return true;
}

QString CaptureController::sdkVersion() const
{
    return QString::fromLatin1(gvfg_get_version());
}

QString CaptureController::gigabyteLibVersion() const
{
    return QString::fromLatin1(gvfg_get_gigabyte_lib_version());
}

void CaptureController::refreshDevices()
{
    if (handle_ != nullptr)
        closeDevice();

    devices_ = {};

    const int count = gvfg_enumerate_devices(devices_.data(), GVFG_MAX_DEVICES);
    deviceCount_ = count > 0 ? count : 0;

    QStringList names;
    for (int i = 0; i < deviceCount_; ++i)
    {
        const QString name = QString::fromUtf8(devices_[i].name);
        const QString displayName = name.isEmpty() ? QStringLiteral("GVFG Capture") : name;
        names.push_back(displayName);
    }

    if (deviceCount_ <= 0)
    {
        appendLog(QStringLiteral("[SDK] ERROR no GVFG device found"));
    }
    else
    {
        appendLog(QStringLiteral("Found %1 GVFG capture device(s)").arg(deviceCount_));
    }
    emit devicesChanged(names);
}

bool CaptureController::openDevice()
{
    if (handle_ != nullptr)
        return true;

    if (selectedDeviceIndex_ < 0)
    {
        appendLog(QStringLiteral("[APP] Open skipped | no device selected"));
        return false;
    }

    gvfg_status_t st = gvfg_create(&handle_);
    if (st != GVFG_OK || handle_ == nullptr)
    {
        reportError(QStringLiteral("gvfg_create"), st);
        handle_ = nullptr;
        emit stateChanged();
        return false;
    }

    lastSignalStatusText_.clear();
    appendLog(QStringLiteral("Created device session for index %1").arg(selectedDeviceIndex_));
    runtimeStatusTimer_->start();
    updateSignalStatus(false);
    emit stateChanged();
    return true;
}

bool CaptureController::openChannel(int channel)
{
    ChannelRuntime &runtime = channels_[channel];
    if (handle_ == nullptr && !openDevice())
        return false;

    const bool newlyOpened = !runtime.opened;
    const bool zeroCopyEnabled = runtime.zeroCopy;
    gvfg_status_t status = GVFG_OK;
    if (newlyOpened)
    {
        status = gvfg_set_channel_zero_copy_enabled(handle_, channel, zeroCopyEnabled ? 1 : 0);
        if (status != GVFG_OK)
        {
            reportError(QStringLiteral("gvfg_set_channel_zero_copy_enabled"), status, channel);
            return false;
        }
        status = gvfg_open_channel(handle_, selectedDeviceIndex_, channel);
        if (status != GVFG_OK)
        {
            reportError(QStringLiteral("gvfg_open_channel"), status, channel);
            return false;
        }
        runtime.opened = true;
    }

    status = gvfg_set_channel_audio_enabled(
        handle_, channel, runtime.requestedAudio ? 1 : 0);
    if (status != GVFG_OK)
    {
        reportError(QStringLiteral("gvfg_set_channel_audio_enabled"), status, channel);
        return false;
    }
    if (!applyOutputFormat(channel))
        return false;

    if (newlyOpened)
    {
        appendLog(QStringLiteral("Opened device index %1 CH%2 | mode=%3")
                      .arg(selectedDeviceIndex_)
                      .arg(channel)
                      .arg(zeroCopyEnabled ? QStringLiteral("zero-copy") : QStringLiteral("copy")));
    }
    return true;
}

bool CaptureController::applyOutputFormat(int channel)
{
    if (handle_ == nullptr || !channels_[channel].opened ||
        channels_[channel].running.load(std::memory_order_acquire))
        return false;

    const gvfg_pixel_format_t format = channels_[channel].requestedFormat;
    const gvfg_status_t status = gvfg_set_channel_video_format(handle_, channel, format);
    if (status != GVFG_OK)
    {
        reportError(QStringLiteral("gvfg_set_channel_video_format"), status, channel);
        return false;
    }
    appendLog(QStringLiteral("CH%1 Output format | %2")
                  .arg(channel)
                  .arg(format == GVFG_PIXFMT_Y210 ? QStringLiteral("Y210") : QStringLiteral("YUY2")));
    return true;
}

void CaptureController::closeDevice()
{
    closeDeviceSession(true);
}

bool CaptureController::prepareAudio(int channelIndex)
{
    ChannelRuntime &channel = channels_[channelIndex];
    channel.audioFormat = {};
    if (!channel.requestedAudio)
        return true;

    appendLog(QStringLiteral("CH%1 [SDK API] Query audio format").arg(channelIndex));
    const gvfg_status_t status =
        gvfg_get_channel_audio_format(handle_, channelIndex, &channel.audioFormat);
    if (status != GVFG_OK)
    {
        reportError(QStringLiteral("gvfg_get_channel_audio_format"), status, channelIndex);
        return false;
    }
    appendLog(QStringLiteral("CH%1 [SDK API] Audio format | %2 Hz %3 ch %4-bit")
                  .arg(channelIndex)
                  .arg(channel.audioFormat.sample_rate)
                  .arg(channel.audioFormat.channels)
                  .arg(channel.audioFormat.bits_per_sample));
    return true;
}

void CaptureController::closeDeviceIfIdle()
{
    if (closingDevice_ || handle_ == nullptr)
        return;

    for (const ChannelRuntime &channel : channels_)
    {
        if (channel.running.load(std::memory_order_acquire))
            return;
    }

    closeDeviceSession(false);
}

void CaptureController::closeDeviceSession(bool clearSelection)
{
    if (closingDevice_)
        return;
    closingDevice_ = true;

    if (runtimeStatusTimer_)
        runtimeStatusTimer_->stop();

    stopAllCaptures();
    for (ChannelRuntime &channel : channels_)
    {
        channel.playback.close();
    }

    if (handle_ != nullptr)
    {
        gvfg_destroy(handle_);
        handle_ = nullptr;
        appendLog(QStringLiteral("Closed device"));
    }

    if (clearSelection)
    {
        emit statusChanged(QStringLiteral("Idle"));
        lastSignalStatusText_.clear();
        emit sdiInfoChanged(QStringLiteral("SDI Info: --"));
        selectedDeviceIndex_ = -1;
    }
    for (ChannelRuntime &channel : channels_)
    {
        channel.opened = false;
        if (clearSelection)
        {
            channel.cachedSignalStatus = {};
            channel.haveCachedSignalStatus = false;
            channel.cachedSdiInfoText.clear();
            channel.lastLoggedInputStatus.clear();
        }
    }
    closingDevice_ = false;
    emit stateChanged();
}

void CaptureController::startCapture(int channelIndex)
{
    if (!isValidChannel(channelIndex))
        return;
    ChannelRuntime &channel = channels_[channelIndex];
    if (channel.running.load(std::memory_order_acquire))
        return;

    if (!openChannel(channelIndex))
    {
        closeDeviceIfIdle();
        return;
    }

    // Consume queued events, then let gvfg_start_channel perform the single
    // fresh signal query used to accept or reject this explicit Start.
    processPendingEvents();

    if (!prepareAudio(channelIndex))
    {
        closeDeviceIfIdle();
        return;
    }
    const bool audioEnabled = channel.requestedAudio;

#if GVFG_INTERNAL_DIAGNOSTICS
    internalDiagnostics_.beginStart(channelIndex);
#endif
    const gvfg_status_t st = gvfg_start_channel(handle_, channelIndex);
#if GVFG_INTERNAL_DIAGNOSTICS
    internalDiagnostics_.finishStartCall(channelIndex);
#endif
    if (st != GVFG_OK)
    {
        reportError(QStringLiteral("gvfg_start_channel"), st, channelIndex);
        if (st == GVFG_ETIMEOUT)
        {
            updateSignalStatus();
            refreshSdiInfo(channelIndex);
        }
        emit previewCloseRequested(channelIndex);
        emit stateChanged();
        closeDeviceIfIdle();
        return;
    }

    // While the SDK channel is running this returns the signal state cached by
    // gvfg_start_channel, without another GigabyteLib/driver read.
    updateSignalStatus();
    refreshSdiInfo(channelIndex);
    if (!channel.haveCachedSignalStatus || !channel.cachedSignalStatus.connected)
    {
        appendLog(QStringLiteral("CH%1 [SDK] Started without cached signal state; stopping")
                      .arg(channelIndex));
        gvfg_stop_channel(handle_, channelIndex);
        closeDeviceIfIdle();
        return;
    }

    channel.frameAvailable.store(false, std::memory_order_release);
    emit previewSourceSizeChanged(channelIndex, channel.cachedSignalStatus.width,
                                  channel.cachedSignalStatus.height);
    emit previewShowRequested(channelIndex);
    if (!applyPreview(channelIndex))
    {
        gvfg_stop_channel(handle_, channelIndex);
        emit previewCloseRequested(channelIndex);
        closeDeviceIfIdle();
        return;
    }

    channel.previewFailureCount = 0;
    channel.audioEnabled = audioEnabled;
    channel.videoFailed = 0;
    channel.lastLoggedPreviewFailures = 0;
    channel.lastLoggedAudioReleaseFailures = channel.lastLoggedAudioOutputFailures = 0;
    channel.lastDeliveryLogMs = 0;
    channel.previewBaseline = {};
    channel.playback.deliveryStats(&channel.previewBaseline);
#if GVFG_INTERNAL_DIAGNOSTICS
    internalDiagnostics_.resetChannel(handle_, channelIndex);
#endif
    channel.signalConnected.store(channel.cachedSignalStatus.connected != 0,
                                  std::memory_order_release);
    channel.stopRequested.store(false, std::memory_order_release);
    channel.running.store(true, std::memory_order_release);
    channel.captureThreadExited.store(false, std::memory_order_release);

    if (!channel.playback.start(channelIndex, channel.audioFormat, audioEnabled,
            [this](const QString &message) { postLog(message); }))
    {
        channel.running = false;
        channel.captureThreadExited = true;
        gvfg_stop_channel(handle_, channelIndex);
        emit previewCloseRequested(channelIndex);
        closeDeviceIfIdle();
        return;
    }

    // 7. Start application-owned read loops last. The video loop always runs;
    // audio playback and its read loop are created only when audio was enabled.
    channel.captureThread = std::thread([this, channelIndex]()
                                        { captureReadLoop(channelIndex); });
    if (audioEnabled)
    {
        channel.audioThread = std::thread([this, channelIndex]()
                                          { audioReadLoop(channelIndex); });
    }
    emit stateChanged();
    appendLog(audioEnabled
                  ? QStringLiteral("CH%1 Started video + audio | %2 Hz %3 ch %4-bit")
                        .arg(channelIndex)
                        .arg(channel.audioFormat.sample_rate)
                        .arg(channel.audioFormat.channels)
                        .arg(channel.audioFormat.bits_per_sample)
                  : QStringLiteral("CH%1 Started video-only").arg(channelIndex));
    updateSignalStatus(false);
}

void CaptureController::stopCapture(int channelIndex)
{
    if (!isValidChannel(channelIndex))
        return;
    ChannelRuntime &channel = channels_[channelIndex];
    if (handle_ != nullptr && channel.running.load(std::memory_order_acquire))
    {
        channel.stopRequested.store(true, std::memory_order_release);
        joinCaptureThread(channelIndex);
        joinAudioThread(channelIndex);
        logDeliveryStatus(channelIndex, true);
        const gvfg_status_t stopStatus = gvfg_stop_channel(handle_, channelIndex);
        if (stopStatus != GVFG_OK)
            reportError(QStringLiteral("gvfg_stop_channel"), stopStatus, channelIndex);
        channel.running.store(false, std::memory_order_release);
        channel.frameAvailable.store(false, std::memory_order_release);
        updateSignalStatus(false);
        channel.audioEnabled = false;
        emit previewCloseRequested(channelIndex);
        appendLog(QStringLiteral("CH%1 Stopped capture").arg(channelIndex));
    }

    channel.frameAvailable.store(false, std::memory_order_release);
    closeDeviceIfIdle();
    if (handle_ != nullptr)
    {
        updateSignalStatus();
        emit stateChanged();
    }
}

void CaptureController::stopAllCaptures()
{
    stopCapture(GVFG_CHANNEL_0);
    stopCapture(GVFG_CHANNEL_1);
}

bool CaptureController::applyPreview(int channelIndex)
{
    if (!isValidChannel(channelIndex))
        return false;
    ChannelRuntime &channel = channels_[channelIndex];
    if (!channel.previewTarget || !channel.playback.attachWindow(channel.previewTarget))
    {
        appendLog(QStringLiteral("CH%1 [PLAYBACK] window setup failed").arg(channelIndex));
        return false;
    }
    return true;
}

void CaptureController::processPendingEvents()
{
    if (handle_ == nullptr)
        return;

    for (int channel = GVFG_CHANNEL_0; channel <= GVFG_CHANNEL_1; ++channel)
    {
        if (!channels_[channel].opened)
            continue;
        gvfg_event_type_t eventType = GVFG_EVENT_UNKNOWN;
        // A zero timeout makes polling non-blocking. Drain every queued event,
        // then query signal/SDI state again when the input state has changed.
        while (gvfg_poll_channel_event(handle_, channel, &eventType, 0) == GVFG_OK)
        {
            appendLog(QStringLiteral("CH%1 [SDK] Event | %2").arg(channel).arg(eventTypeText(eventType)));

            const bool signalChanged =
                eventType == GVFG_EVENT_VIDEO_FORMAT_CHANGED ||
                eventType == GVFG_EVENT_VIDEO_INPUT_PLUGIN ||
                eventType == GVFG_EVENT_VIDEO_INPUT_UNPLUG;
            if (signalChanged)
            {
                if (eventType == GVFG_EVENT_VIDEO_INPUT_UNPLUG)
                    channels_[channel].signalConnected.store(false, std::memory_order_release);
                else if (eventType == GVFG_EVENT_VIDEO_INPUT_PLUGIN)
                    channels_[channel].signalConnected.store(true, std::memory_order_release);
                updateSignalStatus();
                refreshSdiInfo(channel);
                channels_[channel].playback.resetTimeline();
            }


            eventType = GVFG_EVENT_UNKNOWN;
        }
    }

}

void CaptureController::captureReadLoop(int channelIndex)
{
    std::chrono::steady_clock::time_point noFrameSince{};
    std::chrono::steady_clock::time_point nextNoFrameReport{};
    bool captureStalledLogged = false;
    ChannelRuntime &channel = channels_[channelIndex];
    while (!channel.stopRequested.load(std::memory_order_acquire))
    {
        gvfg_frame_t frame{};
#if GVFG_INTERNAL_DIAGNOSTICS
        const auto getFrameStart = std::chrono::steady_clock::now();
#endif
        const gvfg_status_t st = gvfg_read_channel_frame(handle_, channelIndex, &frame, kFrameReadTimeoutMs);
#if GVFG_INTERNAL_DIAGNOSTICS
        const double getFrameElapsedMs =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - getFrameStart)
                .count();
#endif
        if (st == GVFG_OK)
        {
#if GVFG_INTERNAL_DIAGNOSTICS
            internalDiagnostics_.recordVideoRead(channelIndex, getFrameElapsedMs);
#endif
            noFrameSince = {};
            nextNoFrameReport = {};
            if (!channel.frameAvailable.exchange(true, std::memory_order_acq_rel))
            {
                QMetaObject::invokeMethod(this, [this]()
                                          { emit stateChanged(); }, Qt::QueuedConnection);
            }
            if (captureStalledLogged)
            {
                captureStalledLogged = false;
                QMetaObject::invokeMethod(this, [this, channelIndex]()
                                          { appendLog(QStringLiteral("CH%1 [SDK] Video frame delivery resumed").arg(channelIndex)); }, Qt::QueuedConnection);
            }

            const bool attemptedPreview = true; // Always submit timeline, including hidden preview.
            if (attemptedPreview)
            {
                gvfg_preview_frame_t previewFrame{};
                previewFrame.data = frame.data;
                previewFrame.data_size = frame.data_size;
                previewFrame.width = frame.width;
                previewFrame.height = frame.height;
                previewFrame.row_bytes = frame.row_stride_bytes;
                previewFrame.bit_depth = frame.bit_depth;
                previewFrame.frame_id = frame.frame_id;

                switch (frame.pixel_format)
                {
                case GVFG_PIXFMT_YUY2:
                    previewFrame.pixel_format = GVFG_PREVIEW_PIXFMT_YUY2;
                    break;
                case GVFG_PIXFMT_Y210:
                    previewFrame.pixel_format = GVFG_PREVIEW_PIXFMT_Y210;
                    break;
                default:
                    previewFrame.pixel_format = 0;
                    break;
                }

#if GVFG_INTERNAL_DIAGNOSTICS
                const auto previewStart = std::chrono::steady_clock::now();
#endif
                const gvfg_playback_status_t previewStatus =
                    channel.playback.submitVideo(previewFrame, frame.timestamp_ns);
#if GVFG_INTERNAL_DIAGNOSTICS
                const double previewElapsedMs =
                    std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - previewStart)
                        .count();
#endif
                if (previewStatus != GVFG_PLAYBACK_OK)
                {
                    ++channel.videoFailed;
                    channel.stopRequested = true;
                    QMetaObject::invokeMethod(this, [this, channelIndex] { stopCapture(channelIndex); }, Qt::QueuedConnection);
                    const uint64_t failures = ++channel.previewFailureCount;
                    if (failures == 1)
                    {
                        postLog(QStringLiteral("CH%1 [APP] Preview render failed | consecutive=%2 error=%3")
                                    .arg(channelIndex)
                                    .arg(static_cast<qulonglong>(failures))
                                    .arg(QString::fromUtf8(gvfg_playback_strerror(previewStatus))));
                    }
                }
                else
                {
#if GVFG_INTERNAL_DIAGNOSTICS
                    const QString startupLine = internalDiagnostics_.takeStartupLatencyLine(
                        channelIndex, getFrameElapsedMs, previewElapsedMs, frame.frame_id);
                    if (!startupLine.isEmpty())
                    {
                        QMetaObject::invokeMethod(
                            this,
                            [this, startupLine]() { appendLog(startupLine); },
                            Qt::QueuedConnection);
                    }
#endif
                    if (channel.previewFailureCount != 0)
                    {
                        const uint64_t failures = channel.previewFailureCount;
                        channel.previewFailureCount = 0;
                        QMetaObject::invokeMethod(this, [this, channelIndex, failures]()
                                                  { appendLog(QStringLiteral("CH%1 [APP] Preview render recovered | previous_failures=%2")
                                                                  .arg(channelIndex)
                                                                  .arg(static_cast<qulonglong>(failures))); }, Qt::QueuedConnection);
                    }

                }
            }
            const gvfg_status_t releaseStatus = gvfg_release_channel_frame(handle_, channelIndex, &frame);
            if (releaseStatus != GVFG_OK)
            {
                QMetaObject::invokeMethod(this, [this, channelIndex, releaseStatus]()
                                          { reportError(QStringLiteral("gvfg_release_channel_frame"), releaseStatus, channelIndex); }, Qt::QueuedConnection);
                break;
            }

            continue;
        }

        if (st == GVFG_ETIMEOUT)
        {
            const auto now = std::chrono::steady_clock::now();
            if (noFrameSince == std::chrono::steady_clock::time_point{})
            {
                noFrameSince = now;
                nextNoFrameReport = now + std::chrono::seconds(2);
            }
            if (now >= nextNoFrameReport)
            {
                captureStalledLogged = true;
                const uint64_t elapsedMs = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - noFrameSince).count());
                nextNoFrameReport = now + std::chrono::seconds(10);
                QMetaObject::invokeMethod(this, [this, channelIndex, elapsedMs]()
                                          {
                                              appendLog(QStringLiteral("CH%1 [SDK] ERROR no video frame returned | duration_ms=%2")
                                                            .arg(channelIndex)
                                                            .arg(elapsedMs));
#if GVFG_INTERNAL_DIAGNOSTICS
                                              updateSignalStatus(false);
                                              writeDiagnosticSnapshot(lastSignalStatusText_);
#endif
                                          },
                                          Qt::QueuedConnection);
            }
            continue;
        }
        if (channel.stopRequested.load(std::memory_order_acquire))
            break;

        QMetaObject::invokeMethod(this, [this, channelIndex, st]()
                                  { reportError(QStringLiteral("gvfg_read_channel_frame"), st, channelIndex); }, Qt::QueuedConnection);
        break;
    }
    channel.captureThreadExited.store(true, std::memory_order_release);
}

void CaptureController::joinCaptureThread(int channel)
{
    if (channels_[channel].captureThread.joinable())
    {
        // The capture worker can wait behind preview resource updates. Keep
        // DXGI's synchronous HWND messages flowing while joining it.
        while (!channels_[channel].captureThreadExited.load(std::memory_order_acquire))
        {
            MsgWaitForMultipleObjectsEx(0, nullptr, 1, QS_SENDMESSAGE, MWMO_INPUTAVAILABLE);
            MSG message{};
            PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
        }
        channels_[channel].captureThread.join();
    }
}

void CaptureController::audioReadLoop(int channelIndex)
{
    ChannelRuntime &channel = channels_[channelIndex];
    while (!channel.stopRequested.load(std::memory_order_acquire))
    {
        gvfg_audio_frame_t frame{};
        const gvfg_status_t status = gvfg_read_channel_audio_frame(handle_, channelIndex, &frame, 200);
        if (status == GVFG_ETIMEOUT)
            continue;
        if (status != GVFG_OK)
        {
            if (!channel.stopRequested.load(std::memory_order_acquire))
            {
                QMetaObject::invokeMethod(this, [this, channelIndex, status]()
                                          { reportError(QStringLiteral("gvfg_read_channel_audio_frame"), status, channelIndex); }, Qt::QueuedConnection);
            }
            break;
        }

        channel.playback.recordReceivedFrame();
        // Audio frame memory is also owned by the SDK. Copy the PCM payload
        // before release so playback never retains an SDK-owned pointer.
        std::vector<uint8_t> pcm(frame.data_size);
        std::memcpy(pcm.data(), frame.data, frame.data_size);
        const gvfg_status_t releaseStatus =
            gvfg_release_channel_audio_frame(handle_, channelIndex, &frame);
        if (releaseStatus != GVFG_OK)
        {
            channel.playback.recordReleaseFailure();
        }
        if (releaseStatus != GVFG_OK && !channel.stopRequested.load(std::memory_order_acquire))
        {
            QMetaObject::invokeMethod(this, [this, channelIndex, releaseStatus]()
                                          { reportError(QStringLiteral("gvfg_release_channel_audio_frame"), releaseStatus, channelIndex); }, Qt::QueuedConnection);
            break;
        }

        if (releaseStatus == GVFG_OK &&
            !channel.playback.enqueue(std::move(pcm), frame.timestamp_ns))
        {
            channel.stopRequested = true;
            QMetaObject::invokeMethod(this, [this, channelIndex] { stopCapture(channelIndex); }, Qt::QueuedConnection);
            break;
        }
    }
}

void CaptureController::joinAudioThread(int channel)
{
    if (channels_[channel].audioThread.joinable())
        channels_[channel].audioThread.join();
    channels_[channel].playback.stop();
}
