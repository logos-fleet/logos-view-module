// The teardown surface of a GENERATED view plugin, checked the way ui-host
// actually reaches it: load the plugin, then resolve everything BY NAME
// through the meta-object.
//
// Why by name and not by calling the class directly: the host has no header
// for a module's generated plugin. It holds a QObject* from QPluginLoader and
// does QMetaObject::invokeMethod(obj, "aboutToUnload", Q_RETURN_ARG(int, rc)).
// When the meta-method is absent that call returns false and the host moves on
// -- which is indistinguishable from a view answering "Synchronous, nothing to
// wait for". So a test that linked the plugin class and called
// p.aboutToUnload() directly would still pass with the hook stripped from the
// meta-object. This one cannot: it only ever knows the strings.
//
// Three properties, each failing with its own sentence:
//   1. aboutToUnload() is an INVOKABLE method returning int (the host reads it
//      back with Q_RETURN_ARG(int) and must not need the SDK enum to do it);
//   2. unloadFinished() exists as a SIGNAL (a view that answers Asynchronous
//      with no way to say it is done would be refused the wait);
//   3. the completion actually ARRIVES -- the fixture backend finishes on a
//      later turn of the event loop, so this passes only if the generated
//      trampoline's QUEUED emission works.
//
// And one property of the plugin's LAYOUT, which no meta-object can show:
//
//   5. the LogosModules aggregate modules() returns OUTLIVES the backend. The
//      generated plugin owns both as unique_ptr members, so their declaration
//      order is the whole of the answer -- members are destroyed in reverse,
//      so the aggregate must be declared first. Declared the other way round,
//      any backend destructor that calls modules() reads freed memory, and
//      nothing says so: the context pointer is never cleared, so
//      isContextReady() keeps answering true. The fixture backend records
//      what it saw into a probe file; see ticker_panel_backend.h.

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QMetaMethod>
#include <QMetaObject>
#include <QPluginLoader>
#include <QTimer>

// The one header this checker shares with the plugin, and the real host has it
// too: initLogos(LogosAPI*) is the call that constructs the LogosModules
// aggregate, so nothing about modules()' lifetime can be observed without it.
#include "logos_api.h"

#include <cstdio>

static int g_failures = 0;

static void fail(const QString& why)
{
    qCritical().noquote() << "FAIL:" << why;
    ++g_failures;
}

// Receives unloadFinished() by NAME, the same way the host connects to it.
class Watcher : public QObject {
    Q_OBJECT
public:
    bool fired = false;
public Q_SLOTS:
    void onFinished()
    {
        fired = true;
        QCoreApplication::quit();
    }
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    if (argc < 2) {
        qCritical() << "usage: ui_plugin_metaobject_check <plugin-path> [probe-file]";
        return 2;
    }

    // Where the fixture backend's destructor records what it saw. Exported
    // rather than passed, because the backend is reached only through the
    // plugin the loader constructs -- there is no seam to hand it an argument.
    const QString probePath = (argc >= 3) ? QString::fromLocal8Bit(argv[2])
                                          : QStringLiteral("dtor_probe.txt");
    QFile::remove(probePath);
    qputenv("LOGOS_VIEW_DTOR_PROBE", probePath.toLocal8Bit());

    QPluginLoader loader(QString::fromLocal8Bit(argv[1]));
    QObject* plugin = loader.instance();
    if (!plugin) {
        qCritical().noquote() << "FAIL: could not load plugin:" << loader.errorString();
        return 1;
    }

    const QMetaObject* mo = plugin->metaObject();

    // The evidence, printed whether or not anything fails: the method table the
    // host sees. A reviewer should be able to read the teardown surface off the
    // test log without rerunning anything.
    std::printf("--- QMetaObject method table for %s ---\n", mo->className());
    for (int i = 0; i < mo->methodCount(); ++i) {
        const QMetaMethod m = mo->method(i);
        const char* kind = "?";
        switch (m.methodType()) {
        case QMetaMethod::Signal:      kind = "SIGNAL"; break;
        case QMetaMethod::Slot:        kind = "SLOT";   break;
        case QMetaMethod::Method:      kind = "METHOD"; break;
        case QMetaMethod::Constructor: kind = "CTOR";   break;
        }
        const char* ret = (m.typeName() && *m.typeName()) ? m.typeName() : "void";
        std::printf("  [%2d] %-7s %-26s -> %s\n", i, kind,
                    m.methodSignature().constData(), ret);
    }
    std::printf("--- end method table ---\n");
    std::fflush(stdout);

