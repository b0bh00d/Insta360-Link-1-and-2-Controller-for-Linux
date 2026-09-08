/*
  insta360link.cpp - implementation of the high-level Insta360 Link controller.
  C++/Qt port of uinsta360link.pas.
*/
#include "insta360link.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>

#include <cerrno>
#include <cstring>

namespace {
/* Uppercase, zero-padded hex, matching Free Pascal's IntToHex(x, width). */
QString hex(quint64 v, int width)
{
    return QStringLiteral("%1").arg(v, width, 16, QLatin1Char('0')).toUpper();
}
}

Insta360Link::Insta360Link(QObject *parent)
    : QObject(parent)
{
}

Insta360Link::~Insta360Link()
{
    close();
}

void Insta360Link::setXuUnitId(quint8 value)
{
    m_xuUnitId = value;
    m_xuUnitIdOverride = true;
}

void Insta360Link::log(const QString &msg)
{
    emit logMessage(msg);
}

QString Insta360Link::errInfo() const
{
    int e = errno;
    return QStringLiteral("errno=%1: %2").arg(e).arg(QString::fromLocal8Bit(std::strerror(e)));
}

/* ===== Control-range caching ===== */

Insta360Link::CtrlRange Insta360Link::queryControlRange(quint32 ctrlId)
{
    CtrlRange r;
    if (!m_connected)
        return r;

    v4l2_queryctrl qc;
    std::memset(&qc, 0, sizeof(qc));
    qc.id = ctrlId;
    if (v4l2::queryCtrl(m_fd, qc)) {
        r.available = (qc.flags & V4L2_CTRL_FLAG_DISABLED) == 0;
        r.min = qc.minimum;
        r.max = qc.maximum;
        r.step = qc.step;
        r.def = qc.default_value;
        v4l2::getCtrl(m_fd, ctrlId, r.cur);
    }
    return r;
}

void Insta360Link::cacheControlRanges()
{
    m_panRange        = queryControlRange(V4L2_CID_PAN_ABSOLUTE);
    m_tiltRange       = queryControlRange(V4L2_CID_TILT_ABSOLUTE);
    m_zoomRange       = queryControlRange(V4L2_CID_ZOOM_ABSOLUTE);
    m_focusRange      = queryControlRange(V4L2_CID_FOCUS_ABSOLUTE);
    m_brightnessRange = queryControlRange(V4L2_CID_BRIGHTNESS);
    m_contrastRange   = queryControlRange(V4L2_CID_CONTRAST);
    m_saturationRange = queryControlRange(V4L2_CID_SATURATION);
    m_sharpnessRange  = queryControlRange(V4L2_CID_SHARPNESS);
    m_gainRange       = queryControlRange(V4L2_CID_GAIN);
    m_wbTempRange     = queryControlRange(V4L2_CID_WHITE_BALANCE_TEMPERATURE);
    m_exposureRange   = queryControlRange(V4L2_CID_EXPOSURE_ABSOLUTE);
}

/* ===== XU unit-id detection ===== */

quint8 Insta360Link::detectXuUnitId()
{
    // Insta360 Link has Extension Units at bUnitID 9, 10, 11 (and some
    // firmware exposes 4, 3, 6). Unit 9 carries the main proprietary controls.
    static const quint8 ids[] = {9, 10, 11, 4, 3, 6};
    quint8 buf[32];

    log(QStringLiteral("UVCIOC_CTRL_QUERY = $%1").arg(hex(UVCIOC_CTRL_QUERY, 8)));

    // First pass: UVC_GET_INFO on selector 1 - always returns exactly 1 byte.
    for (quint8 id : ids) {
        std::memset(buf, 0, sizeof(buf));
        if (v4l2::xuQuery(m_fd, id, 1, UVC_GET_INFO, buf, 1)) {
            log(QStringLiteral("XU Unit ID detected: %1 (via GET_INFO, flags=$%2)")
                    .arg(id).arg(hex(buf[0], 2)));
            return id;
        }
        int err = errno;
        log(QStringLiteral("  XU probe unit %1 failed, errno=%2 (%3)")
                .arg(id).arg(err).arg(QString::fromLocal8Bit(std::strerror(err))));
    }

    // Second pass: UVC_GET_LEN returns 2 bytes.
    for (quint8 id : ids) {
        std::memset(buf, 0, sizeof(buf));
        if (v4l2::xuQuery(m_fd, id, 1, UVC_GET_LEN, buf, 2)) {
            log(QStringLiteral("XU Unit ID detected: %1 (via GET_LEN)").arg(id));
            return id;
        }
    }

    log(QStringLiteral("WARNING: Could not detect XU Unit ID, defaulting to 9"));
    return 9;
}

