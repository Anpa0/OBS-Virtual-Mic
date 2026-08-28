#pragma once

#include "audio-router.hpp"
#include "controls-integration.hpp"
#include "source-routing.hpp"

#include <QObject>
#include <QString>
#include <QStringList>

class QWidget;

class VirtualMicController : public QObject {
    Q_OBJECT
public:
    explicit VirtualMicController(QWidget *mainWindow, QObject *parent = nullptr);
    ~VirtualMicController() override;

    bool initialize();
    void shutdown();

private slots:
    void toggle();
    void openSourceSettings();

private:
    enum class State { Stopped, Starting, Running, Stopping, Error };

    bool startVirtualMic();
    void stopVirtualMic();
    bool createVirtualDevice(QString &error);
    void destroyVirtualDevice();
    bool createFifo(QString &error);
    void destroyFifo();
    bool runPactl(const QStringList &args, QString &stdoutText, QString &error) const;
    void setState(State state);
    void showError(const QString &message);
    void loadSettings();
    void saveSettings() const;

    QWidget *mainWindow_ = nullptr;
    ControlsIntegration controls_;
    AudioRouter audioRouter_;
    SourceRouting sourceRouting_;
    State state_ = State::Stopped;
    QString moduleId_;
    QString fifoPath_;
    QStringList selectedSources_;

    static constexpr const char *kSourceName = "OBS_VirtualMic";
};