    // 1. aboutToUnload() -- present, invokable, int-returning.
    const int auIdx = mo->indexOfMethod("aboutToUnload()");
    if (auIdx < 0) {
        fail(QStringLiteral(
            "the generated plugin has no aboutToUnload() meta-method. ui-host resolves "
            "this by name; absent, invokeMethod returns false and every teardown of "
            "every view silently skips its chance to finish."));
    } else {
        const QMetaMethod au = mo->method(auIdx);
        if (au.methodType() != QMetaMethod::Method)
            fail(QStringLiteral("aboutToUnload() is not a Q_INVOKABLE method (methodType %1)")
                     .arg(int(au.methodType())));
        if (qstrcmp(au.typeName(), "int") != 0)
            fail(QStringLiteral("aboutToUnload() must return int, not '%1' -- the host reads "
                                "it with Q_RETURN_ARG(int) and has no SDK enum")
                     .arg(QString::fromLatin1(au.typeName())));
    }

    // 2. unloadFinished() -- present, and a SIGNAL.
    const int ufIdx = mo->indexOfSignal("unloadFinished()");
    if (ufIdx < 0) {
        fail(QStringLiteral(
            "the generated plugin has no unloadFinished() SIGNAL. A view that answers "
            "Asynchronous would have no way to report completion, so the host would "
            "wait out the full grace period on every teardown."));
    }

    if (g_failures > 0)
        return 1;

    // 3. Hand the plugin a LogosAPI, exactly as the host does: this is what
    // builds the LogosModules aggregate whose lifetime stage 5 is about. By
    // name again -- the host has no header for the generated class.
    LogosAPI api;
    const int initIdx = mo->indexOfMethod("initLogos(LogosAPI*)");
    if (initIdx < 0) {
        fail(QStringLiteral("the generated plugin has no initLogos(LogosAPI*) meta-method"));
        return 1;
    }
    if (!mo->method(initIdx).invoke(plugin, Qt::DirectConnection, Q_ARG(LogosAPI*, &api))) {
        fail(QStringLiteral("QMetaObject invoke of initLogos(LogosAPI*) returned false"));
        return 1;
    }

    // 4. Drive it exactly as the host does, and require the completion to land.
    Watcher watcher;
    QObject::connect(plugin, SIGNAL(unloadFinished()), &watcher, SLOT(onFinished()));

    int rc = -1;
    if (!QMetaObject::invokeMethod(plugin, "aboutToUnload", Q_RETURN_ARG(int, rc))) {
        fail(QStringLiteral("QMetaObject::invokeMethod(\"aboutToUnload\") returned false -- "
                            "this is the exact call ui-host makes."));
        return 1;
    }
    // 1 == LogosShutdown::Asynchronous. The fixture backend defers its
    // completion, so anything else means the hook never reached the backend.
    if (rc != 1) {
        fail(QStringLiteral("aboutToUnload() answered %1; the fixture backend returns "
                            "Asynchronous (1), so the SFINAE helper did not reach it")
                 .arg(rc));
        return 1;
    }

    // Bounded: if the queued emission never arrives this must FAIL, not hang.
    QTimer::singleShot(5000, &app, &QCoreApplication::quit);
    app.exec();

    if (!watcher.fired) {
        fail(QStringLiteral(
            "unloadFinished() never arrived. The fixture backend completed on a later "
            "turn of the event loop, so the generated trampoline's QUEUED emission is "
            "what did not work -- the host would have waited out the grace period."));
        return 1;
    }

    // 5. Destruction order. Destroy the plugin the way the host does at the
    // end of a teardown and read back what the backend's destructor saw.
    delete plugin;

    QFile probe(probePath);
    if (!probe.open(QIODevice::ReadOnly | QIODevice::Text)) {
        fail(QStringLiteral("the backend destructor left no probe at %1 -- either it did "
                            "not run when the plugin was destroyed, or the fixture is "
                            "not wired to LOGOS_VIEW_DTOR_PROBE").arg(probePath));
        return 1;
    }
    const QString seen = QString::fromUtf8(probe.readAll()).trimmed();
    std::printf("destruction-order probe: %s\n", seen.toLocal8Bit().constData());
    if (seen != QStringLiteral("modules-alive")) {
        fail(QStringLiteral(
            "the LogosModules aggregate was already destroyed when the backend's "
            "destructor ran (probe said '%1'). The generated plugin declares "
            "m_backend before m_logosModules, so the aggregate -- the thing "
            "modules() returns -- is destroyed FIRST. Any view backend that calls "
            "modules() from its destructor therefore reads freed memory, with no "
            "diagnostic: isContextReady() still answers true because the pointer is "
            "never cleared. Declare m_logosModules first in "
            "lidlMakeUiGlueHeader() so it is destroyed last.").arg(seen));
        return 1;
    }

    std::printf("OK: aboutToUnload/unloadFinished present, int-returning, the "
                "queued completion arrived, and modules() outlived the backend.\n");
    return 0;
}

#include "main.moc"
