#include "PreferencesDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace {
QSpinBox *mkSpin(int lo, int hi, int step, const QString &suffix = QString())
{
    auto *s = new QSpinBox;
    s->setRange(lo, hi);
    s->setSingleStep(step);
    if (!suffix.isEmpty()) s->setSuffix(suffix);
    return s;
}
QLabel *note(const QString &text)
{
    auto *l = new QLabel(text);
    l->setWordWrap(true);
    l->setEnabled(false);               // dimmed helper text
    return l;
}
}

PreferencesDialog::PreferencesDialog(const AppConfig &cfg, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Preferences"));
    resize(600, 480);

    // ------------------------------------------------ Audio output
    m_backend = new QComboBox;
    m_backend->addItem(tr("Automatic (GStreamer default)"), "auto");
    for (const AudioBackend &b : AudioOutput::availableBackends())
        m_backend->addItem(QStringLiteral("%1  (%2)").arg(b.label, b.factory), b.factory);
    m_hint = note(QString());
    m_device = new QComboBox;
    m_device->setMinimumContentsLength(24);
    m_refresh = new QPushButton(tr("Refresh"));
    m_refresh->setToolTip(tr("Re-scan the available output devices"));
    auto *devRow = new QHBoxLayout;
    devRow->addWidget(m_device, 1);
    devRow->addWidget(m_refresh);
    m_test = new QPushButton(tr("Play test tone"));
    m_testStatus = new QLabel;
    m_testStatus->setWordWrap(true);
    auto *testRow = new QHBoxLayout;
    testRow->addWidget(m_test);
    testRow->addWidget(m_testStatus, 1);

    auto *out = new QWidget;
    auto *form = new QFormLayout(out);
    form->addRow(tr("Sound service:"), m_backend);
    form->addRow(QString(), m_hint);
    form->addRow(tr("Output device:"), devRow);
    form->addRow(QString(), testRow);

    // ------------------------------------------------ Audio advanced
    m_buffer = mkSpin(0, 5000, 50, tr(" ms"));
    m_buffer->setSpecialValueText(tr("default"));
    m_latency = mkSpin(0, 1000, 10, tr(" ms"));
    m_latency->setSpecialValueText(tr("default"));
    m_custom = new QCheckBox(tr("Use a custom GStreamer sink instead of the settings above"));
    m_customEdit = new QLineEdit;
    m_customEdit->setPlaceholderText(tr("e.g.  alsasink device=hw:1,0     or     pulsesink device=my_sink"));

    auto *adv = new QWidget;
    auto *advLay = new QVBoxLayout(adv);
    auto *tuneBox = new QGroupBox(tr("Buffering"));
    auto *tuneForm = new QFormLayout(tuneBox);
    tuneForm->addRow(tr("Buffer size:"), m_buffer);
    tuneForm->addRow(tr("Period / latency time:"), m_latency);
    tuneForm->addRow(note(tr("Applied only to services that support it (ALSA, PulseAudio, JACK). "
                             "Smaller values = lower latency but a higher risk of dropouts.")));
    auto *customBox = new QGroupBox(tr("Custom output"));
    auto *customLay = new QVBoxLayout(customBox);
    customLay->addWidget(m_custom);
    customLay->addWidget(m_customEdit);
    customLay->addWidget(note(tr("Any GStreamer element or short pipeline that ends in an audio sink. "
                                 "For experts; an invalid pipeline falls back to the automatic output.")));
    advLay->addWidget(tuneBox);
    advLay->addWidget(customBox);
    advLay->addStretch(1);

    // ------------------------------------------------ Spectrum analyser
    m_spBars = mkSpin(8, 512, 8);
    m_spFMin = mkSpin(0, 90000, 100, tr(" Hz"));
    m_spFMax = mkSpin(500, 96000, 500, tr(" Hz"));
    m_spScale = new QComboBox;
    m_spScale->addItem(tr("Logarithmic (starts at 20 Hz or the minimum)"), true);
    m_spScale->addItem(tr("Linear"), false);
    m_spFloor = mkSpin(-120, -20, 5, tr(" dB"));
    m_spCeil = mkSpin(-60, 12, 5, tr(" dB"));
    m_spFft = new QComboBox;
    for (int b : {512, 1024, 2048, 4096, 8192})
        m_spFft->addItem(tr("%1 bands").arg(b), b);

    auto *sp = new QWidget;
    auto *spForm = new QFormLayout(sp);
    spForm->addRow(tr("Number of bars:"), m_spBars);
    spForm->addRow(tr("Lowest frequency:"), m_spFMin);
    spForm->addRow(tr("Highest frequency:"), m_spFMax);
    spForm->addRow(tr("Frequency axis:"), m_spScale);
    spForm->addRow(tr("Bottom of display:"), m_spFloor);
    spForm->addRow(tr("Top of display:"), m_spCeil);
    spForm->addRow(tr("FFT resolution:"), m_spFft);
    spForm->addRow(note(tr("Default: 0 – 22 kHz, 224 bars, 2048 FFT bands. Content above half of the file's "
                           "sample rate does not exist, so with 44.1 kHz material the top of a 22 kHz axis "
                           "stays empty. The FFT resolution takes effect from the next track.")));

    // ------------------------------------------------ Level meters
    m_mMin = mkSpin(-120, -20, 5, tr(" dB"));
    m_mMax = mkSpin(0, 12, 1, tr(" dB"));
    m_mWidth = mkSpin(4, 40, 1, tr(" px"));
    m_mStep = new QDoubleSpinBox;
    m_mStep->setRange(0.25, 6.0);
    m_mStep->setSingleStep(0.25);
    m_mStep->setDecimals(2);
    m_mStep->setSuffix(tr(" dB"));
    m_mStep->setToolTip(tr("Smaller step = more LEDs. 0.75 dB gives 124 LEDs over -90 … +3 dB."));
    m_mScale = new QCheckBox(tr("Show dB scale"));
    m_mTrue = new QCheckBox(tr("Detect inter-sample (true) peaks"));
    m_mTrue->setToolTip(tr("The signal is oversampled to 192 kHz before measuring, so the red LED lights when "
                           "the reconstructed waveform goes above 0 dB between two samples."));

    auto *me = new QWidget;
    auto *meForm = new QFormLayout(me);
    meForm->addRow(tr("Lowest level:"), m_mMin);
    meForm->addRow(tr("Highest level:"), m_mMax);
    meForm->addRow(tr("Width per channel:"), m_mWidth);
    meForm->addRow(tr("dB per LED (default 0.75 = 124 LEDs):"), m_mStep);
    meForm->addRow(QString(), m_mScale);
    meForm->addRow(QString(), m_mTrue);
    meForm->addRow(note(tr("LED colours: green, yellow (from -18 dB), orange (from -6 dB), "
                           "red above 0 dB (also for inter-sample overs when true-peak detection is on). "
                           "A single LED marks the peak hold; overs are held longer.")));

    auto *tabs = new QTabWidget;
    tabs->addTab(out, tr("Audio output"));
    tabs->addTab(adv, tr("Audio advanced"));
    tabs->addTab(sp, tr("Spectrum"));
    tabs->addTab(me, tr("Level meters"));

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply |
                                     QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults);
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(tabs, 1);
    lay->addWidget(box);

    // ------------------------------------------------ wiring
    setConfig(cfg);
    QTimer::singleShot(0, this, [this] { refreshDevices(); });     // device scan after the dialog is visible

    connect(m_backend, &QComboBox::currentIndexChanged, this, [this] {
        updateHint();
        fillDevices(QString());           // new service -> back to its default device
    });
    connect(m_custom, &QCheckBox::toggled, this, &PreferencesDialog::updateEnabled);
    connect(m_refresh, &QPushButton::clicked, this, &PreferencesDialog::refreshDevices);
    connect(m_test, &QPushButton::clicked, this, [this] {
        m_test->setEnabled(false);
        m_testStatus->setText(tr("Playing test tone…"));
        AudioOutput::playTestTone(config().audio, this, [this](const QString &err) {
            m_test->setEnabled(true);
            m_testStatus->setText(err.isEmpty() ? tr("Done – did you hear a 440 Hz tone?")
                                                : tr("Failed: %1").arg(err));
        });
    });
    connect(box, &QDialogButtonBox::clicked, this, [this, box](QAbstractButton *b) {
        switch (box->standardButton(b)) {
        case QDialogButtonBox::Ok:     emit applied(config()); accept(); break;
        case QDialogButtonBox::Apply:  emit applied(config()); break;
        case QDialogButtonBox::Cancel: reject(); break;
        case QDialogButtonBox::RestoreDefaults: setConfig(AppConfig()); break;
        default: break;
        }
    });
}

