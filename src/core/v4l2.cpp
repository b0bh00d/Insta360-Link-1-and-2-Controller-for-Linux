/*
  v4l2.cpp - implementation of the V4L2 + UVC XU helper API.
  C++/Qt port of uv4l2.pas.
*/
#include "v4l2.h"

#include <QByteArray>
#include <QDir>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace v4l2 {

/* ioctl wrapper that retries on EINTR (the Pascal original did a single
   FpIOCtl call; retrying is harmless and slightly more robust). */
static int xioctl(int fd, unsigned long request, void *arg)
{
    int r;
    do {
        r = ::ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

int openDevice(const QString &devPath)
{
    return ::open(devPath.toLocal8Bit().constData(), O_RDWR | O_NONBLOCK, 0);
}

void closeDevice(int fd)
{
    if (fd >= 0)
        ::close(fd);
}

bool queryCap(int fd, v4l2_capability &cap)
{
    std::memset(&cap, 0, sizeof(cap));
    return xioctl(fd, VIDIOC_QUERYCAP, &cap) == 0;
}

bool queryCtrl(int fd, v4l2_queryctrl &qc)
{
    return xioctl(fd, VIDIOC_QUERYCTRL, &qc) == 0;
}

bool getCtrl(int fd, quint32 ctrlId, qint32 &value)
{
    v4l2_control c;
    std::memset(&c, 0, sizeof(c));
    c.id = ctrlId;
    if (xioctl(fd, VIDIOC_G_CTRL, &c) != 0)
        return false;
    value = c.value;
    return true;
}

bool setCtrl(int fd, quint32 ctrlId, qint32 value)
{
    v4l2_control c;
    std::memset(&c, 0, sizeof(c));
    c.id = ctrlId;
    c.value = value;
    return xioctl(fd, VIDIOC_S_CTRL, &c) == 0;
}

bool setPanTilt(int fd, qint32 pan, qint32 tilt)
{
    v4l2_ext_control ext[2];
    std::memset(ext, 0, sizeof(ext));
    ext[0].id = V4L2_CID_PAN_ABSOLUTE;
    ext[0].size = 0;
    ext[0].value = pan;
    ext[1].id = V4L2_CID_TILT_ABSOLUTE;
    ext[1].size = 0;
    ext[1].value = tilt;

    v4l2_ext_controls ctrls;
    std::memset(&ctrls, 0, sizeof(ctrls));
#ifdef V4L2_CTRL_WHICH_CUR_VAL
    ctrls.which = V4L2_CTRL_WHICH_CUR_VAL;
#else
    ctrls.ctrl_class = 0;
#endif
    ctrls.count = 2;
    ctrls.controls = ext;

    return xioctl(fd, VIDIOC_S_EXT_CTRLS, &ctrls) == 0;
}

bool xuQuery(int fd, quint8 unitId, quint8 selector, quint8 queryType,
             void *data, quint16 dataSize)
{
    struct uvc_xu_control_query q;
    std::memset(&q, 0, sizeof(q));
    q.unit = unitId;
    q.selector = selector;
    q.query = queryType;
    q.size = dataSize;
    q.data = static_cast<__u8 *>(data);
    return xioctl(fd, UVCIOC_CTRL_QUERY, &q) == 0;
}

bool xuSetCur(int fd, quint8 unitId, quint8 selector, const void *data, quint16 dataSize)
{
    // The kernel does not write through the pointer for SET_CUR, so the
    // const_cast is safe.
    return xuQuery(fd, unitId, selector, UVC_SET_CUR,
                   const_cast<void *>(data), dataSize);
}

bool xuGetCur(int fd, quint8 unitId, quint8 selector, void *data, quint16 dataSize)
{
    return xuQuery(fd, unitId, selector, UVC_GET_CUR, data, dataSize);
}

QVector<DeviceInfo> enumDevices()
{
    QVector<DeviceInfo> result;

    QDir devDir(QStringLiteral("/dev"));
    const QStringList nodes = devDir.entryList({QStringLiteral("video*")},
                                               QDir::System, QDir::Name);
    for (const QString &name : nodes) {
        const QString path = QStringLiteral("/dev/") + name;
        int fd = openDevice(path);
        if (fd < 0)
            continue;

        v4l2_capability cap;
        if (queryCap(fd, cap)) {
            DeviceInfo info;
            info.path = path;
            info.card = QString::fromLatin1(reinterpret_cast<const char *>(cap.card));
            info.driver = QString::fromLatin1(reinterpret_cast<const char *>(cap.driver));
            result.append(info);
        }
        closeDevice(fd);
    }
    return result;
}

QString cidName(quint32 cid)
{
    switch (cid) {
    case V4L2_CID_BRIGHTNESS:                return QStringLiteral("Brightness");
    case V4L2_CID_CONTRAST:                  return QStringLiteral("Contrast");
    case V4L2_CID_SATURATION:                return QStringLiteral("Saturation");
    case V4L2_CID_HUE:                       return QStringLiteral("Hue");
    case V4L2_CID_AUTO_WHITE_BALANCE:        return QStringLiteral("Auto White Balance");
    case V4L2_CID_GAMMA:                     return QStringLiteral("Gamma");
    case V4L2_CID_GAIN:                      return QStringLiteral("Gain");
    case V4L2_CID_POWER_LINE_FREQUENCY:     return QStringLiteral("Power Line Frequency");
    case V4L2_CID_WHITE_BALANCE_TEMPERATURE: return QStringLiteral("White Balance Temp");
    case V4L2_CID_SHARPNESS:                 return QStringLiteral("Sharpness");
    case V4L2_CID_BACKLIGHT_COMPENSATION:    return QStringLiteral("Backlight Compensation");
    case V4L2_CID_EXPOSURE_AUTO:             return QStringLiteral("Exposure Mode");
    case V4L2_CID_EXPOSURE_ABSOLUTE:         return QStringLiteral("Exposure (Absolute)");
    case V4L2_CID_EXPOSURE_AUTO_PRIORITY:    return QStringLiteral("Exposure Auto Priority");
    case V4L2_CID_PAN_ABSOLUTE:              return QStringLiteral("Pan (Absolute)");
    case V4L2_CID_TILT_ABSOLUTE:             return QStringLiteral("Tilt (Absolute)");
    case V4L2_CID_PAN_RELATIVE:              return QStringLiteral("Pan (Relative)");
    case V4L2_CID_TILT_RELATIVE:             return QStringLiteral("Tilt (Relative)");
    case V4L2_CID_PAN_SPEED:                 return QStringLiteral("Pan Speed");
    case V4L2_CID_TILT_SPEED:                return QStringLiteral("Tilt Speed");
    case V4L2_CID_PAN_RESET:                 return QStringLiteral("Pan Reset");
    case V4L2_CID_TILT_RESET:                return QStringLiteral("Tilt Reset");
    case V4L2_CID_FOCUS_ABSOLUTE:            return QStringLiteral("Focus (Absolute)");
    case V4L2_CID_FOCUS_AUTO:                return QStringLiteral("Auto Focus");
    case V4L2_CID_ZOOM_ABSOLUTE:            return QStringLiteral("Zoom (Absolute)");
    case V4L2_CID_ZOOM_RELATIVE:            return QStringLiteral("Zoom (Relative)");
    case V4L2_CID_ZOOM_CONTINUOUS:          return QStringLiteral("Zoom (Continuous)");
    default:
        return QStringLiteral("Control $%1").arg(cid, 8, 16, QLatin1Char('0')).toUpper();
    }
}

} // namespace v4l2
