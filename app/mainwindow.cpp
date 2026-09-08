/*
  mainwindow.cpp - implementation of the Qt Widgets GUI.
  C++/Qt port of umainform.pas.
*/
#include "mainwindow.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTime>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

namespace {
QString zoomText(int pos)
{
    return QString::asprintf("%.1fx", pos / 100.0);
}
}

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
{
    connect(&m_cam, &Insta360Link::logMessage, this, &MainWindow::onCamLog);

    m_previewTimer = new QTimer(this);
    m_previewTimer->setInterval(40);           // ~25 fps poll
    connect(m_previewTimer, &QTimer::timeout, this, &MainWindow::onPreviewTimer);

    m_ptzTimer = new QTimer(this);
    m_ptzTimer->setInterval(150);              // continuous motion while held
    connect(m_ptzTimer, &QTimer::timeout, this, &MainWindow::ptzRepeat);

    m_trackPollTimer = new QTimer(this);
    m_trackPollTimer->setInterval(1000);
    connect(m_trackPollTimer, &QTimer::timeout, this, &MainWindow::onTrackingPoll);

    buildUi();
    refreshDeviceList();
    loadSettings();
    updateUiState();
}

MainWindow::~MainWindow() = default;

/* ===== UI construction ===== */

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("Insta360 Link Controller for Linux"));

    auto *root = new QVBoxLayout(this);

    // --- TOP: device bar ---
    auto *topBar = new QHBoxLayout;
    topBar->addWidget(new QLabel(QStringLiteral("Device:")));
    m_cboDevice = new QComboBox;
    m_cboDevice->setMinimumWidth(340);
    topBar->addWidget(m_cboDevice, 1);
    m_btnRefresh = new QPushButton(QStringLiteral("Refresh"));
    m_btnConnect = new QPushButton(QStringLiteral("Connect"));
    m_btnDisconnect = new QPushButton(QStringLiteral("Disconnect"));
    m_btnDisconnect->setEnabled(false);
    topBar->addWidget(m_btnRefresh);
    topBar->addWidget(m_btnConnect);
    topBar->addWidget(m_btnDisconnect);
    m_lblStatus = new QLabel(QStringLiteral("Not connected"));
    m_lblStatus->setStyleSheet(QStringLiteral("color: red;"));
    topBar->addWidget(m_lblStatus);
    topBar->addStretch(1);
    root->addLayout(topBar);

    connect(m_btnRefresh, &QPushButton::clicked, this, &MainWindow::refreshDeviceList);
    connect(m_btnConnect, &QPushButton::clicked, this, &MainWindow::onConnect);
    connect(m_btnDisconnect, &QPushButton::clicked, this, &MainWindow::onDisconnect);

    auto *body = new QHBoxLayout;
    root->addLayout(body, 1);

    // ===== LEFT column: PTZ + Modes =====
    auto *leftCol = new QVBoxLayout;
    body->addLayout(leftCol);

    // --- PTZ group ---
    m_grpPTZ = new QGroupBox(QStringLiteral("PTZ Controls"));
    auto *ptzLayout = new QVBoxLayout(m_grpPTZ);

    auto *dpad = new QGridLayout;
    struct DirBtn { int r, c; const char *glyph; int panDir, tiltDir; };
    // panDir: 1=left, 2=right ; tiltDir: 1=up, 2=down
    static const DirBtn dirs[] = {
        {0, 0, "↖", 1, 1}, {0, 1, "↑", 0, 1}, {0, 2, "↗", 2, 1},
        {1, 0, "←", 1, 0},                          {1, 2, "→", 2, 0},
        {2, 0, "↙", 1, 2}, {2, 1, "↓", 0, 2}, {2, 2, "↘", 2, 2},
    };
    for (const DirBtn &d : dirs) {
        auto *b = new QToolButton;
        b->setText(QString::fromUtf8(d.glyph));
        b->setFixedSize(44, 44);
        b->setAutoRepeat(false);
        dpad->addWidget(b, d.r, d.c);
        const int panDir = d.panDir, tiltDir = d.tiltDir;
        connect(b, &QToolButton::pressed, this, [this, panDir, tiltDir] {
            ptzPressed(panDir, tiltDir);
        });
        connect(b, &QToolButton::released, this, &MainWindow::ptzReleased);
    }
    auto *btnHome = new QToolButton;
    btnHome->setText(QString::fromUtf8("⌂"));   // house glyph
    btnHome->setFixedSize(44, 44);
    dpad->addWidget(btnHome, 1, 1);
    connect(btnHome, &QToolButton::clicked, this, &MainWindow::onHome);

    auto *dpadWrap = new QHBoxLayout;
    dpadWrap->addLayout(dpad);
    dpadWrap->addStretch(1);
    ptzLayout->addLayout(dpadWrap);

    auto *stepGrid = new QGridLayout;
    stepGrid->addWidget(new QLabel(QStringLiteral("Pan step:")), 0, 0);
    m_sePanStep = new QSpinBox;
    m_sePanStep->setRange(1, 30);
    m_sePanStep->setValue(8);
    stepGrid->addWidget(m_sePanStep, 0, 1);
    stepGrid->addWidget(new QLabel(QStringLiteral("Tilt step:")), 1, 0);
    m_seTiltStep = new QSpinBox;
    m_seTiltStep->setRange(1, 30);
    m_seTiltStep->setValue(8);
    stepGrid->addWidget(m_seTiltStep, 1, 1);
    stepGrid->setColumnStretch(2, 1);
    ptzLayout->addLayout(stepGrid);

    auto *zoomRow = new QHBoxLayout;
    zoomRow->addWidget(new QLabel(QStringLiteral("Zoom:")));
    m_tbZoom = new QSlider(Qt::Horizontal);
    m_tbZoom->setRange(100, 400);
    m_tbZoom->setValue(100);
    zoomRow->addWidget(m_tbZoom, 1);
    m_lblZoomVal = new QLabel(zoomText(100));
    zoomRow->addWidget(m_lblZoomVal);
    ptzLayout->addLayout(zoomRow);
    connect(m_tbZoom, &QSlider::valueChanged, this, &MainWindow::onZoomChange);

    leftCol->addWidget(m_grpPTZ);

    // --- Modes group ---
    m_grpModes = new QGroupBox(QStringLiteral("Camera Modes"));
    auto *modesLayout = new QVBoxLayout(m_grpModes);

    auto *trackRow = new QHBoxLayout;
    trackRow->addWidget(new QLabel(QStringLiteral("AI Tracking:")));
    m_btnTrackingOn = new QPushButton(QStringLiteral("ON"));
    m_btnTrackingOff = new QPushButton(QStringLiteral("OFF"));
    trackRow->addWidget(m_btnTrackingOn);
    trackRow->addWidget(m_btnTrackingOff);
    trackRow->addStretch(1);
    modesLayout->addLayout(trackRow);
    connect(m_btnTrackingOn, &QPushButton::clicked, this, &MainWindow::onTrackingOn);
    connect(m_btnTrackingOff, &QPushButton::clicked, this, &MainWindow::onTrackingOff);

    auto *frameRow = new QHBoxLayout;
    m_lblTrackFrame = new QLabel(QStringLiteral("Framing:"));
    frameRow->addWidget(m_lblTrackFrame);
    m_cboTrackFrame = new QComboBox;
    m_cboTrackFrame->addItems({QStringLiteral("Head"), QStringLiteral("Half Body"),
                               QStringLiteral("Full Body")});
    m_cboTrackFrame->setCurrentIndex(1);
    frameRow->addWidget(m_cboTrackFrame, 1);
    modesLayout->addLayout(frameRow);
    connect(m_cboTrackFrame, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onTrackFrameChange);

    modesLayout->addWidget(new QLabel(QStringLiteral("Operating Mode:")));
    auto *modeGrid = new QGridLayout;
    struct ModeBtn { const char *text; Insta360Link::CameraMode mode; };
    static const ModeBtn modeBtns[] = {
        {"Normal", Insta360Link::CameraMode::Normal},
        {"DeskView", Insta360Link::CameraMode::DeskView},
        {"Whiteboard", Insta360Link::CameraMode::Whiteboard},
        {"Overhead", Insta360Link::CameraMode::Overhead},
    };
    int mi = 0;
    for (const ModeBtn &mb : modeBtns) {
        auto *b = new QPushButton(QString::fromUtf8(mb.text));
        modeGrid->addWidget(b, mi / 2, mi % 2);
        const Insta360Link::CameraMode mode = mb.mode;
        connect(b, &QPushButton::clicked, this, [this, mode] {
            if (m_cam.connected())
                m_cam.setCameraMode(mode);
        });
        ++mi;
    }
    modesLayout->addLayout(modeGrid);

    auto *btnGimbalReset = new QPushButton(QStringLiteral("Gimbal Reset"));
    modesLayout->addWidget(btnGimbalReset);
    connect(btnGimbalReset, &QPushButton::clicked, this, &MainWindow::onGimbalReset);

    leftCol->addWidget(m_grpModes);
    leftCol->addStretch(1);

    // ===== CENTER column: Image + Exposure + Focus + Preview =====
    auto *centerCol = new QVBoxLayout;
    body->addLayout(centerCol, 1);

    auto addSliderRow = [](QGridLayout *g, int row, const QString &label,
                           int min, int max, QSlider *&slider, QLabel *&valLbl) {
        g->addWidget(new QLabel(label), row, 0);
        slider = new QSlider(Qt::Horizontal);
        slider->setRange(min, max);
        g->addWidget(slider, row, 1);
        valLbl = new QLabel(QStringLiteral("0"));
        valLbl->setMinimumWidth(44);
        g->addWidget(valLbl, row, 2);
    };

    m_grpImage = new QGroupBox(QStringLiteral("Image Controls"));
    auto *imgGrid = new QGridLayout(m_grpImage);
    int r = 0;
    addSliderRow(imgGrid, r++, QStringLiteral("Brightness:"), 0, 100, m_tbBrightness, m_lblBrightnessV);
    addSliderRow(imgGrid, r++, QStringLiteral("Contrast:"),   0, 100, m_tbContrast,   m_lblContrastV);
    addSliderRow(imgGrid, r++, QStringLiteral("Saturation:"), 0, 100, m_tbSaturation, m_lblSaturationV);
    addSliderRow(imgGrid, r++, QStringLiteral("Sharpness:"),  0, 100, m_tbSharpness,  m_lblSharpnessV);
    addSliderRow(imgGrid, r++, QStringLiteral("Gain:"),       0, 100, m_tbGain,       m_lblGainV);

    m_chkAutoWB = new QCheckBox(QStringLiteral("Auto White Balance"));
    m_chkAutoWB->setChecked(true);
    imgGrid->addWidget(m_chkAutoWB, r, 0, 1, 2);
    m_chkBacklight = new QCheckBox(QStringLiteral("Backlight Comp."));
    imgGrid->addWidget(m_chkBacklight, r, 2);
    ++r;
    addSliderRow(imgGrid, r++, QStringLiteral("WB Temp:"), 2000, 10000, m_tbWBTemp, m_lblWBTempV);
    m_tbWBTemp->setValue(6400);
    m_lblWBTempV->setText(QStringLiteral("6400K"));
    centerCol->addWidget(m_grpImage);

    // Image slider wiring
    auto wireImg = [this](QSlider *s, QLabel *lbl, std::function<void(int)> apply) {
        connect(s, &QSlider::valueChanged, this, [this, lbl, apply](int v) {
            if (m_updating || !m_cam.connected())
                return;
            apply(v);
            lbl->setText(QString::number(v));
        });
    };
    wireImg(m_tbBrightness, m_lblBrightnessV, [this](int v) { m_cam.setBrightness(v); });
    wireImg(m_tbContrast,   m_lblContrastV,   [this](int v) { m_cam.setContrast(v); });
    wireImg(m_tbSaturation, m_lblSaturationV, [this](int v) { m_cam.setSaturation(v); });
    wireImg(m_tbSharpness,  m_lblSharpnessV,  [this](int v) { m_cam.setSharpness(v); });
    wireImg(m_tbGain,       m_lblGainV,       [this](int v) { m_cam.setGain(v); });

    connect(m_chkAutoWB, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating || !m_cam.connected())
            return;
        m_cam.setAutoWhiteBalance(on);
        m_tbWBTemp->setEnabled(!on);
    });
    connect(m_tbWBTemp, &QSlider::valueChanged, this, [this](int v) {
        if (m_updating || !m_cam.connected())
            return;
        m_cam.setWhiteBalanceTemp(v);
        m_lblWBTempV->setText(QString::number(v) + QLatin1Char('K'));
    });
    connect(m_chkBacklight, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating || !m_cam.connected())
            return;
        m_cam.setBacklightCompensation(on);
    });

    // Exposure
    m_grpExposure = new QGroupBox(QStringLiteral("Exposure"));
    auto *expGrid = new QGridLayout(m_grpExposure);
    m_chkAutoExposure = new QCheckBox(QStringLiteral("Auto Exposure"));
    m_chkAutoExposure->setChecked(true);
    expGrid->addWidget(m_chkAutoExposure, 0, 0, 1, 3);
    addSliderRow(expGrid, 1, QStringLiteral("Value:"), 3, 2047, m_tbExposure, m_lblExposureV);
    m_tbExposure->setValue(250);
    m_lblExposureV->setText(QStringLiteral("250"));
    centerCol->addWidget(m_grpExposure);
    connect(m_chkAutoExposure, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating || !m_cam.connected())
            return;
        m_cam.setExposureAuto(on);
        m_tbExposure->setEnabled(!on);
    });
    connect(m_tbExposure, &QSlider::valueChanged, this, [this](int v) {
        if (m_updating || !m_cam.connected())
            return;
        m_cam.setExposureAbsolute(v);
        m_lblExposureV->setText(QString::number(v));
    });

    // Focus
    m_grpFocus = new QGroupBox(QStringLiteral("Focus"));
    auto *focGrid = new QGridLayout(m_grpFocus);
    m_chkAutoFocus = new QCheckBox(QStringLiteral("Auto Focus"));
    m_chkAutoFocus->setChecked(true);
    focGrid->addWidget(m_chkAutoFocus, 0, 0, 1, 3);
    addSliderRow(focGrid, 1, QStringLiteral("Value:"), 0, 100, m_tbFocus, m_lblFocusV);
    centerCol->addWidget(m_grpFocus);
    connect(m_chkAutoFocus, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating || !m_cam.connected())
            return;
        m_cam.setAutoFocus(on);
        m_tbFocus->setEnabled(!on);
    });
    connect(m_tbFocus, &QSlider::valueChanged, this, [this](int v) {
        if (m_updating || !m_cam.connected())
            return;
        m_cam.setFocusAbsolute(v);
        m_lblFocusV->setText(QString::number(v));
    });

    // Live preview
    m_grpPreview = new QGroupBox(QStringLiteral("Live Preview"));
    auto *prevLayout = new QVBoxLayout(m_grpPreview);
    m_imgPreview = new QLabel;
    m_imgPreview->setMinimumSize(480, 270);
    m_imgPreview->setAlignment(Qt::AlignCenter);
    m_imgPreview->setStyleSheet(QStringLiteral("background: #202020;"));
    prevLayout->addWidget(m_imgPreview, 1);
    auto *prevBar = new QHBoxLayout;
    m_btnPreview = new QPushButton(QStringLiteral("Start Preview"));
    prevBar->addWidget(m_btnPreview);
    m_lblPreviewInfo = new QLabel;
    prevBar->addWidget(m_lblPreviewInfo);
    prevBar->addStretch(1);
    prevLayout->addLayout(prevBar);
    centerCol->addWidget(m_grpPreview, 1);
    connect(m_btnPreview, &QPushButton::clicked, this, &MainWindow::onPreviewToggle);

    // ===== RIGHT column: Presets + Log =====
    auto *rightCol = new QVBoxLayout;
    body->addLayout(rightCol);

    m_grpPresets = new QGroupBox(QStringLiteral("Preset Positions"));
    auto *presetGrid = new QGridLayout(m_grpPresets);
    for (int i = 0; i < 6; ++i) {
        m_edtPresetName[i] = new QLineEdit(QStringLiteral("Preset %1").arg(i));
        presetGrid->addWidget(m_edtPresetName[i], i, 0);
        m_btnPresetRecall[i] = new QPushButton(QStringLiteral("Recall"));
        presetGrid->addWidget(m_btnPresetRecall[i], i, 1);
        m_btnPresetSave[i] = new QPushButton(QStringLiteral("Save"));
        presetGrid->addWidget(m_btnPresetSave[i], i, 2);
        const int idx = i;
        connect(m_btnPresetRecall[i], &QPushButton::clicked, this,
                [this, idx] { onPresetRecall(idx); });
        connect(m_btnPresetSave[i], &QPushButton::clicked, this,
                [this, idx] { onPresetSave(idx); });
    }
    rightCol->addWidget(m_grpPresets);

    auto *grpLog = new QGroupBox(QStringLiteral("Activity Log"));
    auto *logLayout = new QVBoxLayout(grpLog);
    m_memoLog = new QPlainTextEdit;
    m_memoLog->setReadOnly(true);
    {
        QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setPointSize(8);
        m_memoLog->setFont(mono);
    }
    m_memoLog->setMinimumWidth(260);
    logLayout->addWidget(m_memoLog, 1);
    auto *logBtnRow = new QHBoxLayout;
    m_btnClearLog = new QPushButton(QStringLiteral("Clear Log"));
    m_btnScanXU = new QPushButton(QStringLiteral("Read XU"));
    logBtnRow->addWidget(m_btnClearLog);
    logBtnRow->addWidget(m_btnScanXU);
    logBtnRow->addStretch(1);
    logLayout->addLayout(logBtnRow);
    rightCol->addWidget(grpLog, 1);
    connect(m_btnClearLog, &QPushButton::clicked, m_memoLog, &QPlainTextEdit::clear);
    connect(m_btnScanXU, &QPushButton::clicked, this, &MainWindow::onScanXU);

    resize(1420, 760);
}

