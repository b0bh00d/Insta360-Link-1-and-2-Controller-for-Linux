/*
  videocapture.cpp - implementation of V4L2 MMAP streaming capture.
  C++/Qt port of uvideocap.pas.
*/
#include "videocapture.h"

#include <cerrno>
#include <cstring>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/videodev2.h>

namespace {
int xioctl(int fd, unsigned long request, void *arg)
{
    int r;
    do {
        r = ::ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}
}

QString fourccToStr(quint32 fourcc)
{
    char c[4] = {
        char(fourcc & 0xFF),
        char((fourcc >> 8) & 0xFF),
        char((fourcc >> 16) & 0xFF),
        char((fourcc >> 24) & 0xFF),
    };
    return QString::fromLatin1(c, 4);
}

VideoCapture::~VideoCapture()
{
    stop();
}

bool VideoCapture::trySetFormat(quint32 pixFmt, int w, int h)
{
    v4l2_format fmt;
    std::memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = w;
    fmt.fmt.pix.height = h;
    fmt.fmt.pix.pixelformat = pixFmt;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (xioctl(m_fd, VIDIOC_S_FMT, &fmt) != 0)
        return false;
    if (fmt.fmt.pix.pixelformat != pixFmt)
        return false;

    m_width = fmt.fmt.pix.width;
    m_height = fmt.fmt.pix.height;
    m_bytesPerLine = fmt.fmt.pix.bytesperline;
    m_pixFmt = fmt.fmt.pix.pixelformat;
    return true;
}

bool VideoCapture::setFormat(int w, int h)
{
    // Prefer MJPEG (compressed, better fps / USB bandwidth), fall back to YUYV.
    return trySetFormat(V4L2_PIX_FMT_MJPEG, w, h)
        || trySetFormat(V4L2_PIX_FMT_YUYV, w, h);
}

bool VideoCapture::initBuffers(int count)
{
    v4l2_requestbuffers req;
    std::memset(&req, 0, sizeof(req));
    req.count = count;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(m_fd, VIDIOC_REQBUFS, &req) != 0)
        return false;
    if (req.count < 1)
        return false;

    m_bufStart = QVector<void *>(req.count, nullptr);
    m_bufLen = QVector<size_t>(req.count, 0);

    for (quint32 i = 0; i < req.count; ++i) {
        v4l2_buffer buf;
        std::memset(&buf, 0, sizeof(buf));
        buf.index = i;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (xioctl(m_fd, VIDIOC_QUERYBUF, &buf) != 0)
            return false;

        m_bufLen[i] = buf.length;
        m_bufStart[i] = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE,
                             MAP_SHARED, m_fd, buf.m.offset);
        if (m_bufStart[i] == MAP_FAILED) {
            m_bufStart[i] = nullptr;
            return false;
        }

        if (xioctl(m_fd, VIDIOC_QBUF, &buf) != 0)
            return false;
    }
    return true;
}

void VideoCapture::freeBuffers()
{
    for (int i = 0; i < m_bufStart.size(); ++i) {
        if (m_bufStart[i]) {
            munmap(m_bufStart[i], m_bufLen[i]);
            m_bufStart[i] = nullptr;
        }
    }
    m_bufStart.clear();
    m_bufLen.clear();
}

bool VideoCapture::start(int fd, int prefW, int prefH)
{
    if (m_streaming)
        return true;
    if (fd < 0)
        return false;
    m_fd = fd;

    if (!setFormat(prefW, prefH))
        return false;
    if (!initBuffers(4)) {
        freeBuffers();
        return false;
    }

    int bt = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(m_fd, VIDIOC_STREAMON, &bt) != 0) {
        freeBuffers();
        return false;
    }

    m_streaming = true;
    return true;
}

void VideoCapture::stop()
{
    if (m_streaming && m_fd >= 0) {
        int bt = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(m_fd, VIDIOC_STREAMOFF, &bt);
    }
    m_streaming = false;

    // Unmap our buffers, then ask the driver to release its buffer set
    // (REQBUFS count=0). Without this the device stays busy and a later
    // S_FMT / restart fails with EBUSY.
    freeBuffers();
    if (m_fd >= 0) {
        v4l2_requestbuffers req;
        std::memset(&req, 0, sizeof(req));
        req.count = 0;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        xioctl(m_fd, VIDIOC_REQBUFS, &req);
    }

    m_fd = -1;
}

void VideoCapture::decodeMJPEG(const uchar *data, quint32 len, QImage &img)
{
    img.loadFromData(data, int(len), "JPG");
}

void VideoCapture::decodeYUYV(const uchar *data, QImage &img)
{
    if (img.width() != m_width || img.height() != m_height
        || img.format() != QImage::Format_RGB888) {
        img = QImage(m_width, m_height, QImage::Format_RGB888);
    }

    auto clamp = [](int v) -> uchar {
        if (v < 0) return 0;
        if (v > 255) return 255;
        return uchar(v);
    };

    for (int y = 0; y < m_height; ++y) {
        const uchar *src = data + size_t(y) * m_bytesPerLine;
        uchar *dst = img.scanLine(y);
        int x = 0;
        while (x < m_width) {
            int Y0 = src[x * 2 + 0];
            int U  = src[x * 2 + 1];
            int Y1 = src[x * 2 + 2];
            int V  = src[x * 2 + 3];

            int d = U - 128;
            int e = V - 128;

            auto put = [&](int px, int Yv) {
                int cr = Yv + ((91881 * e) >> 16);
                int cg = Yv - ((22554 * d + 46802 * e) >> 16);
                int cb = Yv + ((116130 * d) >> 16);
                dst[px * 3 + 0] = clamp(cr);
                dst[px * 3 + 1] = clamp(cg);
                dst[px * 3 + 2] = clamp(cb);
            };

            put(x, Y0);
            if (x + 1 < m_width)
                put(x + 1, Y1);
            x += 2;
        }
    }
}

bool VideoCapture::grabFrame(QImage &img)
{
    if (!m_streaming)
        return false;

    v4l2_buffer buf;
    std::memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    // Non-blocking dequeue; EAGAIN (no frame ready) just returns false.
    if (xioctl(m_fd, VIDIOC_DQBUF, &buf) != 0)
        return false;

    bool ok = false;
    if (buf.index < quint32(m_bufStart.size())
        && buf.bytesused <= m_bufLen[buf.index]) {
        const uchar *p = static_cast<const uchar *>(m_bufStart[buf.index]);
        switch (m_pixFmt) {
        case V4L2_PIX_FMT_MJPEG:
            decodeMJPEG(p, buf.bytesused, img);
            ok = !img.isNull();
            break;
        case V4L2_PIX_FMT_YUYV:
            decodeYUYV(p, img);
            ok = true;
            break;
        default:
            break;
        }
    }

    // Every successfully dequeued buffer must be returned, even after a
    // decode error, or the stream eventually runs out of buffers and stalls.
    xioctl(m_fd, VIDIOC_QBUF, &buf);
    return ok;
}
