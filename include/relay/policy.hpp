#pragma once
#include <cstdint>
#include <cstddef>

enum class BackpressurePolicy {
    DropOldest,
    DropNewest,
    Disconnect
};

struct RelayPolicy {
    size_t max_pending_bytes = 2 * 1024 * 1024;
    BackpressurePolicy backpressure = BackpressurePolicy::DropOldest;
    uint64_t pusher_down_disconnect_ms = 2000;
};