/* ===== Device management ===== */

void MainWindow::refreshDeviceList()
{
    m_cboDevice->clear();
    const auto devices = v4l2::enumDevices();
    for (const auto &d : devices)
        m_cboDevice->addItem(d.display(), d.path);
    if (!devices.isEmpty())
        m_cboDevice->setCurrentIndex(0);
}

void MainWindow::onConnect()
{
    if (m_cboDevice->currentIndex() < 0) {
        QMessageBox::information(this, windowTitle(),
                                QStringLiteral("Please select a video device."));
        return;
    }

    const QString devpath = m_cboDevice->currentData().toString();

    if (m_cam.open(devpath)) {
        m_lblStatus->setText(QStringLiteral("Connected: ") + m_cam.deviceName());
        m_lblStatus->setStyleSheet(QStringLiteral("color: green;"));
        m_btnConnect->setEnabled(false);
        m_btnDisconnect->setEnabled(true);
        readCurrentValues();
        updateUiState();
        m_trackPollTimer->start();
        onCamLog(QStringLiteral("Connected to ") + devpath);
        startPreview(); // auto-start; logs and continues if it fails
    } else {
        QMessageBox::warning(this, windowTitle(),
                             QStringLiteral("Failed to open %1\n"
                                            "Try running with sudo or check device permissions.")
                                 .arg(devpath));
    }
}

