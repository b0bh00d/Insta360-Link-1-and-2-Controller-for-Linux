/*
  insta360link.h - High-level Insta360 Link Camera Controller
  ==================================================================
  C++/Qt port of uinsta360link.pas.

  Wraps V4L2 standard controls and UVC Extension Unit commands into a
  clean API for controlling all Insta360 Link / Link 2 features:
    - Pan / Tilt / Zoom (absolute & relative)
    - AI Tracking (XU Selector 2 mode control)
    - DeskView / Whiteboard / Overhead special modes
    - Image settings (brightness, contrast, saturation, ...)
    - Exposure and Focus (auto / manual)
    - Software preset positions (save & recall)
    - Gimbal reset

  XU Selector Map (Unit 9, GUID faf1672d-..., confirmed via Windows KS
  property monitoring against the official Link Controller):

    Selector 2 (52 bytes) = master mode control
      byte[0]=0x01, byte[1]=0x00 -> AI Tracking
      byte[0]=0x04, byte[1]=0x01 -> Whiteboard
      byte[0]=0x05, byte[1]=0x03 -> Overhead
      byte[0]=0x06, byte[1]=0x10 -> DeskView
      byte[0]=0x00               -> Off / Normal

  Instead of the Pascal "OnLog" event-of-object, log output is delivered
  through the Qt signal logMessage(const QString &).
*/
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include "v4l2.h"

/* ---- Insta360 Link XU selectors / mode ids (reverse-engineered) ---- */
namespace xu {
constexpr quint8 PanTiltRelativeControl = 13; // pan/tilt relative (129 bytes)
constexpr quint8 GimbalResetControl     = 14; // reset gimbal to center (1 byte, SET-only)
constexpr quint8 ModeControl            = 2;  // master mode control (52-byte buffer)

// Mode ids for ModeControl byte[0]
constexpr quint8 ModeOff        = 0x00;
constexpr quint8 ModeAiTracking = 0x01;
constexpr quint8 ModeWhiteboard = 0x04;
constexpr quint8 ModeOverhead   = 0x05;
constexpr quint8 ModeDeskView   = 0x06;

// Mode flags for ModeControl byte[1]
constexpr quint8 FlagAiTracking = 0x00;
constexpr quint8 FlagWhiteboard = 0x01;
constexpr quint8 FlagOverhead   = 0x03;
constexpr quint8 FlagDeskView   = 0x10;

// Tracking framing mode (Selector 19, 1-byte value)
constexpr quint8 TrackingFrameControl = 19;
constexpr quint8 FrameHead     = 0x01;
constexpr quint8 FrameHalfBody = 0x02;
constexpr quint8 FrameFullBody = 0x03;

// Tracking target mode (XU-10, Selector 1, 8-byte buffer, byte[4])
constexpr quint8 TrackingTargetUnit    = 10;
constexpr quint8 TrackingTargetControl = 1;
constexpr quint8 TargetSingle = 0x00;
constexpr quint8 TargetGroup  = 0x01;
} // namespace xu

class Insta360Link : public QObject
{
    Q_OBJECT

public:
    enum class CameraMode  { Normal, DeskView, Whiteboard, Overhead };
    enum class CameraModel  { Unknown, Link, Link2 };
    enum class TrackingFrame  { Head, HalfBody, FullBody };
    enum class TrackingTarget { Single, Group };

    /* Describes a control's available range. */
    struct CtrlRange {
        bool   available = false;
        qint32 min = 0, max = 0, step = 0, def = 0, cur = 0;
    };

    /* A software preset position. */
    struct PresetPosition {
        QString name;
        qint32  pan = 0, tilt = 0, zoom = 100;
        bool    valid = false;
    };

    explicit Insta360Link(QObject *parent = nullptr);
    ~Insta360Link() override;

    /* ---- Connection ---- */
    bool open(const QString &devPath);
    void close();

    /* ---- PTZ ---- */
    bool setPanAbsolute(qint32 value);
    qint32 getPanAbsolute() const;
    bool setTiltAbsolute(qint32 value);
    qint32 getTiltAbsolute() const;
    /* Move pan/tilt relative to the current (software-tracked) position.
       PanDelta/TiltDelta are in whole degrees; internally scaled by 3600. */
    bool panTiltRelative(qint32 panDelta, qint32 tiltDelta);
    bool panTiltStop();
    bool setZoom(qint32 value);          // 100 (1x) .. 400 (4x)
    qint32 getZoom();
    bool gimbalReset();

    /* ---- AI Tracking ---- */
    bool setAiTracking(bool enable);
    bool getAiTracking();
    bool setTrackingFrame(TrackingFrame frame);
    TrackingFrame getTrackingFrame();
    bool setTrackingTarget(TrackingTarget target);
    TrackingTarget getTrackingTarget();

