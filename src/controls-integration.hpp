#pragma once

#include <QObject>
#include <QPointer>
#include <QPushButton>
#include <QToolButton>

class QDockWidget;
class QWidget;

class ControlsIntegration : public QObject {
    Q_OBJECT
public:
    explicit ControlsIntegration(QObject *parent = nullptr);
    ~ControlsIntegration() override;

    bool install(QWidget *mainWindow);
    void uninstall();
    QPushButton *button() const { return button_.data(); }
    QToolButton *settingsButton() const { return settingsButton_.data(); }

signals:
    void toggleRequested();
    void settingsRequested();

private:
    QDockWidget *findControlsDock(QWidget *mainWindow) const;
    QPushButton *findVirtualCameraButton(QDockWidget *controls) const;
    QPushButton *findStudioModeButton(QDockWidget *controls) const;
    bool insertBeforeStudioMode(QDockWidget *controls, QPushButton *virtualCam, QPushButton *studioMode);
    bool insertInControlsDock(QDockWidget *controls, QPushButton *virtualCam);
    void buildVirtualMicRow(QWidget *rowParent, QPushButton *virtualCam);

    QPointer<QPushButton> button_;
    QPointer<QToolButton> settingsButton_;
    QPointer<QWidget> ownedRow_;
};