void MainWindow::onDisconnect()
{
    stopPreview();
    m_trackPollTimer->stop();
    {
        QFont f = m_btnTrackingOn->font();
        f.setBold(false);
        m_btnTrackingOn->setFont(f);
        m_btnTrackingOn->setText(QStringLiteral("ON"));
    }
    m_cam.close();
    m_lblStatus->setText(QStringLiteral("Not connected"));
    m_lblStatus->setStyleSheet(QStringLiteral("color: red;"));
    m_btnConnect->setEnabled(true);
    m_btnDisconnect->setEnabled(false);
    updateUiState();
}

/* ===== PTZ (press-and-hold) ===== */

void MainWindow::ptzPressed(int panDir, int tiltDir)
{
    if (!m_cam.connected())
        return;

    m_ptzPanStep = 0;
    m_ptzTiltStep = 0;
    if (panDir == 1)
        m_ptzPanStep = -m_sePanStep->value();   // Left
    else if (panDir == 2)
        m_ptzPanStep = m_sePanStep->value();    // Right
    if (tiltDir == 1)
        m_ptzTiltStep = m_seTiltStep->value();  // Up
    else if (tiltDir == 2)
        m_ptzTiltStep = -m_seTiltStep->value(); // Down

    m_cam.panTiltRelative(m_ptzPanStep, m_ptzTiltStep);
    m_ptzTimer->start();
}

