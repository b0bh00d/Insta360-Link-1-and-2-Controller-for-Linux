/*
  main.cpp - command-line controller for the Insta360 Link webcam.
  C++/Qt port of linkctl.pas.

  Usage:
    linkctl [options] <command> [args]

  Options:
    -d /dev/videoN   Specify device (default: auto-detect)
    -u N             Specify XU unit ID (default: auto-detect)
    -v               Verbose output
*/
#include <QCoreApplication>
#include <QStringList>
#include <QTextStream>

#include "insta360link.h"
#include "v4l2.h"

static QTextStream out(stdout);
static QTextStream errOut(stderr);

static bool g_verbose = false;
static QString g_devicePath;
static int g_xUnitId = -1;

/* ---- Help ---- */

static void showHelp()
{
    out << "Insta360 Link Webcam Controller for Linux\n"
        << "==========================================\n\n"
        << "Usage: linkctl [options] <command> [args]\n\n"
        << "Options:\n"
        << "  -d /dev/videoN   Specify device (default: auto-detect)\n"
        << "  -u N             Specify XU unit ID (default: auto-detect)\n"
        << "  -v               Verbose output\n\n"
        << "PTZ Commands:\n"
        << "  pan <value>         Set absolute pan (-36000..36000)\n"
        << "  tilt <value>        Set absolute tilt\n"
        << "  move <px> <ty>      Relative pan/tilt (-30..30 each)\n"
        << "  zoom <value>        Set zoom (100=1x to 400=4x)\n"
        << "  home               Reset gimbal to center\n\n"
        << "AI & Mode Commands:\n"
        << "  tracking on|off     AI tracking\n"
        << "  frame head|half|full  Tracking framing mode\n"
        << "  deskview on|off     DeskView (split desk + face)\n"
        << "  whiteboard on|off   Whiteboard mode\n"
        << "  overhead on|off     Overhead document view\n"
        << "  normal             Back to normal mode\n\n"
        << "Image Commands:\n"
        << "  brightness <val>   Set brightness\n"
        << "  contrast <val>     Set contrast\n"
        << "  saturation <val>   Set saturation\n"
        << "  sharpness <val>    Set sharpness\n"
        << "  gain <val>         Set gain\n"
        << "  wb auto|<temp>     White balance\n"
        << "  exposure auto|<val> Exposure\n"
        << "  focus auto|<val>   Focus\n"
        << "  backlight on|off   Backlight compensation\n\n"
        << "Other Commands:\n"
        << "  list                List video devices\n"
        << "  info                Show camera info + all controls\n"
        << "  preset save|recall <0-5>\n"
        << "  xu <selector> <hex_bytes...>   Raw XU command\n";
    out.flush();
}

/* ---- Helpers ---- */

static bool parseOnOff(const QString &s)
{
    const QString v = s.toLower();
    return v == "on" || s == "1" || v == "true" || v == "yes" || v == "enable";
}

static bool isOnOff(const QString &s)
{
    const QString v = s.toLower();
    return v == "on" || v == "off" || v == "1" || v == "0" || v == "true"
        || v == "false" || v == "yes" || v == "no" || v == "enable" || v == "disable";
}

[[noreturn]] static void usageError(const QString &msg)
{
    errOut << "ERROR: " << msg << '\n'
           << "Run \"linkctl help\" for usage.\n";
    errOut.flush();
    ::exit(2);
}

static void doList()
{
    const auto devices = v4l2::enumDevices();
    if (devices.isEmpty()) {
        out << "No V4L2 video devices found.\n";
    } else {
        for (const auto &d : devices)
            out << d.display() << '\n';
    }
    out.flush();
}

static QString autoDetectDevice()
{
    const auto devices = v4l2::enumDevices();
    for (const auto &d : devices) {
        if (d.display().contains(QLatin1String("insta360"), Qt::CaseInsensitive))
            return d.path;
    }
    if (!devices.isEmpty())
        return devices.first().path;
    return QStringLiteral("/dev/video0");
}

static bool connectCamera(Insta360Link &cam)
{
    if (g_devicePath.isEmpty())
        g_devicePath = autoDetectDevice();

    if (g_xUnitId >= 0)
        cam.setXuUnitId(quint8(g_xUnitId));

    if (!cam.open(g_devicePath)) {
        out << "ERROR: Cannot open " << g_devicePath << '\n'
            << "Try running with sudo or check device permissions.\n";
        out.flush();
        return false;
    }
    if (!g_verbose)
        out << "Connected: " << cam.deviceName() << " (" << g_devicePath << ")\n";
    out.flush();
    return true;
}

