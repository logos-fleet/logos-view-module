// The USER-WRITTEN half of a view: the backend the generated plugin owns.
//
// Two DIFFERENT names meet here, and the fixture keeps them different on
// purpose because a real module does:
//   * TickerBackend      -- the class declared in Ticker.rep, so repc emits
//                           TickerBackendSimpleSource / ...SourceAPI, and the
//                           generated plugin derives TickerBackendViewPluginBase;
//   * TickerPanelBackend -- the backend class, which the generator derives from
//                           the MODULE name in metadata.json (ticker_panel ->
//                           TickerPanel -> TickerPanelBackend).
// A fixture that collapsed them into one name would stop testing that the
// generator threads the right one into each position.
//
// LogosUiPluginContext is the REAL cpp/logos_ui_plugin_context.h, never a stub.
// That is deliberate: the generated glue calls
// _logos_codegen_::maybeUiPluginAboutToUnload(*m_backend, cb), so merely
// COMPILING this fixture is what proves the emitter and that header still
// agree. Those two are precisely the pair that rotted apart across repos.
#pragma once

#include <QFile>
#include <QIODevice>
#include <QTimer>

#include "rep_Ticker_source.h"
#include "logos_sdk.h"
#include "logos_ui_plugin_context.h"

class TickerPanelBackend : public TickerBackendSimpleSource,
                           public LogosUiPluginContext {
    Q_OBJECT
public:
    explicit TickerPanelBackend(QObject* parent = nullptr)
        : TickerBackendSimpleSource(parent) {}

    // The destruction-order probe. A view backend is entitled to reach its
    // dependencies from its destructor -- modules() is the ONLY way it can
    // talk to them, and teardown is exactly when a view wants to (say a last
    // "I am going away" call). The generated plugin owns both the backend and
    // the LogosModules aggregate modules() returns, so plain C++ member order
    // decides which outlives the other: members die in REVERSE declaration
    // order, so the aggregate has to be declared FIRST.
    //
    // Emitted the other way round, this destructor reads freed memory with no
    // diagnostic at all -- isContextReady() still answers true, because the
    // pointer the backend holds is never cleared. That is why the probe asks
    // the aggregate's own live count rather than dereferencing it: a
    // use-after-free that happens to read intact bytes would otherwise pass.
    ~TickerPanelBackend() override
    {
        const bool aggregateStillAlive =
            isContextReady() && LogosModules::liveCount > 0;
        const QString probe = qEnvironmentVariable("LOGOS_VIEW_DTOR_PROBE");
        if (probe.isEmpty())
            return;
        QFile f(probe);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            f.write(aggregateStillAlive ? "modules-alive\n" : "modules-destroyed\n");
    }

    void refresh() override {}

protected:
    // Answers Asynchronous and finishes LATER, on a return to the event loop.
    // The deferral is the point: it forces the completion to travel the
    // trampoline the generated plugin installed, rather than resolving inline
    // where a broken queued emission would still look like it had worked.
    LogosShutdown aboutToUnload() override {
        QTimer::singleShot(0, this, [this]() { unloadFinished(); });
        return LogosShutdown::Asynchronous;
    }
};
