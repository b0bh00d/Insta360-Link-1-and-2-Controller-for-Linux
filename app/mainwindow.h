/*
  mainwindow.h - Qt Widgets GUI for the Insta360 Link controller.
  ==================================================================
  C++/Qt port of umainform.pas (the Lazarus TfrmMain).

  Full GUI with:
    - Device selection and connection
    - Live video preview
    - PTZ controls (press-and-hold D-pad + zoom slider)
    - AI Tracking / DeskView / Whiteboard / Overhead mode buttons
    - Image adjustment sliders (brightness, contrast, ...)
    - Exposure and Focus controls
    - 6 preset position slots (save / recall)
    - Activity log
    - Settings persistence via QSettings (INI)
*/
#pragma once

#include <QImage>
#include <QWidget>

#include "insta360link.h"
#include "videocapture.h"

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QTimer;
class QToolButton;

class MainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void refreshDeviceList();
    void onConnect();
    void onDisconnect();

    // PTZ (press-and-hold)
    void ptzPressed(int panDir, int tiltDir);
    void ptzReleased();
    void ptzRepeat();
    void onHome();
    void onZoomChange(int value);

    // Preview
    void onPreviewToggle();
    void onPreviewTimer();

    // Modes
    void onTrackingOn();
    void onTrackingOff();
    void onTrackFrameChange(int index);
    void onTrackingPoll();
    void onGimbalReset();

    // Presets
    void onPresetRecall(int index);
    void onPresetSave(int index);

    // Diagnostics / log
    void onScanXU();
    void onCamLog(const QString &msg);

private:
    void buildUi();
    void startPreview();
    void stopPreview();
    void updateUiState();
    void readCurrentValues();
    void setupSlider(QSlider *s, const Insta360Link::CtrlRange &range);
    void saveSettings();
    void loadSettings();
    QString settingsPath() const;

    Insta360Link m_cam;
    bool         m_updating = false;      // prevent slider feedback loops

    VideoCapture m_capture;
    QImage       m_previewImg;

    QTimer *m_previewTimer = nullptr;
    QTimer *m_ptzTimer = nullptr;
    QTimer *m_trackPollTimer = nullptr;
    int     m_ptzPanStep = 0;             // degrees per tick while a D-pad button is held
    int     m_ptzTiltStep = 0;

    // --- Top bar ---
    QComboBox   *m_cboDevice = nullptr;
    QPushButton *m_btnConnect = nullptr;
    QPushButton *m_btnDisconnect = nullptr;
    QPushButton *m_btnRefresh = nullptr;
    QLabel      *m_lblStatus = nullptr;

    // --- PTZ ---
    QGroupBox *m_grpPTZ = nullptr;
    QSpinBox  *m_sePanStep = nullptr;
    QSpinBox  *m_seTiltStep = nullptr;
    QSlider   *m_tbZoom = nullptr;
    QLabel    *m_lblZoomVal = nullptr;

    // --- Modes ---
    QGroupBox   *m_grpModes = nullptr;
    QPushButton *m_btnTrackingOn = nullptr;
    QPushButton *m_btnTrackingOff = nullptr;
    QLabel      *m_lblTrackFrame = nullptr;
    QComboBox   *m_cboTrackFrame = nullptr;

    // --- Image ---
    QGroupBox *m_grpImage = nullptr;
    QSlider *m_tbBrightness = nullptr;  QLabel *m_lblBrightnessV = nullptr;
    QSlider *m_tbContrast = nullptr;    QLabel *m_lblContrastV = nullptr;
    QSlider *m_tbSaturation = nullptr;  QLabel *m_lblSaturationV = nullptr;
    QSlider *m_tbSharpness = nullptr;   QLabel *m_lblSharpnessV = nullptr;
    QSlider *m_tbGain = nullptr;        QLabel *m_lblGainV = nullptr;
    QCheckBox *m_chkAutoWB = nullptr;
    QCheckBox *m_chkBacklight = nullptr;
    QSlider *m_tbWBTemp = nullptr;      QLabel *m_lblWBTempV = nullptr;

    // --- Exposure ---
    QGroupBox *m_grpExposure = nullptr;
    QCheckBox *m_chkAutoExposure = nullptr;
    QSlider   *m_tbExposure = nullptr;  QLabel *m_lblExposureV = nullptr;

    // --- Focus ---
    QGroupBox *m_grpFocus = nullptr;
    QCheckBox *m_chkAutoFocus = nullptr;
    QSlider   *m_tbFocus = nullptr;     QLabel *m_lblFocusV = nullptr;

    // --- Preview ---
    QGroupBox   *m_grpPreview = nullptr;
    QLabel      *m_imgPreview = nullptr;
    QPushButton *m_btnPreview = nullptr;
    QLabel      *m_lblPreviewInfo = nullptr;

    // --- Presets ---
    QGroupBox   *m_grpPresets = nullptr;
    QLineEdit   *m_edtPresetName[6] = {};
    QPushButton *m_btnPresetRecall[6] = {};
    QPushButton *m_btnPresetSave[6] = {};

    // --- Log ---
    QPlainTextEdit *m_memoLog = nullptr;
    QPushButton    *m_btnClearLog = nullptr;
    QPushButton    *m_btnScanXU = nullptr;
};