void MainWindow::ptzReleased()
{
    m_ptzTimer->stop();
    m_ptzPanStep = 0;
    m_ptzTiltStep = 0;
}

void MainWindow::ptzRepeat()
{
    if (!m_cam.connected()) {
        m_ptzTimer->stop();
        return;
    }
    if (m_ptzPanStep != 0 || m_ptzTiltStep != 0)
        m_cam.panTiltRelative(m_ptzPanStep, m_ptzTiltStep);
}

void MainWindow::onHome()
{
    if (m_cam.connected())
        m_cam.gimbalReset();
}

void MainWindow::onZoomChange(int value)
{
    if (m_updating || !m_cam.connected())
        return;
    m_cam.setZoom(value);
    m_lblZoomVal->setText(zoomText(value));
}

/* ===== Live preview ===== */

void MainWindow::startPreview()
{
    if (m_capture.streaming())
        return;
    if (!m_cam.connected())
        return;

    if (m_capture.start(m_cam.fd(), 1280, 720)) {
        m_previewTimer->start();
        m_btnPreview->setText(QStringLiteral("Stop Preview"));
        const QString info = QStringLiteral("%1x%2 %3")
                                 .arg(m_capture.width())
                                 .arg(m_capture.height())
                                 .arg(fourccToStr(m_capture.pixFmt()));
        m_lblPreviewInfo->setText(info);
        onCamLog(QStringLiteral("Preview started: ") + info);
    } else {
        onCamLog(QStringLiteral("Preview FAILED to start (camera may be in use by another app)"));
    }
}