/* ===== XU selector scan ===== */

void Insta360Link::scanXuSelectors()
{
    static const int units[] = {9, 10, 11};

    for (int unitId : units) {
        log(QStringLiteral("--- Scanning XU unit %1 ---").arg(unitId));
        for (int sel = 1; sel <= 20; ++sel) {
            quint8 lenBuf[2] = {0, 0};
            if (!v4l2::xuQuery(m_fd, unitId, sel, UVC_GET_LEN, lenBuf, 2)) {
                int err = errno;
                if (err != EINVAL && err != ENOENT)
                    log(QStringLiteral("  Sel %1: failed errno=%2 (%3)")
                            .arg(sel, 2).arg(err)
                            .arg(QString::fromLocal8Bit(std::strerror(err))));
                continue;
            }

            quint16 dataLen = quint16(lenBuf[0] | (lenBuf[1] << 8));
            if (unitId == m_xuUnitId && sel >= 1 && sel <= 20)
                m_xuLens[sel] = dataLen;

            quint8 infoBuf = 0;
            v4l2::xuQuery(m_fd, unitId, sel, UVC_GET_INFO, &infoBuf, 1);

            QString hexStr;
            if (dataLen <= 16 && (infoBuf & 1) != 0) {
                quint8 dataBuf[16];
                std::memset(dataBuf, 0, sizeof(dataBuf));
                if (v4l2::xuQuery(m_fd, unitId, sel, UVC_GET_CUR, dataBuf, dataLen)) {
                    hexStr = QStringLiteral(" cur=[");
                    for (int j = 0; j < dataLen; ++j) {
                        if (j > 0)
                            hexStr += QLatin1Char(' ');
                        hexStr += hex(dataBuf[j], 2);
                    }
                    hexStr += QLatin1Char(']');
                }
            }
            log(QStringLiteral("  Sel %1: len=%2 flags=$%3 (GET=%4 SET=%5)%6")
                    .arg(sel, 2).arg(dataLen, 3).arg(hex(infoBuf, 2))
                    .arg((infoBuf & 1) ? QStringLiteral("Y") : QStringLiteral("N"))
                    .arg((infoBuf & 2) ? QStringLiteral("Y") : QStringLiteral("N"))
                    .arg(hexStr));
        }
    }
    log(QStringLiteral("--- End XU scan ---"));
}

/* ===== XU snapshot (for reverse engineering) ===== */

void Insta360Link::dumpAllXU()
{
    static const int units[] = {9, 10, 11};

    for (int unitId : units) {
        log(QStringLiteral("--- Unit %1 snapshot ---").arg(unitId));
        for (int sel = 1; sel <= 20; ++sel) {
            quint16 dataLen = 0;
            if (unitId == m_xuUnitId && sel >= 1 && sel <= 20)
                dataLen = m_xuLens[sel];

            if (dataLen == 0) {
                quint8 lenBuf[2] = {0, 0};
                if (v4l2::xuQuery(m_fd, unitId, sel, UVC_GET_LEN, lenBuf, 2))
                    dataLen = quint16(lenBuf[0] | (lenBuf[1] << 8));
                else
                    continue; // selector doesn't exist
            }
            if (dataLen > 512)
                dataLen = 512;

            quint8 infoBuf = 0;
            v4l2::xuQuery(m_fd, unitId, sel, UVC_GET_INFO, &infoBuf, 1);
            if ((infoBuf & 1) == 0) {
                log(QStringLiteral("  U%1 S%2 [%3]: (SET-only)")
                        .arg(unitId).arg(sel, 2).arg(dataLen, 3));
                continue;
            }

            QByteArray dataBuf(dataLen, '\0');
            if (v4l2::xuQuery(m_fd, unitId, sel, UVC_GET_CUR, dataBuf.data(), dataLen)) {
                int showBytes = qMin<int>(dataLen, 32);
                QString hexStr;
                for (int j = 0; j < showBytes; ++j) {
                    if (j > 0)
                        hexStr += QLatin1Char(' ');
                    hexStr += hex(quint8(dataBuf[j]), 2);
                }
                if (dataLen > 32)
                    hexStr += QStringLiteral(" ... (%1 more)").arg(dataLen - 32);
                log(QStringLiteral("  U%1 S%2 [%3]: %4")
                        .arg(unitId).arg(sel, 2).arg(dataLen, 3).arg(hexStr));
            }
        }
    }
}

