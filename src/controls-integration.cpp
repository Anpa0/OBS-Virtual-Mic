#include "controls-integration.hpp"

#include <QAbstractButton>
#include <QBoxLayout>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QLayout>
#include <QLayoutItem>
#include <QSizePolicy>
#include <QString>
#include <QToolButton>
#include <QWidget>

ControlsIntegration::ControlsIntegration(QObject *parent) : QObject(parent) {}
ControlsIntegration::~ControlsIntegration() { uninstall(); }

QDockWidget *ControlsIntegration::findControlsDock(QWidget *mainWindow) const
{
    if (!mainWindow)
        return nullptr;

    if (auto *dock = mainWindow->findChild<QDockWidget *>("controlsDock"))
        return dock;

    const auto docks = mainWindow->findChildren<QDockWidget *>();
    for (auto *dock : docks) {
        if (dock->objectName().contains("control", Qt::CaseInsensitive) ||
            dock->windowTitle().contains("Controls", Qt::CaseInsensitive)) {
            return dock;
        }
    }

    return nullptr;
}

QPushButton *ControlsIntegration::findVirtualCameraButton(QDockWidget *controls) const
{
    if (!controls)
        return nullptr;

    const auto buttons = controls->findChildren<QPushButton *>();
    for (auto *button : buttons) {
        const QString name = button->objectName().toLower();
        const QString text = button->text().toLower();
        if ((name.contains("virtual") && name.contains("cam")) ||
            text.contains("virtual camera")) {
            return button;
        }
    }

    return nullptr;
}

QPushButton *ControlsIntegration::findStudioModeButton(QDockWidget *controls) const
{
    if (!controls)
        return nullptr;

    const auto buttons = controls->findChildren<QPushButton *>();
    for (auto *button : buttons) {
        const QString name = button->objectName().toLower();
        const QString text = button->text().toLower();
        if ((name.contains("studio") && name.contains("mode")) ||
            text.contains("studio mode")) {
            return button;
        }
    }

    return nullptr;
}

static bool layoutItemContainsWidget(QLayoutItem *item, QWidget *needle)
{
    if (!item || !needle)
        return false;

    if (QWidget *widget = item->widget()) {
        if (widget == needle || widget->isAncestorOf(needle))
            return true;
    }

    if (QLayout *layout = item->layout()) {
        for (int i = 0; i < layout->count(); ++i) {
            if (layoutItemContainsWidget(layout->itemAt(i), needle))
                return true;
        }
    }

    return false;
}

static int indexContainingWidget(QBoxLayout *layout, QWidget *needle)
{
    if (!layout || !needle)
        return -1;

    for (int i = 0; i < layout->count(); ++i) {
        if (layoutItemContainsWidget(layout->itemAt(i), needle))
            return i;
    }

    return -1;
}

static bool layoutAlsoContains(QBoxLayout *layout, QWidget *widget)
{
    return indexContainingWidget(layout, widget) >= 0;
}

static QAbstractButton *findVirtualCameraSettingsButton(QPushButton *virtualCam)
{
    if (!virtualCam)
        return nullptr;

    QWidget *searchRoot = virtualCam->parentWidget();
    if (!searchRoot)
        return nullptr;

    const auto siblings = searchRoot->findChildren<QAbstractButton *>(QString(), Qt::FindDirectChildrenOnly);
    QAbstractButton *bestSettingsButton = nullptr;
    int bestScore = -1;

    for (auto *sibling : siblings) {
        if (sibling == virtualCam || sibling->icon().isNull())
            continue;

        const QString haystack = (sibling->objectName() + QLatin1Char(' ') +
                                  sibling->toolTip() + QLatin1Char(' ') +
                                  sibling->accessibleName() + QLatin1Char(' ') +
                                  sibling->text()).toLower();

        int score = 0;
        if (haystack.contains("config")) score += 8;
        if (haystack.contains("setting")) score += 8;
        if (haystack.contains("propert")) score += 6;
        if (haystack.contains("virtual") && haystack.contains("cam")) score += 4;
        if (haystack.contains("gear") || haystack.contains("cog")) score += 4;
        if (haystack.contains("pause") || haystack.contains("studio")) score -= 10;

        if (score > bestScore) {
            bestScore = score;
            bestSettingsButton = sibling;
        }
    }

    return (bestSettingsButton && bestScore > 0) ? bestSettingsButton : nullptr;
}