void MainWindow::stopPreview()
{
    m_previewTimer->stop();
    if (m_capture.streaming()) {
        m_capture.stop();
        onCamLog(QStringLiteral("Preview stopped"));
    }
    if (m_btnPreview)
        m_btnPreview->setText(QStringLiteral("Start Preview"));
    if (m_lblPreviewInfo)
        m_lblPreviewInfo->clear();
}

void MainWindow::onPreviewToggle()
{
    if (m_capture.streaming())
        stopPreview();
    else
        startPreview();
}

void MainWindow::onPreviewTimer()
{
    if (!m_capture.streaming())
        return;
    if (m_capture.grabFrame(m_previewImg)) {
        m_imgPreview->setPixmap(QPixmap::fromImage(m_previewImg)
                                    .scaled(m_imgPreview->size(), Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation));
    }
}

/* ===== Modes ===== */

void MainWindow::onTrackingOn()
{
    if (!m_cam.connected())
        return;
    m_cam.setAiTracking(true);
    switch (m_cam.getTrackingFrame()) {
    case Insta360Link::TrackingFrame::Head:     m_cboTrackFrame->setCurrentIndex(0); break;
    case Insta360Link::TrackingFrame::HalfBody: m_cboTrackFrame->setCurrentIndex(1); break;
    case Insta360Link::TrackingFrame::FullBody: m_cboTrackFrame->setCurrentIndex(2); break;
    }
}