/* ===== Padded XU helpers ===== */

bool Insta360Link::xuSetPadded(quint8 selector, const QByteArray &data)
{
    if (selector < 1 || selector > 20)
        return false;
    quint16 expectedLen = m_xuLens[selector];
    if (expectedLen == 0) {
        log(QStringLiteral("xuSetPadded: selector %1 has no cached length "
                           "(not found in scan)").arg(selector));
        return false;
    }
    QByteArray buf(expectedLen, '\0');
    int copyLen = qMin<int>(data.size(), expectedLen);
    std::memcpy(buf.data(), data.constData(), copyLen);
    return v4l2::xuSetCur(m_fd, m_xuUnitId, selector, buf.constData(), expectedLen);
}

bool Insta360Link::xuGetPadded(quint8 selector, QByteArray &data)
{
    if (selector < 1 || selector > 20)
        return false;
    quint16 expectedLen = m_xuLens[selector];
    if (expectedLen == 0) {
        log(QStringLiteral("xuGetPadded: selector %1 has no cached length "
                           "(not found in scan)").arg(selector));
        return false;
    }
    QByteArray buf(expectedLen, '\0');
    if (!v4l2::xuGetCur(m_fd, m_xuUnitId, selector, buf.data(), expectedLen))
        return false;
    int copyLen = qMin<int>(data.size(), expectedLen);
    std::memcpy(data.data(), buf.constData(), copyLen);
    return true;
}

bool Insta360Link::xuSetMode(quint8 modeId, quint8 modeFlag)
{
    if (!m_connected)
        return false;

    QByteArray buf(52, '\0');
    buf[0] = char(modeId);
    buf[1] = char(modeFlag);
    bool ok = xuSetPadded(xu::ModeControl, buf);
    if (ok)
        log(QStringLiteral("XU Mode SET: byte[0]=$%1 byte[1]=$%2")
                .arg(hex(modeId, 2), hex(modeFlag, 2)));
    else
        log(QStringLiteral("XU Mode SET FAILED (%1)").arg(errInfo()));
    return ok;
}

/* ===== Connection ===== */

