#pragma once
#include <string>
#include <functional>

struct StreamId {
    std::string app;
    std::string stream;
    
    bool operator==(const StreamId& other) const {
        return app == other.app && stream == other.stream;
    }
    
    bool operator<(const StreamId& other) const {
        if (app != other.app) return app < other.app;
        return stream < other.stream;
    }
    
    std::string to_string() const {
        return app + "/" + stream;
    }
};

namespace std {
    template<>
    struct hash<StreamId> {
        size_t operator()(const StreamId& id) const {
            return hash<string>()(id.app) ^ (hash<string>()(id.stream) << 1);
        }
    };
}