void PreferencesDialog::setConfig(const AppConfig &cfg)
{
    const AudioOutputConfig &c = cfg.audio;
    {
        QSignalBlocker block(m_backend);
        int i = m_backend->findData(c.backend.isEmpty() ? QStringLiteral("auto") : c.backend);
        if (i < 0) {                       // configured service not installed on this machine
            m_backend->addItem(tr("%1 (not available)").arg(c.backend), c.backend);
            i = m_backend->count() - 1;
        }
        m_backend->setCurrentIndex(i);
    }
    m_buffer->setValue(c.bufferMs);
    m_latency->setValue(c.latencyMs);
    m_customEdit->setText(c.customPipeline);
    m_custom->setChecked(c.useCustom);
    updateHint();
    fillDevices(c.device);

    const SpectrumConfig &s = cfg.spectrum;
    m_spBars->setValue(s.bars);
    m_spFMin->setValue(int(s.fMinHz));
    m_spFMax->setValue(int(s.fMaxHz));
    m_spScale->setCurrentIndex(s.logScale ? 1 : 0);
    m_spFloor->setValue(s.floorDb);
    m_spCeil->setValue(s.ceilDb);
    m_spFft->setCurrentIndex(std::max(0, m_spFft->findData(s.fftBands)));

    const MeterConfig &m = cfg.meter;
    m_mMin->setValue(int(m.minDb));
    m_mMax->setValue(int(m.maxDb));
    m_mWidth->setValue(m.barWidth);
    m_mStep->setValue(m.ledStepDb);
    m_mScale->setChecked(m.showScale);
    m_mTrue->setChecked(m.truePeak);

    updateEnabled();
}