void ControlsIntegration::buildVirtualMicRow(QWidget *rowParent, QPushButton *virtualCam)
{
    ownedRow_ = new QWidget(rowParent);
    ownedRow_->setObjectName("obsVirtualMicRow");
    ownedRow_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    auto *row = new QHBoxLayout(ownedRow_);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);

    button_ = new QPushButton("Start Virtual Mic", ownedRow_);
    button_->setObjectName("obsVirtualMicButton");
    button_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    if (virtualCam) {
        if (virtualCam->minimumHeight() > 0)
            button_->setMinimumHeight(virtualCam->minimumHeight());
        if (virtualCam->maximumHeight() < QWIDGETSIZE_MAX)
            button_->setMaximumHeight(virtualCam->maximumHeight());
    }

    row->addWidget(button_);

    settingsButton_ = new QToolButton(ownedRow_);
    settingsButton_->setObjectName("obsVirtualMicSettingsButton");
    settingsButton_->setToolTip("Choose Virtual Mic audio sources");
    settingsButton_->setText(QString::fromUtf8("⚙"));
    settingsButton_->setMinimumWidth(30);

    if (auto *cameraSettings = findVirtualCameraSettingsButton(virtualCam)) {
        settingsButton_->setIcon(cameraSettings->icon());
        settingsButton_->setIconSize(cameraSettings->iconSize());
        settingsButton_->setText(QString());
        settingsButton_->setFixedWidth(cameraSettings->width() > 0 ? cameraSettings->width() : 30);
        if (cameraSettings->minimumHeight() > 0)
            settingsButton_->setMinimumHeight(cameraSettings->minimumHeight());
    }

    row->addWidget(settingsButton_);

    connect(button_, &QPushButton::clicked, this, &ControlsIntegration::toggleRequested);
    connect(settingsButton_, &QToolButton::clicked, this, &ControlsIntegration::settingsRequested);
}

bool ControlsIntegration::insertBeforeStudioMode(QDockWidget *controls, QPushButton *virtualCam, QPushButton *studioMode)
{
    if (!controls || !controls->widget() || !studioMode)
        return false;

    // Anchor on Studio Mode rather than Virtual Camera. The desired position is
    // unambiguously immediately BEFORE Studio Mode. OBS 32.x wraps the Virtual
    // Camera button+gear in nested layouts, which made attempts to insert "after
    // Virtual Camera" land in the wrong wrapper or fall back to the bottom.
    //
    // Walk outward from Studio Mode and find the first vertical/box layout that
    // contains Studio Mode and also contains Virtual Camera. That is the actual
    // Controls-stack layout. Then insert our row at Studio Mode's item index.
    for (QWidget *candidate = studioMode; candidate; candidate = candidate->parentWidget()) {
        QWidget *parent = candidate->parentWidget();
        if (!parent)
            break;

        auto *layout = qobject_cast<QBoxLayout *>(parent->layout());
        if (!layout)
            continue;

        const int studioIndex = indexContainingWidget(layout, studioMode);
        if (studioIndex < 0)
            continue;

        // Requiring Virtual Camera to be in the same stack prevents accidentally
        // inserting into an inner horizontal row or some unrelated wrapper.
        if (virtualCam && !layoutAlsoContains(layout, virtualCam))
            continue;

        buildVirtualMicRow(parent, virtualCam);
        layout->insertWidget(studioIndex, ownedRow_);
        return true;
    }

    return false;
}

bool ControlsIntegration::insertInControlsDock(QDockWidget *controls, QPushButton *virtualCam)
{
    if (!controls || !controls->widget())
        return false;

    auto *box = qobject_cast<QBoxLayout *>(controls->widget()->layout());
    if (!box)
        return false;

    buildVirtualMicRow(controls->widget(), virtualCam);
    box->addWidget(ownedRow_);
    return true;
}

bool ControlsIntegration::install(QWidget *mainWindow)
{
    if (!mainWindow || button_)
        return false;

    QDockWidget *controls = findControlsDock(mainWindow);
    if (!controls)
        return false;

    auto *virtualCam = findVirtualCameraButton(controls);
    auto *studioMode = findStudioModeButton(controls);

    if (studioMode && insertBeforeStudioMode(controls, virtualCam, studioMode))
        return true;

    return insertInControlsDock(controls, virtualCam);
}

void ControlsIntegration::uninstall()
{
    if (ownedRow_) {
        delete ownedRow_;
        ownedRow_ = nullptr;
        button_ = nullptr;
        settingsButton_ = nullptr;
    } else if (button_) {
        delete button_;
        button_ = nullptr;
        settingsButton_ = nullptr;
    }
}