static void doInfo(Insta360Link &cam)
{
    out << "Device: " << cam.deviceName() << '\n'
        << "Driver: " << cam.driverName() << '\n'
        << "Bus:    " << cam.busInfo() << '\n'
        << "XU ID:  " << cam.xuUnitId() << "\n\n"
        << "=== Available Controls ===\n";
    for (const QString &line : cam.enumerateControls())
        out << line << '\n';

    out << "\n=== Control Ranges ===\n";
    auto r = cam.panRange();
    if (r.available)
        out << QStringLiteral("Pan:        %1..%2  current=%3\n")
                   .arg(r.min).arg(r.max).arg(cam.getPanAbsolute());
    r = cam.tiltRange();
    if (r.available)
        out << QStringLiteral("Tilt:       %1..%2  current=%3\n")
                   .arg(r.min).arg(r.max).arg(cam.getTiltAbsolute());
    r = cam.zoomRange();
    if (r.available)
        out << QStringLiteral("Zoom:       %1..%2  current=%3\n")
                   .arg(r.min).arg(r.max).arg(cam.getZoom());
    r = cam.focusRange();
    if (r.available)
        out << QStringLiteral("Focus:      %1..%2  current=%3\n")
                   .arg(r.min).arg(r.max).arg(cam.getFocusAbsolute());
    r = cam.brightnessRange();
    if (r.available)
        out << QStringLiteral("Brightness: %1..%2  current=%3\n")
                   .arg(r.min).arg(r.max).arg(cam.getBrightness());
    out.flush();
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();

    if (args.size() <= 1) {
        showHelp();
        return 0;
    }

    // ---- Parse options ----
    int i = 1;
    while (i < args.size() && args[i].startsWith(QLatin1Char('-'))) {
        const QString opt = args[i];
        if (opt == "-d") {
            if (i + 1 >= args.size())
                usageError(QStringLiteral("-d requires a device path"));
            g_devicePath = args[++i];
        } else if (opt == "-u") {
            if (i + 1 >= args.size())
                usageError(QStringLiteral("-u requires a unit ID"));
            bool ok = false;
            g_xUnitId = args[++i].toInt(&ok);
            if (!ok || g_xUnitId < 0 || g_xUnitId > 255)
                usageError(QStringLiteral("-u expects a unit ID from 0 to 255"));
        } else if (opt == "-v") {
            g_verbose = true;
        } else if (opt == "-h" || opt == "--help") {
            showHelp();
            return 0;
        } else {
            usageError(QStringLiteral("unknown option: ") + opt);
        }
        ++i;
    }

    if (i >= args.size()) {
        showHelp();
        return 0;
    }

    const QString cmd = args[i].toLower();
    ++i;
    const int remaining = args.size() - i;      // args after the command
    auto arg = [&](int n) -> QString {
        return (i + n < args.size()) ? args[i + n] : QString();
    };

    // ---- Validate arguments before opening the camera ----
    static const QStringList needArg = {
        "pan", "tilt", "zoom", "tracking", "frame", "deskview", "whiteboard",
        "overhead", "brightness", "contrast", "saturation", "sharpness", "gain",
        "backlight", "wb", "exposure", "focus"};
    static const QStringList intArg = {
        "pan", "tilt", "zoom", "brightness", "contrast", "saturation",
        "sharpness", "gain"};
    static const QStringList onOffArg = {
        "tracking", "deskview", "whiteboard", "overhead", "backlight"};

    if (cmd == "move" && remaining < 2)
        usageError(QStringLiteral("move requires <pan> <tilt>"));
    else if (cmd == "preset" && remaining < 2)
        usageError(QStringLiteral("preset requires save|recall <0-5>"));
    else if (cmd == "xu" && remaining < 2)
        usageError(QStringLiteral("xu requires <selector> <hex_byte> [hex_byte...]"));
    else if (needArg.contains(cmd) && remaining < 1)
        usageError(cmd + QStringLiteral(" requires an argument"));

    if (onOffArg.contains(cmd) && !isOnOff(arg(0)))
        usageError(cmd + QStringLiteral(" expects on or off"));

    if (intArg.contains(cmd)) {
        bool ok = false;
        arg(0).toInt(&ok);
        if (!ok)
            usageError(cmd + QStringLiteral(" expects an integer"));
    }

    if (cmd == "move") {
        bool ok1 = false, ok2 = false;
        arg(0).toInt(&ok1);
        arg(1).toInt(&ok2);
        if (!ok1 || !ok2)
            usageError(QStringLiteral("move expects two integers"));
    }

    if (cmd == "preset") {
        const QString sub = arg(0).toLower();
        if (sub != "save" && sub != "recall")
            usageError(QStringLiteral("preset expects save or recall"));
        bool ok = false;
        const int idx = arg(1).toInt(&ok);
        if (!ok || idx < 0 || idx > 5)
            usageError(QStringLiteral("preset index must be from 0 to 5"));
    }

    // ---- Commands that don't need a connection ----
    if (cmd == "list") {
        doList();
        return 0;
    }
    if (cmd == "help") {
        showHelp();
        return 0;
    }

    // ---- Connect ----
    Insta360Link cam;
    QObject::connect(&cam, &Insta360Link::logMessage, [](const QString &msg) {
        if (g_verbose)
            out << "[LOG] " << msg << '\n';
    });

    if (!connectCamera(cam))
        return 1;

    bool ok = false;

    if (cmd == "pan") {
        ok = cam.setPanAbsolute(arg(0).toInt());
    } else if (cmd == "tilt") {
        ok = cam.setTiltAbsolute(arg(0).toInt());
    } else if (cmd == "move") {
        ok = cam.panTiltRelative(arg(0).toInt(), arg(1).toInt());
    } else if (cmd == "zoom") {
        ok = cam.setZoom(arg(0).isEmpty() ? 100 : arg(0).toInt());
    } else if (cmd == "home") {
        ok = cam.gimbalReset();
    } else if (cmd == "tracking") {
        ok = cam.setAiTracking(parseOnOff(arg(0)));
    } else if (cmd == "frame") {
        const QString f = arg(0).toLower();
        if (f == "head")
            ok = cam.setTrackingFrame(Insta360Link::TrackingFrame::Head);
        else if (f == "half")
            ok = cam.setTrackingFrame(Insta360Link::TrackingFrame::HalfBody);
        else if (f == "full")
            ok = cam.setTrackingFrame(Insta360Link::TrackingFrame::FullBody);
        else
            out << "Unknown frame mode: " << arg(0) << " (use head|half|full)\n";
    } else if (cmd == "deskview") {
        ok = cam.setDeskView(parseOnOff(arg(0)));
    } else if (cmd == "whiteboard") {
        ok = cam.setWhiteboard(parseOnOff(arg(0)));
    } else if (cmd == "overhead") {
        ok = cam.setOverhead(parseOnOff(arg(0)));
    } else if (cmd == "normal") {
        ok = cam.setCameraMode(Insta360Link::CameraMode::Normal);
    } else if (cmd == "brightness") {
        ok = cam.setBrightness(arg(0).isEmpty() ? 128 : arg(0).toInt());
    } else if (cmd == "contrast") {
        ok = cam.setContrast(arg(0).isEmpty() ? 128 : arg(0).toInt());
    } else if (cmd == "saturation") {
        ok = cam.setSaturation(arg(0).isEmpty() ? 128 : arg(0).toInt());
    } else if (cmd == "sharpness") {
        ok = cam.setSharpness(arg(0).isEmpty() ? 128 : arg(0).toInt());
    } else if (cmd == "gain") {
        ok = cam.setGain(arg(0).isEmpty() ? 0 : arg(0).toInt());
    } else if (cmd == "backlight") {
        ok = cam.setBacklightCompensation(parseOnOff(arg(0)));
    } else if (cmd == "wb") {
        if (arg(0).toLower() == "auto") {
            ok = cam.setAutoWhiteBalance(true);
        } else {
            cam.setAutoWhiteBalance(false);
            ok = cam.setWhiteBalanceTemp(arg(0).isEmpty() ? 4000 : arg(0).toInt());
        }
    } else if (cmd == "exposure") {
        if (arg(0).toLower() == "auto") {
            ok = cam.setExposureAuto(true);
        } else {
            cam.setExposureAuto(false);
            ok = cam.setExposureAbsolute(arg(0).isEmpty() ? 250 : arg(0).toInt());
        }
    } else if (cmd == "focus") {
        if (arg(0).toLower() == "auto") {
            ok = cam.setAutoFocus(true);
        } else {
            cam.setAutoFocus(false);
            ok = cam.setFocusAbsolute(arg(0).isEmpty() ? 0 : arg(0).toInt());
        }
    } else if (cmd == "preset") {
        const QString sub = arg(0).toLower();
        if (sub == "save")
            ok = cam.savePreset(quint8(arg(1).toInt()));
        else if (sub == "recall")
            ok = cam.recallPreset(quint8(arg(1).toInt()));
    } else if (cmd == "xu") {
        const quint8 sel = quint8(arg(0).toUInt());
        QByteArray data;
        for (int k = 1; k < remaining; ++k) {
            bool okByte = false;
            const uint b = arg(k).toUInt(&okByte, 16);
            data.append(char(okByte ? (b & 0xFF) : 0));
        }
        ok = cam.rawXuSet(sel, data);
        out << (ok ? "XU command sent OK\n" : "XU command FAILED\n");
    } else if (cmd == "info") {
        doInfo(cam);
        ok = true;
    } else {
        out << "Unknown command: " << cmd << '\n'
            << "Run \"linkctl help\" for usage.\n";
    }

    if (ok)
        out << "OK\n";
    else if (cmd != "info" && cmd != "list")
        out << "Command may have failed. Try running with -v for details.\n";
    out.flush();

    cam.close();
    return ok ? 0 : 1;
}
