/*
  videocapture.h - V4L2 MMAP streaming capture for the live preview.
  ==================================================================
  C++/Qt port of uvideocap.pas.

  Streams from an already-open V4L2 file descriptor (the same fd the
  Insta360Link controller owns) using memory-mapped buffers and decodes
  each frame into a QImage for display.

  Two pixel formats are supported:
    - MJPEG (preferred) - decoded via QImage::loadFromData
    - YUYV  (fallback)  - converted to RGB manually

  Frames are pulled on demand with grabFrame(); the fd is expected to be
  opened O_NONBLOCK so grabFrame() returns false (rather than blocking)
  when no frame is ready yet.
*/
#pragma once

#include <QImage>
#include <QtGlobal>
#include <QVector>

class VideoCapture
{
public:
    VideoCapture() = default;
    ~VideoCapture();

    /* Begin streaming on the given fd. prefW/prefH are a requested size;
       the driver negotiates the nearest supported resolution. */
    bool start(int fd, int prefW, int prefH);
    void stop();

    /* Dequeue one frame and draw it into img. Returns false if no frame is
       ready (EAGAIN) or not streaming. */
    bool grabFrame(QImage &img);

    bool streaming() const { return m_streaming; }
    int width() const      { return m_width; }
    int height() const     { return m_height; }
    quint32 pixFmt() const { return m_pixFmt; }

private:
    bool trySetFormat(quint32 pixFmt, int w, int h);
    bool setFormat(int w, int h);
    bool initBuffers(int count);
    void freeBuffers();
    void decodeMJPEG(const uchar *data, quint32 len, QImage &img);
    void decodeYUYV(const uchar *data, QImage &img);

    int m_fd = -1;                     // borrowed fd (not owned)
    QVector<void *> m_bufStart;        // mmap'd buffer addresses
    QVector<size_t> m_bufLen;          // mmap'd buffer lengths
    int m_width = 0;
    int m_height = 0;
    quint32 m_bytesPerLine = 0;
    quint32 m_pixFmt = 0;
    bool m_streaming = false;
};

/* FourCC -> readable string, e.g. for status display. */
QString fourccToStr(quint32 fourcc);
