#include "virtual-mic-controller.hpp"
#include "source-selection-dialog.hpp"

extern "C" {
#include <obs-module.h>
}

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QFileInfo>
#include <QWidget>
#include <QSettings>
#include <QStringList>

#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

#define VMLOG(level, fmt, ...) blog(level, "[OBS Virtual Mic] " fmt, ##__VA_ARGS__)

VirtualMicController::VirtualMicController(QWidget *mainWindow, QObject *parent)
    : QObject(parent), mainWindow_(mainWindow), controls_(this)
{
    connect(&controls_, &ControlsIntegration::toggleRequested, this, &VirtualMicController::toggle);
    connect(&controls_, &ControlsIntegration::settingsRequested, this, &VirtualMicController::openSourceSettings);
}

VirtualMicController::~VirtualMicController()
{
    shutdown();
}

bool VirtualMicController::initialize()
{
    if (!controls_.install(mainWindow_)) {
        VMLOG(LOG_ERROR, "Could not insert Start Virtual Mic control into OBS Controls dock");
        return false;
    }
    loadSettings();
    setState(State::Stopped);
    VMLOG(LOG_INFO, "Plugin initialized with %lld selected virtual-mic source(s)",
          static_cast<long long>(selectedSources_.size()));
    return true;
}

void VirtualMicController::shutdown()
{
    stopVirtualMic();
    controls_.uninstall();
}

void VirtualMicController::toggle()
{
    if (state_ == State::Stopped || state_ == State::Error)
        startVirtualMic();
    else if (state_ == State::Running)
        stopVirtualMic();
}

void VirtualMicController::openSourceSettings()
{
    const QStringList available = SourceRouting::availableAudioSources();
    if (available.isEmpty()) {
        showError("No OBS audio sources are currently available.");
        return;
    }

    SourceSelectionDialog dialog(available, selectedSources_, mainWindow_);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QStringList newSelection = dialog.selectedSources();
    if (newSelection.isEmpty()) {
        showError("Select at least one audio source for OBS Virtual Mic.");
        return;
    }

    selectedSources_ = newSelection;
    saveSettings();

    // Re-route live without interrupting the virtual microphone device/FIFO.
    if (state_ == State::Running) {
        std::string routingError;
        if (!sourceRouting_.apply(selectedSources_, routingError)) {
            showError(QString::fromStdString(routingError));
        } else {
            VMLOG(LOG_INFO, "Updated virtual mic source selection while running (%lld sources)",
                  static_cast<long long>(selectedSources_.size()));
        }
    }
}

bool VirtualMicController::startVirtualMic()
{
    setState(State::Starting);

    QString error;
    if (!createFifo(error)) {
        setState(State::Error);
        showError(error);
        return false;
    }

    if (!createVirtualDevice(error)) {
        destroyFifo();
        setState(State::Error);
        showError(error);
        return false;
    }

    if (selectedSources_.isEmpty()) {
        selectedSources_ = SourceRouting::defaultSourcesFromTrack1();
        if (selectedSources_.isEmpty()) {
            destroyVirtualDevice();
            destroyFifo();
            setState(State::Error);
            showError("No Virtual Mic sources are selected. Click the gear next to Start Virtual Mic and choose at least one audio source.");
            return false;
        }
        saveSettings();
    }

    std::string routingError;
    if (!sourceRouting_.apply(selectedSources_, routingError)) {
        destroyVirtualDevice();
        destroyFifo();
        setState(State::Error);
        showError(QString::fromStdString(routingError));
        return false;
    }

    std::string routerError;
    if (!audioRouter_.start(fifoPath_.toStdString(), SourceRouting::kVirtualMicMixIndex, routerError)) {
        sourceRouting_.restore();
        destroyVirtualDevice();
        destroyFifo();
        setState(State::Error);
        showError(QString::fromStdString(routerError));
        return false;
    }

    setState(State::Running);
    VMLOG(LOG_INFO, "Virtual mic started as input source '%s'", kSourceName);
    return true;
}

void VirtualMicController::stopVirtualMic()
{
    if (state_ == State::Stopped && moduleId_.isEmpty() && fifoPath_.isEmpty())
        return;

    setState(State::Stopping);
    audioRouter_.stop();
    sourceRouting_.restore();
    destroyVirtualDevice();
    destroyFifo();
    setState(State::Stopped);
    VMLOG(LOG_INFO, "Virtual mic stopped");
}

bool VirtualMicController::createFifo(QString &error)
{
    if (!fifoPath_.isEmpty())
        return true;

    // IMPORTANT FOR FLATPAK:
    // module-pipe-source is instantiated by the host PipeWire/PulseAudio server,
    // so the FIFO path must resolve to the same inode in both the OBS sandbox and
    // the host server's mount namespace. /run/user/$UID is not reliable for this
    // across Flatpak namespaces. QStandardPaths::CacheLocation resolves to the
    // app's real ~/.var/app/<id>/cache/... path under Flatpak, which is visible
    // from both sides. Native OBS also gets an ordinary per-user cache path.
    QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cacheRoot.isEmpty()) {
        error = "Could not determine a writable per-user cache directory for the virtual mic FIFO.";
        return false;
    }

    QDir cacheDir(cacheRoot);
    if (!cacheDir.mkpath("obs-virtual-mic")) {
        error = QString("Could not create virtual mic cache directory under '%1'.").arg(cacheRoot);
        return false;
    }

    const QString fifoDir = cacheDir.filePath("obs-virtual-mic");
    QFileInfo dirInfo(fifoDir);
    if (!dirInfo.exists() || !dirInfo.isDir() || !dirInfo.isWritable()) {
        error = QString("Virtual mic FIFO directory is not writable: %1").arg(fifoDir);
        return false;
    }

    fifoPath_ = QDir(fifoDir).filePath(
        QString("audio-%1.fifo").arg(QCoreApplication::applicationPid()));
    QFile::remove(fifoPath_);

    if (::mkfifo(fifoPath_.toUtf8().constData(), 0600) != 0) {
        error = QString("Could not create virtual mic FIFO '%1': %2")
                    .arg(fifoPath_, QString::fromLocal8Bit(std::strerror(errno)));
        fifoPath_.clear();
        return false;
    }

    VMLOG(LOG_INFO, "Created host-visible virtual mic FIFO '%s'", fifoPath_.toUtf8().constData());
    return true;
}

