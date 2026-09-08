/*
  v4l2.h - Video4Linux2 + UVC Extension Unit (XU) API
  ==================================================================
  C++/Qt port of the Free Pascal unit uv4l2.pas.

  Provides:
    - Standard V4L2 control ioctls (QUERYCAP, QUERYCTRL, G_CTRL, S_CTRL)
    - Combined pan/tilt set via VIDIOC_S_EXT_CTRLS (required by Link 2)
    - UVC Extension Unit (XU) ioctls for vendor-specific controls
    - Device enumeration and CID name lookup

  Unlike the Pascal original, this port uses the kernel UAPI headers
  (<linux/videodev2.h>, <linux/uvcvideo.h>, <linux/usb/video.h>) directly
  rather than re-declaring the structs and the _IOC() request numbers by
  hand - the layouts are then correct by construction.

  Designed for the Insta360 Link (USB VID 0x2E1A PID 0x4C01) and Link 2
  (PID 0x4C04) but works with any V4L2/UVC webcam on Linux.
*/
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

#include <linux/usb/video.h>   // UVC_SET_CUR / UVC_GET_CUR / UVC_GET_LEN / ...
#include <linux/uvcvideo.h>    // struct uvc_xu_control_query, UVCIOC_CTRL_QUERY
#include <linux/videodev2.h>   // V4L2_CID_*, VIDIOC_*, struct v4l2_*

namespace v4l2 {

/* One enumerated capture device. */
struct DeviceInfo {
    QString path;    // e.g. "/dev/video0"
    QString card;    // human-readable card name
    QString driver;  // kernel driver name

    // Matches the Pascal combo-item format: "<path>  [<card> - <driver>]"
    QString display() const {
        return QStringLiteral("%1  [%2 - %3]").arg(path, card, driver);
    }
};

/* ---- Device open / close ---- */
int  openDevice(const QString &devPath);          // O_RDWR | O_NONBLOCK, -1 on failure
void closeDevice(int fd);

/* ---- Standard V4L2 controls ---- */
bool queryCap(int fd, v4l2_capability &cap);
bool queryCtrl(int fd, v4l2_queryctrl &qc);
bool getCtrl(int fd, quint32 ctrlId, qint32 &value);
bool setCtrl(int fd, quint32 ctrlId, qint32 value);

/* Set pan and tilt atomically via VIDIOC_S_EXT_CTRLS.
   Some cameras (e.g. Link 2) reject individual S_CTRL for pan/tilt. */
bool setPanTilt(int fd, qint32 pan, qint32 tilt);

/* ---- UVC Extension Unit raw access ---- */
bool xuQuery(int fd, quint8 unitId, quint8 selector, quint8 queryType,
             void *data, quint16 dataSize);
bool xuSetCur(int fd, quint8 unitId, quint8 selector, const void *data, quint16 dataSize);
bool xuGetCur(int fd, quint8 unitId, quint8 selector, void *data, quint16 dataSize);

/* ---- Utility ---- */
QVector<DeviceInfo> enumDevices();          // scans /dev/video*
QString cidName(quint32 cid);               // human-readable name for a CID

} // namespace v4l2