AppConfig PreferencesDialog::config() const
{
    AppConfig a;
    a.audio.backend = m_backend->currentData().toString();
    a.audio.device = m_device->currentData().toString();
    a.audio.bufferMs = m_buffer->value();
    a.audio.latencyMs = m_latency->value();
    a.audio.useCustom = m_custom->isChecked();
    a.audio.customPipeline = m_customEdit->text().trimmed();

    a.spectrum.bars = m_spBars->value();
    a.spectrum.fMinHz = m_spFMin->value();
    a.spectrum.fMaxHz = m_spFMax->value();
    a.spectrum.logScale = m_spScale->currentData().toBool();
    a.spectrum.floorDb = m_spFloor->value();
    a.spectrum.ceilDb = m_spCeil->value();
    a.spectrum.fftBands = m_spFft->currentData().toInt();
    a.spectrum.sanitize();

    a.meter.minDb = m_mMin->value();
    a.meter.maxDb = m_mMax->value();
    a.meter.barWidth = m_mWidth->value();
    a.meter.ledStepDb = m_mStep->value();
    a.meter.showScale = m_mScale->isChecked();
    a.meter.truePeak = m_mTrue->isChecked();
    a.meter.sanitize();
    return a;
}

void PreferencesDialog::fillDevices(const QString &wanted)
{
    const QString be = m_backend->currentData().toString();
    m_device->clear();
    m_device->addItem(tr("Default device"), QString());
    if (be != "auto")
        for (const AudioDeviceInfo &d : m_devices)
            if (d.backend == be) m_device->addItem(d.name, d.name);

    int i = m_device->findData(wanted);
    if (i < 0 && !wanted.isEmpty()) {      // keep a remembered device that is currently unplugged
        m_device->addItem(tr("%1 (not found)").arg(wanted), wanted);
        i = m_device->count() - 1;
    }
    m_device->setCurrentIndex(std::max(0, i));
    updateEnabled();
}

void PreferencesDialog::updateHint()
{
    m_hint->setText(AudioOutput::backendHint(m_backend->currentData().toString()));
}

void PreferencesDialog::updateEnabled()
{
    const bool custom = m_custom->isChecked();
    const bool autoBackend = m_backend->currentData().toString() == "auto";
    m_backend->setEnabled(!custom);
    m_device->setEnabled(!custom && !autoBackend);
    m_refresh->setEnabled(!custom && !autoBackend);
    m_buffer->setEnabled(!custom);
    m_latency->setEnabled(!custom);
    m_customEdit->setEnabled(custom);
}

void PreferencesDialog::refreshDevices()
{
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    m_devices = AudioOutput::devices();
    QGuiApplication::restoreOverrideCursor();
    fillDevices(m_device->currentData().toString());
}