void VirtualMicController::destroyFifo()
{
    if (fifoPath_.isEmpty())
        return;

    const QString path = fifoPath_;
    fifoPath_.clear();
    if (!QFile::remove(path) && QFile::exists(path))
        VMLOG(LOG_WARNING, "Could not remove FIFO '%s'", path.toUtf8().constData());
}

bool VirtualMicController::createVirtualDevice(QString &error)
{
    if (!moduleId_.isEmpty())
        return true;

    if (fifoPath_.isEmpty()) {
        error = "Internal error: virtual mic FIFO was not created.";
        return false;
    }

    if (QStandardPaths::findExecutable("pactl").isEmpty()) {
        error = "Could not find 'pactl'. Install PipeWire/PulseAudio compatibility tools and try again.";
        return false;
    }

    QString out;
    QStringList args{
        "load-module",
        "module-pipe-source",
        QString("file=%1").arg(fifoPath_),
        QString("source_name=%1").arg(kSourceName),
        "format=float32le",
        "rate=48000",
        "channels=2",
        "channel_map=front-left,front-right",
        "source_properties=device.description=OBS_Virtual_Mic device.icon_name=audio-input-microphone media.class=Audio/Source"
    };

    if (!runPactl(args, out, error)) {
        error = QString("Could not create OBS Virtual Mic input source: %1\n\n"
                        "This version requires PipeWire/PulseAudio support for module-pipe-source.")
                    .arg(error);
        return false;
    }

    moduleId_ = out.trimmed();
    bool ok = false;
    moduleId_.toUInt(&ok);
    if (!ok) {
        error = QString("pactl created the device but returned an unexpected module id: '%1'").arg(moduleId_);
        moduleId_.clear();
        return false;
    }

    VMLOG(LOG_INFO, "Created input-only virtual source '%s' as PulseAudio module %s",
          kSourceName, moduleId_.toUtf8().constData());
    return true;
}

void VirtualMicController::destroyVirtualDevice()
{
    if (moduleId_.isEmpty())
        return;

    QString out, error;
    const QString id = moduleId_;
    moduleId_.clear();
    if (!runPactl({"unload-module", id}, out, error))
        VMLOG(LOG_WARNING, "Could not unload PulseAudio module %s: %s", id.toUtf8().constData(),
              error.toUtf8().constData());
}

bool VirtualMicController::runPactl(const QStringList &args, QString &stdoutText, QString &error) const
{
    QProcess proc;
    proc.setProgram("pactl");
    proc.setArguments(args);
    proc.start();
    if (!proc.waitForStarted(2000)) {
        error = QString("Failed to start pactl: %1").arg(proc.errorString());
        return false;
    }
    if (!proc.waitForFinished(4000)) {
        proc.kill();
        proc.waitForFinished(1000);
        error = "pactl timed out.";
        return false;
    }

    stdoutText = QString::fromUtf8(proc.readAllStandardOutput());
    const QString stderrText = QString::fromUtf8(proc.readAllStandardError()).trimmed();
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        error = stderrText.isEmpty() ? QString("pactl exited with code %1").arg(proc.exitCode()) : stderrText;
        return false;
    }
    return true;
}

void VirtualMicController::setState(State state)
{
    state_ = state;
    auto *button = controls_.button();
    if (!button)
        return;

    switch (state_) {
    case State::Stopped:
    case State::Error:
        button->setText("Start Virtual Mic");
        button->setEnabled(true);
        break;
    case State::Starting:
        button->setText("Starting Virtual Mic…");
        button->setEnabled(false);
        break;
    case State::Running:
        button->setText("Stop Virtual Mic");
        button->setEnabled(true);
        break;
    case State::Stopping:
        button->setText("Stopping Virtual Mic…");
        button->setEnabled(false);
        break;
    }
}

void VirtualMicController::loadSettings()
{
    QSettings settings("OBSProject", "OBSVirtualMic");
    selectedSources_ = settings.value("selectedSources").toStringList();
    if (selectedSources_.isEmpty())
        selectedSources_ = SourceRouting::defaultSourcesFromTrack1();
}

void VirtualMicController::saveSettings() const
{
    QSettings settings("OBSProject", "OBSVirtualMic");
    settings.setValue("selectedSources", selectedSources_);
    settings.sync();
}

void VirtualMicController::showError(const QString &message)
{
    VMLOG(LOG_ERROR, "%s", message.toUtf8().constData());
    QMessageBox::critical(mainWindow_, "OBS Virtual Mic", message);
}