void MainWindow::onTrackingOff()
{
    if (m_cam.connected())
        m_cam.setAiTracking(false);
}

void MainWindow::onTrackFrameChange(int index)
{
    if (!m_cam.connected())
        return;
    switch (index) {
    case 0: m_cam.setTrackingFrame(Insta360Link::TrackingFrame::Head); break;
    case 1: m_cam.setTrackingFrame(Insta360Link::TrackingFrame::HalfBody); break;
    case 2: m_cam.setTrackingFrame(Insta360Link::TrackingFrame::FullBody); break;
    }
}

void MainWindow::onTrackingPoll()
{
    if (!m_cam.connected())
        return;

    const bool isTracking = m_cam.getAiTracking();
    QFont on = m_btnTrackingOn->font();
    on.setBold(isTracking);
    m_btnTrackingOn->setFont(on);
    m_btnTrackingOn->setText(isTracking ? QStringLiteral("● ON") : QStringLiteral("ON"));
    QFont off = m_btnTrackingOff->font();
    off.setBold(false);
    m_btnTrackingOff->setFont(off);
    m_btnTrackingOff->setText(QStringLiteral("OFF"));
}

void MainWindow::onGimbalReset()
{
    if (m_cam.connected())
        m_cam.gimbalReset();
}

/* ===== Presets ===== */

