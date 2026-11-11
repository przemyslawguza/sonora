#pragma once
#include <QDialog>
#include "Config.h"

class QComboBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QDoubleSpinBox;

class PreferencesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PreferencesDialog(const AppConfig &cfg, QWidget *parent = nullptr);
    AppConfig config() const;

signals:
    void applied(const AppConfig &cfg);

private:
    void setConfig(const AppConfig &cfg);
    void fillDevices(const QString &wanted);
    void updateHint();
    void updateEnabled();
    void refreshDevices();

    // audio
    QComboBox *m_backend, *m_device;
    QLabel *m_hint, *m_testStatus;
    QPushButton *m_refresh, *m_test;
    QSpinBox *m_buffer, *m_latency;
    QCheckBox *m_custom;
    QLineEdit *m_customEdit;
    QList<AudioDeviceInfo> m_devices;
    // spectrum
    QSpinBox *m_spBars, *m_spFMin, *m_spFMax, *m_spFloor, *m_spCeil;
    QComboBox *m_spScale, *m_spFft;
    // meters
    QSpinBox *m_mMin, *m_mMax, *m_mWidth;
    QDoubleSpinBox *m_mStep;
    QCheckBox *m_mScale, *m_mTrue;
    // visualisation
    QSpinBox *m_vizDelay;
};
