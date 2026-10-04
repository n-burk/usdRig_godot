// Offline subdivision of the tutorial ball, using the same OpenSubdiv
// Catmull-Clark scheme and face-varying rules as USD/Hydra.
#define _USE_MATH_DEFINES
#include <cmath>
#include <opensubdiv/far/topologyDescriptor.h>
#include <opensubdiv/far/topologyRefinerFactory.h>
#include <opensubdiv/far/primvarRefiner.h>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <memory>
#include <vector>
#include <map>
using namespace OpenSubdiv;
struct Value {
    float x = 0, y = 0, z = 0;
    void Clear() { x = y = z = 0; }
    void AddWithWeight(const Value &v, float w) { x += v.x*w; y += v.y*w; z += v.z*w; }
};
// Bake subdivision into sparse linear stencils. Runtime evaluates these
// against rigExec's deformed control points, including derivative normals.
struct Weights {
    std::map<int, float> values;
    void Clear() { values.clear(); }
    void AddWithWeight(const Weights &v, float w) {
        for (auto entry : v.values) values[entry.first] += entry.second * w;
    }
};
int main(int argc, char **argv) {
    if (argc != 3 && argc != 4) return 2;
    std::ifstream in(argv[1]);
    int nv, nf, nt, rule;
    if (!(in >> nv >> nf >> nt >> rule) || nv < 1 || nf < 1 || nt < 1) return 2;
    std::vector<Value> positions(nv), uv(nt);
    for (auto &p : positions) in >> p.x >> p.y >> p.z;
    std::vector<int> counts(nf);
    int corners = 0;
    for (auto &n : counts) { in >> n; corners += n; }
    std::vector<int> indices(corners), uvIndices(corners);
    for (auto &i : indices) in >> i;
    for (auto &v : uv) in >> v.x >> v.y;
    for (auto &i : uvIndices) in >> i;
    if (!in) return 2;
    Far::TopologyDescriptor desc;
    desc.numVertices = nv;
    desc.numFaces = nf;
    desc.numVertsPerFace = counts.data();
    desc.vertIndicesPerFace = indices.data();
    Far::TopologyDescriptor::FVarChannel channel;
    channel.numValues = nt;
    channel.valueIndices = uvIndices.data();
    desc.numFVarChannels = 1;
    desc.fvarChannels = &channel;
    Sdc::Options options;
    options.SetVtxBoundaryInterpolation(Sdc::Options::VTX_BOUNDARY_EDGE_AND_CORNER);
    options.SetFVarLinearInterpolation(static_cast<Sdc::Options::FVarLinearInterpolation>(rule));
    using Factory = Far::TopologyRefinerFactory<Far::TopologyDescriptor>;
    std::unique_ptr<Far::TopologyRefiner> refiner(Factory::Create(desc, Factory::Options(Sdc::SCHEME_CATMARK, options)));
    if (!refiner) return 3;
    Far::TopologyRefiner::UniformOptions refinement(2);
    refinement.fullTopologyInLastLevel = true;
    refiner->RefineUniform(refinement);
    Far::PrimvarRefiner primvars(*refiner);
    std::vector<Weights> weights(nv);
    for (int i = 0; i < nv; ++i) weights[i].values[i] = 1;
    for (int level = 1; level <= 2; ++level) {
        std::vector<Value> next(refiner->GetLevel(level).GetNumVertices());
        std::vector<Value> nextUv(refiner->GetLevel(level).GetNumFVarValues());
        primvars.Interpolate(level, positions, next);
        primvars.InterpolateFaceVarying(level, uv, nextUv);
        std::vector<Weights> nextWeights(next.size());
        primvars.Interpolate(level, weights, nextWeights);
        weights.swap(nextWeights);
        positions.swap(next);
        uv.swap(nextUv);
    }
    std::vector<Value> limit(positions.size()), du(positions.size()), dv(positions.size()), limitUv(uv.size());
    primvars.Limit(positions, limit, du, dv);
    primvars.LimitFaceVarying(uv, limitUv);
    if (argc == 4) {
        std::vector<Weights> p(weights.size()), u(weights.size()), v(weights.size());
        primvars.Limit(weights, p, u, v);
        std::ofstream bindings(argv[3]);
        bindings << std::setprecision(9) << '[';
        for (size_t i = 0; i < p.size(); ++i) {
            if (i) bindings << ',';
            bindings << '[';
            int group = 0;
            for (const Weights *w : {&p[i], &u[i], &v[i]}) {
                if (group++) bindings << ',';
                bindings << '[';
                bool first = true;
                for (auto entry : w->values) {
                    if (std::abs(entry.second) < 1e-10f) continue;
                    if (!first) bindings << ',';
                    first = false;
                    bindings << entry.first << ',' << entry.second;
                }
                bindings << ']';
            }
            bindings << ']';
        }
        bindings << ']';
        if (!bindings) return 6;
    }
    std::ofstream out(argv[2]);
    out << std::setprecision(9) << "# USD tutorial ball: OpenSubdiv level 2, limit positions/normals/UVs\n";
    for (const auto &p : limit) out << "v " << p.x << ' ' << p.y << ' ' << p.z << '\n';
    // USD st has bottom-left origin. OBJ also uses bottom-left; Godot's
    // OBJ importer performs the V conversion. Never substitute sphere UVs.
    for (const auto &v : limitUv) out << "vt " << v.x << ' ' << v.y << '\n';
    for (size_t i = 0; i < limit.size(); ++i) {
        Value n{du[i].y*dv[i].z-du[i].z*dv[i].y,
                du[i].z*dv[i].x-du[i].x*dv[i].z,
                du[i].x*dv[i].y-du[i].y*dv[i].x};
        float length = std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);
        if (length < 1e-8f) return 4;
        out << "vn " << n.x/length << ' ' << n.y/length << ' ' << n.z/length << '\n';
    }
    const auto &level = refiner->GetLevel(2);
    for (int f = 0; f < level.GetNumFaces(); ++f) {
        auto verts = level.GetFaceVertices(f);
        auto tex = level.GetFaceFVarValues(f);
        out << 'f';
        for (int c = 0; c < verts.size(); ++c)
            out << ' ' << verts[c]+1 << '/' << tex[c]+1 << '/' << verts[c]+1;
        out << '\n';
    }
    return out ? 0 : 5;
}