void MainWindow::onPresetRecall(int index)
{
    if (!m_cam.connected())
        return;

    const auto p = m_cam.getPreset(quint8(index));
    if (!m_cam.recallPreset(quint8(index)))
        return;

    // Mirror the recalled zoom without firing onZoomChange again.
    const bool was = m_updating;
    m_updating = true;
    m_tbZoom->setValue(p.zoom);
    m_lblZoomVal->setText(zoomText(p.zoom));
    m_updating = was;
}

void MainWindow::onPresetSave(int index)
{
    if (m_cam.connected() && m_cam.savePreset(quint8(index)))
        saveSettings(); // persist immediately
}

/* ===== Log ===== */

void MainWindow::onScanXU()
{
    if (!m_cam.connected())
        return;
    m_memoLog->appendPlainText(QString());
    m_memoLog->appendPlainText(QStringLiteral("=== XU Snapshot %1 ===")
                                   .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss"))));
    m_cam.dumpAllXU();
    m_memoLog->appendPlainText(QStringLiteral("=== End snapshot ==="));
    m_memoLog->appendPlainText(QString());
}

void MainWindow::onCamLog(const QString &msg)
{
    m_memoLog->appendPlainText(
        QTime::currentTime().toString(QStringLiteral("HH:mm:ss")) + QStringLiteral("  ") + msg);
    m_memoLog->verticalScrollBar()->setValue(m_memoLog->verticalScrollBar()->maximum());
}

/* ===== UI helpers ===== */

void MainWindow::setupSlider(QSlider *s, const Insta360Link::CtrlRange &range)
{
    if (range.available) {
        s->setRange(range.min, range.max);
        s->setValue(range.cur);
        s->setEnabled(true);
    } else {
        s->setEnabled(false);
    }
}

void MainWindow::readCurrentValues()
{
    m_updating = true;

    const auto zr = m_cam.zoomRange();
    if (zr.available) {
        m_tbZoom->setRange(zr.min, zr.max);
        m_tbZoom->setValue(m_cam.getZoom());
        m_lblZoomVal->setText(zoomText(m_tbZoom->value()));
    }

    setupSlider(m_tbBrightness, m_cam.brightnessRange());
    m_lblBrightnessV->setText(QString::number(m_tbBrightness->value()));
    setupSlider(m_tbContrast, m_cam.contrastRange());
    m_lblContrastV->setText(QString::number(m_tbContrast->value()));
    setupSlider(m_tbSaturation, m_cam.saturationRange());
    m_lblSaturationV->setText(QString::number(m_tbSaturation->value()));
    setupSlider(m_tbSharpness, m_cam.sharpnessRange());
    m_lblSharpnessV->setText(QString::number(m_tbSharpness->value()));
    setupSlider(m_tbGain, m_cam.gainRange());
    m_lblGainV->setText(QString::number(m_tbGain->value()));

    m_chkAutoWB->setChecked(m_cam.getAutoWhiteBalance());
    setupSlider(m_tbWBTemp, m_cam.wbTempRange());
    m_tbWBTemp->setEnabled(!m_chkAutoWB->isChecked());
    m_lblWBTempV->setText(QString::number(m_tbWBTemp->value()) + QLatin1Char('K'));

    m_chkBacklight->setChecked(m_cam.getBacklightCompensation());

    m_chkAutoExposure->setChecked(m_cam.getExposureAuto());
    setupSlider(m_tbExposure, m_cam.exposureRange());
    m_tbExposure->setEnabled(!m_chkAutoExposure->isChecked());
    m_lblExposureV->setText(QString::number(m_tbExposure->value()));

    m_chkAutoFocus->setChecked(m_cam.getAutoFocus());
    setupSlider(m_tbFocus, m_cam.focusRange());
    m_tbFocus->setEnabled(!m_chkAutoFocus->isChecked());
    m_lblFocusV->setText(QString::number(m_tbFocus->value()));

    switch (m_cam.getTrackingFrame()) {
    case Insta360Link::TrackingFrame::Head:     m_cboTrackFrame->setCurrentIndex(0); break;
    case Insta360Link::TrackingFrame::HalfBody: m_cboTrackFrame->setCurrentIndex(1); break;
    case Insta360Link::TrackingFrame::FullBody: m_cboTrackFrame->setCurrentIndex(2); break;
    }

    m_updating = false;
}

void MainWindow::updateUiState()
{
    const bool connected = m_cam.connected();
    m_grpPTZ->setEnabled(connected);
    m_grpModes->setEnabled(connected);
    m_grpImage->setEnabled(connected);
    m_grpExposure->setEnabled(connected);
    m_grpFocus->setEnabled(connected);
    m_grpPresets->setEnabled(connected);
    m_btnPreview->setEnabled(connected);

    // Framing only works on Link 2.
    if (connected && m_cam.cameraModel() == Insta360Link::CameraModel::Link) {
        m_cboTrackFrame->setVisible(false);
        m_lblTrackFrame->setText(QStringLiteral("Framing: Link 2 only"));
    } else if (connected) {
        m_cboTrackFrame->setVisible(true);
        m_cboTrackFrame->setEnabled(true);
        m_lblTrackFrame->setText(QStringLiteral("Framing:"));
    }
}

/* ===== Settings ===== */

QString MainWindow::settingsPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + QStringLiteral("/insta360link.ini");
}

