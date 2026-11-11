#include <QApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <cstdio>
#include <thread>
#include <gst/gst.h>
#include "MainWindow.h"

int main(int argc, char **argv)
{
    QElapsedTimer clock;
    clock.start();
    const bool profile = qEnvironmentVariableIsSet("SONORA_PROFILE");     // SONORA_PROFILE=1 sonora
    auto mark = [&](const char *what) {
        if (profile) std::fprintf(stderr, "[sonora] %6lld ms  %s\n", (long long)clock.elapsed(), what);
    };

    // GStreamer's registry/plugin initialisation runs in parallel with building the window.
    // Nothing touches GStreamer before it is joined below (the player pipeline is created lazily).
    std::thread gstInit([] { gst_init(nullptr, nullptr); });

    QApplication app(argc, argv);
    QApplication::setApplicationName("sonora");
    QApplication::setApplicationDisplayName("Sonora");
    QApplication::setOrganizationName("sonora");
    QApplication::setApplicationVersion(SONORA_VERSION);
    mark("Qt ready");

    MainWindow w;
    mark("main window built");
    w.show();
    app.processEvents();                     // paint the window first ...
    mark("window shown");

    gstInit.join();                          // ... then make sure GStreamer is ready
    mark("GStreamer initialised");

    const QStringList args = QApplication::arguments().mid(1);
    if (!args.isEmpty()) {
        w.addPaths(args);
        w.playFirst();
    }
    QTimer::singleShot(0, [&] { mark("event loop running"); });
    return app.exec();
}