    /* ---- Special modes ---- */
    bool setCameraMode(CameraMode mode);
    bool setDeskView(bool enable);
    bool setWhiteboard(bool enable);
    bool setOverhead(bool enable);

    /* ---- Image controls ---- */
    bool setBrightness(qint32 v);   qint32 getBrightness();
    bool setContrast(qint32 v);     qint32 getContrast();
    bool setSaturation(qint32 v);   qint32 getSaturation();
    bool setSharpness(qint32 v);    qint32 getSharpness();
    bool setGain(qint32 v);         qint32 getGain();
    bool setBacklightCompensation(bool enable);
    bool getBacklightCompensation();

    bool setAutoWhiteBalance(bool enable);
    bool getAutoWhiteBalance();
    bool setWhiteBalanceTemp(qint32 v);
    qint32 getWhiteBalanceTemp();

    bool setExposureAuto(bool enable);
    bool getExposureAuto();
    bool setExposureAbsolute(qint32 v);
    qint32 getExposureAbsolute();

    bool setAutoFocus(bool enable);
    bool getAutoFocus();
    bool setFocusAbsolute(qint32 v);
    qint32 getFocusAbsolute();

    /* ---- Presets (software) ---- */
    bool savePreset(quint8 index);
    bool recallPreset(quint8 index);
    PresetPosition getPreset(quint8 index) const;
    void setPreset(quint8 index, const PresetPosition &preset);

    /* ---- Utility ---- */
    QStringList enumerateControls();
    bool rawXuSet(quint8 selector, const QByteArray &data);
    bool rawXuGet(quint8 selector, QByteArray &data, quint16 len);
    void dumpAllXU();

    /* ---- Properties ---- */
    bool connected() const              { return m_connected; }
    CameraModel cameraModel() const     { return m_cameraModel; }
    QString devicePath() const          { return m_devicePath; }
    QString deviceName() const          { return m_deviceName; }
    QString driverName() const          { return m_driverName; }
    QString busInfo() const             { return m_busInfo; }
    int fd() const                      { return m_fd; }
    quint8 xuUnitId() const             { return m_xuUnitId; }
    void setXuUnitId(quint8 value);
    CameraMode currentMode() const      { return m_currentMode; }
    bool aiTrackingEnabled() const      { return m_aiTrackingEnabled; }
    TrackingFrame trackingFrame() const { return m_trackingFrame; }
    TrackingTarget trackingTarget() const { return m_trackingTarget; }

    CtrlRange panRange() const        { return m_panRange; }
    CtrlRange tiltRange() const       { return m_tiltRange; }
    CtrlRange zoomRange() const       { return m_zoomRange; }
    CtrlRange focusRange() const      { return m_focusRange; }
    CtrlRange brightnessRange() const { return m_brightnessRange; }
    CtrlRange contrastRange() const   { return m_contrastRange; }
    CtrlRange saturationRange() const { return m_saturationRange; }
    CtrlRange sharpnessRange() const  { return m_sharpnessRange; }
    CtrlRange gainRange() const       { return m_gainRange; }
    CtrlRange wbTempRange() const     { return m_wbTempRange; }
    CtrlRange exposureRange() const   { return m_exposureRange; }

signals:
    void logMessage(const QString &msg);

private:
    void log(const QString &msg);
    QString errInfo() const;                    // "errno=N: message" snapshot
    CtrlRange queryControlRange(quint32 ctrlId);
    void cacheControlRanges();
    quint8 detectXuUnitId();
    void scanXuSelectors();
    bool xuSetPadded(quint8 selector, const QByteArray &data);
    bool xuGetPadded(quint8 selector, QByteArray &data);   // data sized on entry
    bool xuSetMode(quint8 modeId, quint8 modeFlag);

    int         m_fd = -1;
    QString     m_devicePath;
    bool        m_connected = false;
    CameraModel m_cameraModel = CameraModel::Unknown;
    QString     m_deviceName;
    QString     m_driverName;
    QString     m_busInfo;
    quint8      m_xuUnitId = 9;           // default XU unit id for Insta360 Link
    bool        m_xuUnitIdOverride = false;
    quint16     m_xuLens[21] = {};        // cached GET_LEN for selectors 1..20
    PresetPosition m_presets[6];

    CtrlRange m_panRange, m_tiltRange, m_zoomRange, m_focusRange;
    CtrlRange m_brightnessRange, m_contrastRange, m_saturationRange;
    CtrlRange m_sharpnessRange, m_gainRange, m_wbTempRange, m_exposureRange;

    CameraMode     m_currentMode = CameraMode::Normal;
    bool           m_aiTrackingEnabled = false;
    TrackingFrame  m_trackingFrame = TrackingFrame::HalfBody;
    TrackingTarget m_trackingTarget = TrackingTarget::Single;
    qint32         m_panPos = 0;          // software-tracked pan position
    qint32         m_tiltPos = 0;         // software-tracked tilt position
};
