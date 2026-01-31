#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>
#include "device.h"

// Forward declaration for HID device handling
class DeepCoolDevice;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void detectDevices();
    void connectToDevice();
    void disconnectFromDevice();
    void startMonitoring();
    void stopMonitoring();
    void updateSystemInfo();
    void changeDisplayMode();
    void updateRefreshRate();
    void toggleFahrenheit(bool checked);
    void toggleAlarm(bool checked);

private:
    void setupUI();
    void updateStatusBar(const QString &message);

    // UI Components
    QComboBox *deviceComboBox;
    QPushButton *connectButton;
    QPushButton *disconnectButton;
    QPushButton *refreshButton;
    QPushButton *startMonitorButton;
    QPushButton *stopMonitorButton;

    QComboBox *displayModeCombo;
    QSpinBox *updateIntervalSpinBox;
    QCheckBox *fahrenheitCheckBox;
    QCheckBox *alarmCheckBox;

    // Info display labels
    QLabel *cpuTempLabel;
    QLabel *cpuUsageLabel;
    QLabel *gpuTempLabel;
    QLabel *gpuUsageLabel;
    QLabel *ramUsageLabel;
    QLabel *connectionStatusLabel;

    // Backend
    QVector<DeviceInfo> devices;
    DeepCoolDevice *device;
    QTimer *updateTimer;
    bool isConnected;
    bool isMonitoring;

    // System monitoring
    float getCPUTemperature();
    float getCPUUsage();
    float getGPUTemperature();
    float getGPUUsage();
    float getRAMUsage();
};

#endif // MAINWINDOW_H