bool Insta360Link::open(const QString &devPath)
{
    close();

    m_fd = v4l2::openDevice(devPath);
    if (m_fd < 0) {
        log(QStringLiteral("ERROR: Cannot open %1: %2")
                .arg(devPath, QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }

    m_devicePath = devPath;

    v4l2_capability cap;
    if (v4l2::queryCap(m_fd, cap)) {
        m_deviceName = QString::fromLatin1(reinterpret_cast<const char *>(cap.card));
        m_driverName = QString::fromLatin1(reinterpret_cast<const char *>(cap.driver));
        m_busInfo = QString::fromLatin1(reinterpret_cast<const char *>(cap.bus_info));
        log(QStringLiteral("Connected to: %1").arg(m_deviceName));
        log(QStringLiteral("Driver: %1  Bus: %2").arg(m_driverName, m_busInfo));
    } else {
        log(QStringLiteral("WARNING: QUERYCAP failed"));
    }

    m_connected = true;

    // Detect camera model from the USB product id via sysfs.
    m_cameraModel = CameraModel::Unknown;
    const QString vidName = QFileInfo(devPath).fileName();  // e.g. "video0"
    QFile pidFile(QStringLiteral("/sys/class/video4linux/%1/device/../idProduct").arg(vidName));
    if (pidFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString pid = QString::fromLatin1(pidFile.readLine()).trimmed().toLower();
        if (pid == QLatin1String("4c01"))
            m_cameraModel = CameraModel::Link;
        else if (pid == QLatin1String("4c04"))
            m_cameraModel = CameraModel::Link2;
    }

    switch (m_cameraModel) {
    case CameraModel::Link:    log(QStringLiteral("Camera model: Insta360 Link")); break;
    case CameraModel::Link2:   log(QStringLiteral("Camera model: Insta360 Link 2")); break;
    case CameraModel::Unknown: log(QStringLiteral("Camera model: Unknown")); break;
    }

    // Respect a caller-supplied unit id; otherwise auto-detect it.
    if (!m_xuUnitIdOverride)
        m_xuUnitId = detectXuUnitId();
    else
        log(QStringLiteral("Using requested XU unit ID: %1").arg(m_xuUnitId));

    scanXuSelectors();
    cacheControlRanges();

    if (m_panRange.available)
        log(QStringLiteral("Pan: %1..%2 (step %3)")
                .arg(m_panRange.min).arg(m_panRange.max).arg(m_panRange.step));
    if (m_tiltRange.available)
        log(QStringLiteral("Tilt: %1..%2 (step %3)")
                .arg(m_tiltRange.min).arg(m_tiltRange.max).arg(m_tiltRange.step));
    if (m_zoomRange.available)
        log(QStringLiteral("Zoom: %1..%2 (step %3)")
                .arg(m_zoomRange.min).arg(m_zoomRange.max).arg(m_zoomRange.step));

    return true;
}

void Insta360Link::close()
{
    if (m_fd >= 0) {
        v4l2::closeDevice(m_fd);
        m_fd = -1;
        m_connected = false;
        log(QStringLiteral("Disconnected from %1").arg(m_devicePath));
        m_devicePath.clear();
        m_deviceName.clear();
    }
}

/* ===== PTZ ===== */

bool Insta360Link::setPanAbsolute(qint32 value)
{
    if (!m_connected)
        return false;

    bool ok = v4l2::setCtrl(m_fd, V4L2_CID_PAN_ABSOLUTE, value);
    if (!ok)
        ok = v4l2::setPanTilt(m_fd, value, m_tiltPos);

    if (ok) {
        m_panPos = value;
        log(QStringLiteral("Pan absolute: %1").arg(value));
    } else {
        log(QStringLiteral("Pan absolute FAILED"));
    }
    return ok;
}

qint32 Insta360Link::getPanAbsolute() const
{
    return m_panPos;
}

bool Insta360Link::setTiltAbsolute(qint32 value)
{
    if (!m_connected)
        return false;

    bool ok = v4l2::setCtrl(m_fd, V4L2_CID_TILT_ABSOLUTE, value);
    if (!ok)
        ok = v4l2::setPanTilt(m_fd, m_panPos, value);

    if (ok) {
        m_tiltPos = value;
        log(QStringLiteral("Tilt absolute: %1").arg(value));
    } else {
        log(QStringLiteral("Tilt absolute FAILED"));
    }
    return ok;
}

qint32 Insta360Link::getTiltAbsolute() const
{
    return m_tiltPos;
}

bool Insta360Link::panTiltRelative(qint32 panDelta, qint32 tiltDelta)
{
    if (!m_connected)
        return false;

    qint32 newPan  = m_panPos  + panDelta  * 3600;
    qint32 newTilt = m_tiltPos + tiltDelta * 3600;

    if (m_panRange.available) {
        newPan = qBound(m_panRange.min, newPan, m_panRange.max);
    }
    if (m_tiltRange.available) {
        newTilt = qBound(m_tiltRange.min, newTilt, m_tiltRange.max);
    }

    // Combined set works on both Link and Link 2.
    bool ok = v4l2::setPanTilt(m_fd, newPan, newTilt);
    if (!ok) {
        v4l2::setCtrl(m_fd, V4L2_CID_PAN_ABSOLUTE, newPan);
        ok = v4l2::setCtrl(m_fd, V4L2_CID_TILT_ABSOLUTE, newTilt);
    }

    if (ok) {
        log(QStringLiteral("Pan/Tilt: pan=%1->%2 tilt=%3->%4")
                .arg(m_panPos).arg(newPan).arg(m_tiltPos).arg(newTilt));
        m_panPos = newPan;
        m_tiltPos = newTilt;
    } else {
        log(QStringLiteral("Pan/Tilt FAILED (%1)").arg(errInfo()));
    }
    return ok;
}

bool Insta360Link::panTiltStop()
{
    return panTiltRelative(0, 0);
}

bool Insta360Link::setZoom(qint32 value)
{
    bool ok = m_connected && v4l2::setCtrl(m_fd, V4L2_CID_ZOOM_ABSOLUTE, value);
    log(ok ? QStringLiteral("Zoom: %1").arg(value) : QStringLiteral("Zoom FAILED"));
    return ok;
}

qint32 Insta360Link::getZoom()
{
    qint32 result = 100;
    if (m_connected)
        v4l2::getCtrl(m_fd, V4L2_CID_ZOOM_ABSOLUTE, result);
    return result;
}

bool Insta360Link::gimbalReset()
{
    if (!m_connected)
        return false;

    // Try the XU reset command (SET-only, 1 byte).
    xuSetPadded(xu::GimbalResetControl, QByteArray(1, '\x01'));

    bool ok = v4l2::setPanTilt(m_fd, 0, 0);
    if (!ok) {
        v4l2::setCtrl(m_fd, V4L2_CID_PAN_ABSOLUTE, 0);
        ok = v4l2::setCtrl(m_fd, V4L2_CID_TILT_ABSOLUTE, 0);
    }

    if (ok) {
        m_panPos = 0;
        m_tiltPos = 0;
        log(QStringLiteral("Gimbal reset to center"));
    } else {
        log(QStringLiteral("Gimbal reset FAILED"));
    }
    return ok;
}

/* ===== AI Tracking ===== */

bool Insta360Link::setAiTracking(bool enable)
{
    if (!m_connected)
        return false;

    bool ok = enable ? xuSetMode(xu::ModeAiTracking, xu::FlagAiTracking)
                     : xuSetMode(xu::ModeOff, 0);

    if (ok) {
        m_aiTrackingEnabled = enable;
        if (enable) {
            m_currentMode = CameraMode::Normal; // AI tracking is an overlay, not a mode
            log(QStringLiteral("AI Tracking: ENABLED"));
        } else {
            log(QStringLiteral("AI Tracking: DISABLED"));
        }
    } else {
        log(QStringLiteral("AI Tracking FAILED"));
    }
    return ok;
}

bool Insta360Link::getAiTracking()
{
    if (!m_connected)
        return false;
    QByteArray buf(52, '\0');
    if (xuGetPadded(xu::ModeControl, buf))
        return quint8(buf[0]) == xu::ModeAiTracking;
    return false;
}

bool Insta360Link::setTrackingFrame(TrackingFrame frame)
{
    if (!m_connected)
        return false;

    quint8 data;
    switch (frame) {
    case TrackingFrame::Head:     data = xu::FrameHead; break;
    case TrackingFrame::HalfBody: data = xu::FrameHalfBody; break;
    case TrackingFrame::FullBody: data = xu::FrameFullBody; break;
    default:                      data = xu::FrameHalfBody; break;
    }

    log(QStringLiteral("setTrackingFrame: writing $%1 to Sel %2 (len=%3)")
            .arg(hex(data, 2)).arg(xu::TrackingFrameControl)
            .arg(m_xuLens[xu::TrackingFrameControl]));

    bool ok = xuSetPadded(xu::TrackingFrameControl, QByteArray(1, char(data)));
    if (ok) {
        m_trackingFrame = frame;
        switch (frame) {
        case TrackingFrame::Head:     log(QStringLiteral("Tracking frame: HEAD")); break;
        case TrackingFrame::HalfBody: log(QStringLiteral("Tracking frame: HALF BODY")); break;
        case TrackingFrame::FullBody: log(QStringLiteral("Tracking frame: FULL BODY")); break;
        }
    } else {
        log(QStringLiteral("Tracking frame FAILED (%1)").arg(errInfo()));
    }
    return ok;
}

Insta360Link::TrackingFrame Insta360Link::getTrackingFrame()
{
    TrackingFrame result = TrackingFrame::HalfBody;
    if (!m_connected)
        return result;

    QByteArray data(1, '\0');
    if (xuGetPadded(xu::TrackingFrameControl, data)) {
        switch (quint8(data[0])) {
        case xu::FrameHead:     result = TrackingFrame::Head; break;
        case xu::FrameHalfBody: result = TrackingFrame::HalfBody; break;
        case xu::FrameFullBody: result = TrackingFrame::FullBody; break;
        }
        m_trackingFrame = result;
    }
    return result;
}

bool Insta360Link::setTrackingTarget(TrackingTarget target)
{
    if (!m_connected)
        return false;

    // Query the actual data length for XU-10 Sel 1.
    quint8 lenBuf[2] = {0, 0};
    quint16 dataLen;
    if (v4l2::xuQuery(m_fd, xu::TrackingTargetUnit, xu::TrackingTargetControl,
                      UVC_GET_LEN, lenBuf, 2))
        dataLen = quint16(lenBuf[0] | (lenBuf[1] << 8));
    else
        dataLen = 8; // default seen on Windows

    if (dataLen > 8)
        dataLen = 8;

    log(QStringLiteral("XU-10 Sel 1: detected length = %1").arg(dataLen));

    quint8 buf[8];
    std::memset(buf, 0, sizeof(buf));
    if (!v4l2::xuGetCur(m_fd, xu::TrackingTargetUnit, xu::TrackingTargetControl,
                        buf, dataLen)) {
        log(QStringLiteral("Tracking target: read FAILED (%1)").arg(errInfo()));
        std::memset(buf, 0, sizeof(buf));
    } else {
        log(QStringLiteral("XU-10 Sel 1 read: %1 %2 %3 %4 %5 %6 %7 %8")
                .arg(hex(buf[0], 2), hex(buf[1], 2), hex(buf[2], 2), hex(buf[3], 2),
                     hex(buf[4], 2), hex(buf[5], 2), hex(buf[6], 2), hex(buf[7], 2)));
    }

    buf[4] = (target == TrackingTarget::Single) ? xu::TargetSingle : xu::TargetGroup;

    bool ok = v4l2::xuSetCur(m_fd, xu::TrackingTargetUnit, xu::TrackingTargetControl,
                             buf, dataLen);
    if (ok) {
        m_trackingTarget = target;
        log(target == TrackingTarget::Single ? QStringLiteral("Tracking target: SINGLE")
                                             : QStringLiteral("Tracking target: GROUP"));
    } else {
        log(QStringLiteral("Tracking target FAILED (%1)").arg(errInfo()));
    }
    return ok;
}

Insta360Link::TrackingTarget Insta360Link::getTrackingTarget()
{
    TrackingTarget result = TrackingTarget::Single;
    if (!m_connected)
        return result;

    quint8 lenBuf[2] = {0, 0};
    quint16 dataLen;
    if (v4l2::xuQuery(m_fd, xu::TrackingTargetUnit, xu::TrackingTargetControl,
                      UVC_GET_LEN, lenBuf, 2))
        dataLen = quint16(lenBuf[0] | (lenBuf[1] << 8));
    else
        dataLen = 8;
    if (dataLen > 8)
        dataLen = 8;

    quint8 buf[8];
    std::memset(buf, 0, sizeof(buf));
    if (v4l2::xuGetCur(m_fd, xu::TrackingTargetUnit, xu::TrackingTargetControl,
                       buf, dataLen)) {
        switch (buf[4]) {
        case xu::TargetSingle: result = TrackingTarget::Single; break;
        case xu::TargetGroup:  result = TrackingTarget::Group; break;
        }
        m_trackingTarget = result;
    }
    return result;
}

/* ===== Special modes ===== */

bool Insta360Link::setCameraMode(CameraMode mode)
{
    if (!m_connected)
        return false;

    xuSetMode(xu::ModeOff, 0);
    m_aiTrackingEnabled = false;

    bool ok = false;
    switch (mode) {
    case CameraMode::Normal:
        ok = true;
        log(QStringLiteral("Mode: Normal"));
        break;
    case CameraMode::DeskView:   ok = setDeskView(true); break;
    case CameraMode::Whiteboard: ok = setWhiteboard(true); break;
    case CameraMode::Overhead:   ok = setOverhead(true); break;
    }

    if (ok)
        m_currentMode = mode;
    return ok;
}

bool Insta360Link::setDeskView(bool enable)
{
    if (!m_connected)
        return false;

    bool ok = enable ? xuSetMode(xu::ModeDeskView, xu::FlagDeskView)
                     : xuSetMode(xu::ModeOff, 0);
    if (ok) {
        if (enable) {
            m_currentMode = CameraMode::DeskView;
            m_aiTrackingEnabled = false;
            log(QStringLiteral("DeskView: ENABLED"));
        } else {
            log(QStringLiteral("DeskView: DISABLED"));
        }
    } else {
        log(QStringLiteral("DeskView FAILED"));
    }
    return ok;
}

bool Insta360Link::setWhiteboard(bool enable)
{
    if (!m_connected)
        return false;

    bool ok = enable ? xuSetMode(xu::ModeWhiteboard, xu::FlagWhiteboard)
                     : xuSetMode(xu::ModeOff, 0);
    if (ok) {
        if (enable) {
            m_currentMode = CameraMode::Whiteboard;
            m_aiTrackingEnabled = false;
            log(QStringLiteral("Whiteboard: ENABLED"));
        } else {
            log(QStringLiteral("Whiteboard: DISABLED"));
        }
    } else {
        log(QStringLiteral("Whiteboard FAILED"));
    }
    return ok;
}

bool Insta360Link::setOverhead(bool enable)
{
    if (!m_connected)
        return false;

    bool ok = enable ? xuSetMode(xu::ModeOverhead, xu::FlagOverhead)
                     : xuSetMode(xu::ModeOff, 0);
    if (ok) {
        if (enable) {
            m_currentMode = CameraMode::Overhead;
            m_aiTrackingEnabled = false;
            log(QStringLiteral("Overhead: ENABLED"));
        } else {
            log(QStringLiteral("Overhead: DISABLED"));
        }
    } else {
        log(QStringLiteral("Overhead FAILED"));
    }
    return ok;
}

/* ===== Image controls ===== */

bool Insta360Link::setBrightness(qint32 v)
{ return m_connected && v4l2::setCtrl(m_fd, V4L2_CID_BRIGHTNESS, v); }
qint32 Insta360Link::getBrightness()
{ qint32 r = 0; if (m_connected) v4l2::getCtrl(m_fd, V4L2_CID_BRIGHTNESS, r); return r; }

bool Insta360Link::setContrast(qint32 v)
{ return m_connected && v4l2::setCtrl(m_fd, V4L2_CID_CONTRAST, v); }
qint32 Insta360Link::getContrast()
{ qint32 r = 0; if (m_connected) v4l2::getCtrl(m_fd, V4L2_CID_CONTRAST, r); return r; }

bool Insta360Link::setSaturation(qint32 v)
{ return m_connected && v4l2::setCtrl(m_fd, V4L2_CID_SATURATION, v); }
qint32 Insta360Link::getSaturation()
{ qint32 r = 0; if (m_connected) v4l2::getCtrl(m_fd, V4L2_CID_SATURATION, r); return r; }

bool Insta360Link::setSharpness(qint32 v)
{ return m_connected && v4l2::setCtrl(m_fd, V4L2_CID_SHARPNESS, v); }
qint32 Insta360Link::getSharpness()
{ qint32 r = 0; if (m_connected) v4l2::getCtrl(m_fd, V4L2_CID_SHARPNESS, r); return r; }

bool Insta360Link::setGain(qint32 v)
{ return m_connected && v4l2::setCtrl(m_fd, V4L2_CID_GAIN, v); }
qint32 Insta360Link::getGain()
{ qint32 r = 0; if (m_connected) v4l2::getCtrl(m_fd, V4L2_CID_GAIN, r); return r; }

bool Insta360Link::setBacklightCompensation(bool enable)
{ return m_connected && v4l2::setCtrl(m_fd, V4L2_CID_BACKLIGHT_COMPENSATION, enable ? 1 : 0); }
bool Insta360Link::getBacklightCompensation()
{
    qint32 v;
    if (m_connected && v4l2::getCtrl(m_fd, V4L2_CID_BACKLIGHT_COMPENSATION, v))
        return v != 0;
    return false;
}

bool Insta360Link::setAutoWhiteBalance(bool enable)
{ return m_connected && v4l2::setCtrl(m_fd, V4L2_CID_AUTO_WHITE_BALANCE, enable ? 1 : 0); }
bool Insta360Link::getAutoWhiteBalance()
{
    qint32 v;
    if (m_connected && v4l2::getCtrl(m_fd, V4L2_CID_AUTO_WHITE_BALANCE, v))
        return v != 0;
    return true;
}

bool Insta360Link::setWhiteBalanceTemp(qint32 v)
{ return m_connected && v4l2::setCtrl(m_fd, V4L2_CID_WHITE_BALANCE_TEMPERATURE, v); }
qint32 Insta360Link::getWhiteBalanceTemp()
{
    qint32 r = 4000;
    if (m_connected)
        v4l2::getCtrl(m_fd, V4L2_CID_WHITE_BALANCE_TEMPERATURE, r);
    return r;
}

bool Insta360Link::setExposureAuto(bool enable)
{
    if (!m_connected)
        return false;
    return v4l2::setCtrl(m_fd, V4L2_CID_EXPOSURE_AUTO,
                         enable ? V4L2_EXPOSURE_APERTURE_PRIORITY : V4L2_EXPOSURE_MANUAL);
}
bool Insta360Link::getExposureAuto()
{
    qint32 v;
    if (m_connected && v4l2::getCtrl(m_fd, V4L2_CID_EXPOSURE_AUTO, v))
        return v != V4L2_EXPOSURE_MANUAL;
    return true;
}

bool Insta360Link::setExposureAbsolute(qint32 v)
{ return m_connected && v4l2::setCtrl(m_fd, V4L2_CID_EXPOSURE_ABSOLUTE, v); }
qint32 Insta360Link::getExposureAbsolute()
{
    qint32 r = 250;
    if (m_connected)
        v4l2::getCtrl(m_fd, V4L2_CID_EXPOSURE_ABSOLUTE, r);
    return r;
}

bool Insta360Link::setAutoFocus(bool enable)
{ return m_connected && v4l2::setCtrl(m_fd, V4L2_CID_FOCUS_AUTO, enable ? 1 : 0); }
bool Insta360Link::getAutoFocus()
{
    qint32 v;
    if (m_connected && v4l2::getCtrl(m_fd, V4L2_CID_FOCUS_AUTO, v))
        return v != 0;
    return true;
}

bool Insta360Link::setFocusAbsolute(qint32 v)
{ return m_connected && v4l2::setCtrl(m_fd, V4L2_CID_FOCUS_ABSOLUTE, v); }
qint32 Insta360Link::getFocusAbsolute()
{
    qint32 r = 0;
    if (m_connected)
        v4l2::getCtrl(m_fd, V4L2_CID_FOCUS_ABSOLUTE, r);
    return r;
}

/* ===== Presets (software) ===== */

bool Insta360Link::savePreset(quint8 index)
{
    if (!m_connected || index > 5)
        return false;

    // Use tracked positions for pan/tilt (V4L2 reads can be unreliable on
    // Link 2); read zoom directly.
    qint32 z = 100;
    v4l2::getCtrl(m_fd, V4L2_CID_ZOOM_ABSOLUTE, z);
    m_presets[index].pan = m_panPos;
    m_presets[index].tilt = m_tiltPos;
    m_presets[index].zoom = z;
    m_presets[index].valid = true;
    log(QStringLiteral("Preset %1 SAVED: pan=%2 tilt=%3 zoom=%4")
            .arg(index).arg(m_panPos).arg(m_tiltPos).arg(z));
    return true;
}

bool Insta360Link::recallPreset(quint8 index)
{
    if (!m_connected || index > 5)
        return false;
    if (!m_presets[index].valid) {
        log(QStringLiteral("Preset %1 not saved yet").arg(index));
        return false;
    }

    bool ok = v4l2::setPanTilt(m_fd, m_presets[index].pan, m_presets[index].tilt);
    if (!ok) {
        v4l2::setCtrl(m_fd, V4L2_CID_PAN_ABSOLUTE, m_presets[index].pan);
        v4l2::setCtrl(m_fd, V4L2_CID_TILT_ABSOLUTE, m_presets[index].tilt);
        ok = true; // best effort
    }
    v4l2::setCtrl(m_fd, V4L2_CID_ZOOM_ABSOLUTE, m_presets[index].zoom);
    m_panPos = m_presets[index].pan;
    m_tiltPos = m_presets[index].tilt;
    if (ok)
        log(QStringLiteral("Preset %1 RECALLED: pan=%2 tilt=%3 zoom=%4")
                .arg(index).arg(m_presets[index].pan).arg(m_presets[index].tilt)
                .arg(m_presets[index].zoom));
    else
        log(QStringLiteral("Recall preset %1 FAILED").arg(index));
    return ok;
}

Insta360Link::PresetPosition Insta360Link::getPreset(quint8 index) const
{
    if (index <= 5)
        return m_presets[index];
    return PresetPosition();
}

void Insta360Link::setPreset(quint8 index, const PresetPosition &preset)
{
    if (index <= 5)
        m_presets[index] = preset;
}

/* ===== Utility ===== */

QStringList Insta360Link::enumerateControls()
{
    QStringList result;
    if (!m_connected)
        return result;

    auto scan = [&](quint32 base, quint32 count) {
        for (quint32 id = base; id < base + count; ++id) {
            v4l2_queryctrl qc;
            std::memset(&qc, 0, sizeof(qc));
            qc.id = id;
            if (!v4l2::queryCtrl(m_fd, qc))
                continue;
            if (qc.flags & V4L2_CTRL_FLAG_DISABLED)
                continue;
            qint32 val = 0;
            v4l2::getCtrl(m_fd, id, val);
            result.append(QStringLiteral("%1: val=%2  min=%3  max=%4  step=%5  def=%6")
                              .arg(QString::fromLatin1(reinterpret_cast<const char *>(qc.name)), -30)
                              .arg(val).arg(qc.minimum).arg(qc.maximum)
                              .arg(qc.step).arg(qc.default_value));
        }
    };

    scan(V4L2_CID_BASE, 100);
    scan(V4L2_CID_CAMERA_CLASS_BASE, 50);
    return result;
}

bool Insta360Link::rawXuSet(quint8 selector, const QByteArray &data)
{
    return v4l2::xuSetCur(m_fd, m_xuUnitId, selector, data.constData(), quint16(data.size()));
}

bool Insta360Link::rawXuGet(quint8 selector, QByteArray &data, quint16 len)
{
    if (data.size() < len)
        data.resize(len);
    return v4l2::xuGetCur(m_fd, m_xuUnitId, selector, data.data(), len);
}