void MainWindow::saveSettings()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    QSettings ini(settingsPath(), QSettings::IniFormat);

    ini.setValue(QStringLiteral("Device/LastDevice"), m_cam.devicePath());
    ini.setValue(QStringLiteral("PTZ/PanStep"), m_sePanStep->value());
    ini.setValue(QStringLiteral("PTZ/TiltStep"), m_seTiltStep->value());
    for (int i = 0; i < 6; ++i) {
        const auto p = m_cam.getPreset(quint8(i));
        ini.setValue(QStringLiteral("Presets/Name%1").arg(i), m_edtPresetName[i]->text());
        ini.setValue(QStringLiteral("Presets/Valid%1").arg(i), p.valid);
        ini.setValue(QStringLiteral("Presets/Pan%1").arg(i), p.pan);
        ini.setValue(QStringLiteral("Presets/Tilt%1").arg(i), p.tilt);
        ini.setValue(QStringLiteral("Presets/Zoom%1").arg(i), p.zoom);
    }
}

void MainWindow::loadSettings()
{
    if (!QFile::exists(settingsPath()))
        return;

    QSettings ini(settingsPath(), QSettings::IniFormat);

    m_sePanStep->setValue(ini.value(QStringLiteral("PTZ/PanStep"), 8).toInt());
    m_seTiltStep->setValue(ini.value(QStringLiteral("PTZ/TiltStep"), 8).toInt());

    const QString lastDev = ini.value(QStringLiteral("Device/LastDevice")).toString();
    if (!lastDev.isEmpty()) {
        for (int i = 0; i < m_cboDevice->count(); ++i) {
            if (m_cboDevice->itemData(i).toString() == lastDev) {
                m_cboDevice->setCurrentIndex(i);
                break;
            }
        }
    }

    for (int i = 0; i < 6; ++i) {
        Insta360Link::PresetPosition p;
        p.name = ini.value(QStringLiteral("Presets/Name%1").arg(i),
                           QStringLiteral("Preset %1").arg(i)).toString();
        m_edtPresetName[i]->setText(p.name);
        p.valid = ini.value(QStringLiteral("Presets/Valid%1").arg(i), false).toBool();
        p.pan = ini.value(QStringLiteral("Presets/Pan%1").arg(i), 0).toInt();
        p.tilt = ini.value(QStringLiteral("Presets/Tilt%1").arg(i), 0).toInt();
        p.zoom = ini.value(QStringLiteral("Presets/Zoom%1").arg(i), 100).toInt();
        m_cam.setPreset(quint8(i), p);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Save before closing the camera so LastDevice is still populated.
    saveSettings();
    stopPreview();
    m_cam.close();
    QWidget::closeEvent(event);
}
