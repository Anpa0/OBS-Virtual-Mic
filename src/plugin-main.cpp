extern "C" {
#include <obs-module.h>
#include <obs-frontend-api.h>
}

#include "virtual-mic-controller.hpp"

#include <QMainWindow>
#include <QMetaObject>
#include <QPointer>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-virtual-mic", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
    return "Creates a temporary Linux virtual microphone from OBS audio mix 1.";
}

static QPointer<VirtualMicController> g_controller;

static void frontendEvent(enum obs_frontend_event event, void *)
{
    if (event == OBS_FRONTEND_EVENT_EXIT && g_controller)
        g_controller->shutdown();
}

bool obs_module_load(void)
{
    auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
    if (!mainWindow) {
        blog(LOG_ERROR, "[OBS Virtual Mic] obs_frontend_get_main_window returned null");
        return false;
    }

    // Defer UI mutation until the current frontend event loop turn finishes.
    QMetaObject::invokeMethod(mainWindow, [mainWindow] {
        if (g_controller)
            return;
        auto *controller = new VirtualMicController(mainWindow, mainWindow);
        if (!controller->initialize()) {
            controller->deleteLater();
            return;
        }
        g_controller = controller;
    }, Qt::QueuedConnection);

    obs_frontend_add_event_callback(frontendEvent, nullptr);
    blog(LOG_INFO, "[OBS Virtual Mic] module loaded (v%s)", OBS_VIRTUAL_MIC_VERSION);
    return true;
}

void obs_module_unload(void)
{
    obs_frontend_remove_event_callback(frontendEvent, nullptr);
    if (g_controller) {
        g_controller->shutdown();
        delete g_controller;
        g_controller = nullptr;
    }
    blog(LOG_INFO, "[OBS Virtual Mic] module unloaded");
}
