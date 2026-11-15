#pragma once
#include <atomic>
#include <bit>
#include <unordered_map>
#include "Game/ModelShapes.hpp"

namespace OW::ModelShapes {
struct WireMesh {
    std::vector<Vector3> vertices;
    std::vector<std::array<uint16_t,2>> edges;
};
inline WireMesh BuildWireMesh(const Shape& shape) {
    WireMesh mesh;
    mesh.vertices.reserve(120);mesh.edges.reserve(60);
    const auto index=[&](Vector3 point) {
        // Exact equality preserves the previous wire's endpoints and segments.
        for(size_t i=0;i<mesh.vertices.size();++i)
            if(mesh.vertices[i]==point)return uint16_t(i);
        mesh.vertices.push_back(point);
        return uint16_t(mesh.vertices.size()-1);
    };
    Wire(shape,[&](Vector3 a,Vector3 b){mesh.edges.push_back({index(a),index(b)});});
    return mesh;
}
struct WireKey {
    std::array<uint32_t,7> values{};
    explicit WireKey(const Shape& shape):values{
        std::bit_cast<uint32_t>(shape.a.X),std::bit_cast<uint32_t>(shape.a.Y),std::bit_cast<uint32_t>(shape.a.Z),
        std::bit_cast<uint32_t>(shape.b.X),std::bit_cast<uint32_t>(shape.b.Y),std::bit_cast<uint32_t>(shape.b.Z),
        std::bit_cast<uint32_t>(shape.radius)} {}
    bool operator==(const WireKey&) const = default;
};
struct WireKeyHash {
    size_t operator()(const WireKey& key) const {
        size_t hash=1469598103934665603ull;
        for(auto value:key.values) { hash^=value;hash*=1099511628211ull; }
        return hash;
    }
};
class WireCache {
    struct Entry { WireMesh mesh; uint64_t used; };
    std::unordered_map<WireKey,Entry,WireKeyHash> entries;
    uint64_t sequence=0;
public:
    static constexpr size_t Limit=512;
    uint64_t hits=0,misses=0;
    const WireMesh& Get(const Shape& shape) {
        const WireKey key(shape);
        auto found=entries.find(key);
        if(found!=entries.end()) { ++hits;found->second.used=++sequence;return found->second.mesh; }
        ++misses;
        if(entries.size()>=Limit) {
            auto oldest=entries.begin();
            for(auto it=entries.begin();it!=entries.end();++it)
                if(it->second.used<oldest->second.used)oldest=it;
            entries.erase(oldest);
        }
        return entries.emplace(key,Entry{BuildWireMesh(shape),++sequence}).first->second.mesh;
    }
    size_t Size() const { return entries.size(); }
};
// Totals are diagnostics only. Cache and per-frame coordinates belong to the
// calling render thread; the test server observes only relaxed atomic counters.
inline std::atomic<uint64_t> WireCacheHits{0},WireCacheMisses{0},WireProjectedVertices{0},WireCandidateSegments{0};
template<class World,class Project,class Line>
bool ProjectWireMesh(const WireMesh& mesh,World&& world,Project&& project,Line&& line) {
    std::array<Vector2,120> screen{};
    std::array<bool,120> visible{};
    if(mesh.vertices.size()>screen.size())return false;
    for(size_t v=0;v<mesh.vertices.size();++v)
        visible[v]=project(world(mesh.vertices[v]),screen[v]) && std::isfinite(screen[v].X) && std::isfinite(screen[v].Y);
    bool drew=false;
    for(const auto& edge:mesh.edges)
        if(visible[edge[0]] && visible[edge[1]]) { line(screen[edge[0]],screen[edge[1]]);drew=true; }
    return drew;
}
inline WireCache& RenderWireCache() { static thread_local WireCache cache;return cache; }
template<class World,class Project,class Line>
bool DrawCachedWire(const Shape& shape,World&& world,Project&& project,Line&& line) {
    auto& cache=RenderWireCache();
    const auto misses=cache.misses;
    const auto& mesh=cache.Get(shape);
    (cache.misses==misses?WireCacheHits:WireCacheMisses).fetch_add(1,std::memory_order_relaxed);
    WireProjectedVertices.fetch_add(mesh.vertices.size(),std::memory_order_relaxed);
    WireCandidateSegments.fetch_add(mesh.edges.size(),std::memory_order_relaxed);
    return ProjectWireMesh(mesh,world,project,line);
}
}
