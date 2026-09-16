// Stub of the per-module logos_sdk.h umbrella. The real one aggregates the
// typed dependency wrappers; the generated glue only constructs it from a
// LogosAPI* and hands it to the backend, which is what this reproduces.
#pragma once

class LogosAPI;

struct LogosModules {
    // Liveness canary, for the destruction-order check in
    // ticker_panel_backend.h. A real LogosModules owns the typed client
    // wrappers a view calls its dependencies through; asking "does one still
    // exist?" is the cheapest faithful stand-in for "is modules() still
    // usable?", and unlike reading the freed object it is DEFINED behaviour,
    // so the check cannot pass by accident on a heap that happens not to have
    // reused the block yet.
    static inline int liveCount = 0;

    explicit LogosModules(LogosAPI* api) : api(api) { ++liveCount; }
    ~LogosModules() { --liveCount; }

    LogosModules(const LogosModules&) = delete;
    LogosModules& operator=(const LogosModules&) = delete;

    LogosAPI* api = nullptr;
};
