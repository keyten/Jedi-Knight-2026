// Copyright (C) 2026 OpenJK contributors. GPL-2.0-or-later.
#ifndef FX_PHYSICALIZATION_SOURCES_H
#define FX_PHYSICALIZATION_SOURCES_H
#include <cstdint>
#include <limits>
#include <vector>
#include <algorithm>
#include <tuple>

namespace FxPhysical {
// Runtime metadata only. Never serialized or passed through the renderer ABI.
// Attached/view/physics sources remain unsuitable for spatial aggregation.
enum SourceDomain { SourceWorld = 0, SourcePortal = 1, SourceAttached = 2,
                    SourceView = 4, SourcePhysics = 8 };
struct SourceContext {
    uint64_t generation = 0, id = 0;
    unsigned domain = SourceWorld;
};

// A bounded snapshot of automatic frontend submissions in one FX scene.
// It carries no renderer acceptance claims and owns no density or lifetime.
class SourceLedger {
    struct Record { SourceContext source; bool glow; };
    std::vector<Record> records;
public:
    struct Row { SourceContext source; unsigned density = 0, glow = 0; };
    uint64_t generation = 0;
    int time = -1;
    unsigned untracked = 0, overflow = 0;
    void Begin(uint64_t value, int sceneTime = -1) { generation = value; time = sceneTime; records.clear(); untracked = overflow = 0; }
    void Submit(const SourceContext& source, bool glow) {
        if (!source.id) { ++untracked; return; }
        if (records.size() >= 1200) { ++overflow; return; }
        if (records.capacity() == 0) records.reserve(1200);
        Record r; r.source = source; r.glow = glow; records.push_back(r);
    }
    std::vector<Row> Rows() {
        std::sort(records.begin(), records.end(), [](const Record& a, const Record& b) {
            return std::tie(a.source.generation,a.source.id,a.source.domain) <
                   std::tie(b.source.generation,b.source.id,b.source.domain);
        });
        std::vector<Row> rows;
        for (const auto& r : records) {
            if (rows.empty() || rows.back().source.id != r.source.id ||
                rows.back().source.generation != r.source.generation || rows.back().source.domain != r.source.domain) {
                Row row; row.source = r.source; rows.push_back(row);
            }
            if (r.glow) ++rows.back().glow; else ++rows.back().density;
        }
        return rows;
    }
};

class SourceTracker {
    uint64_t generation = 1, next = 0;
    bool enabled = false, exhausted = false, scoped = false;
    SourceContext current;
public:
    uint64_t Generation() const { return generation; }
    bool Enabled() const { return enabled && !exhausted; }
    bool Valid(const SourceContext& c) const {
        return Enabled() && c.id && c.generation == generation;
    }
    SourceContext Capture() const { return Valid(current) ? current : SourceContext(); }
    void Reset() {
        // Fail closed rather than ever reusing a source identifier.
        if (generation == std::numeric_limits<uint64_t>::max()) exhausted = true;
        else ++generation;
        next = 0;
        current = SourceContext();
    }
    void Enable(bool value) {
        if (enabled != value) { Reset(); enabled = value; }
    }
    SourceContext New(unsigned domain) {
        SourceContext c;
        if (!Enabled() || next == std::numeric_limits<uint64_t>::max()) return c;
        c.generation = generation; c.id = ++next; c.domain = domain;
        return c;
    }
    SourceContext Play(unsigned domain) {
        if (!scoped) return New(domain);
        // Deferred children with stale/missing parents must not invent owners.
        if (!Valid(current) || ((current.domain ^ domain) & SourcePortal)) return SourceContext();
        SourceContext c = current;
        c.domain |= domain;
        return c;
    }
    class Scope {
        SourceTracker& tracker;
        SourceContext previous;
        bool wasScoped, active;
    public:
        Scope(SourceTracker& t, const SourceContext& c)
            : tracker(t), previous(t.current), wasScoped(t.scoped), active(t.Enabled()) {
            if (active) { t.current = t.Valid(c) ? c : SourceContext(); t.scoped = true; }
        }
        ~Scope() {
            if (active) { tracker.current = previous; tracker.scoped = wasScoped; }
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    };
};
}
#endif
