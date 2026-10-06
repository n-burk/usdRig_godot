#include "rigexec_player.h"
#include "rigexec_character.h"
#include "rigExecRuntime/runtime.h"
// The presentation schema; its generated header needs format.h's types.
#include "rigExecBinary/format.h"
#include "flatbuffers/verifier.h"
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <vector>

namespace rigexec {
using namespace godot;

namespace {

// A control's input is a Double or a Float.
double ScalarValue(const rigExec::RrInputValue &value) {
    return value.tag == rigExec::RrInputTag::Float ? double(value.f32) : value.f64;
}

} // namespace

void RigExecPlayer::_clear_presentation() {
    for (auto &v : _visuals) {
        if (v.node) { remove_child(v.node); memdelete(v.node); }
    }
    _visuals.clear();
    _controls.clear(); _control_slots.clear(); _source_info.clear();
    _has_presentation = false;
}

bool RigExecPlayer::set_control(const String &name, double value) {
    if (!_control_slots.has(name) || !std::isfinite(value)) {
        _last_error = "Expected a finite value for an exposed controller: " + name;
        return false;
    }
    if (!_ensure_reader(&_last_error)) return false;
    const Control &control = _controls[size_t(int64_t(_control_slots[name]))];
    rigExec::RrInputValue held{};
    held.tag = rigExec::RrInputTag::Double;
    held.f64 = value;
    std::string why;
    if (!_reader->SetInputAt(control.input, held, &why)) {
        _last_error = String(why.c_str()); return false;
    }
    _last_error = "";
    return true;
}

Dictionary RigExecPlayer::get_controls() const {
    Dictionary result;
    if (!_reader) return result;
    // Defaults and values come from the file's inputs; the input path is
    // internal wiring, not the game interface.
    for (const Control &control : _controls) {
        Dictionary metadata;
        metadata["name"] = control.name;
        metadata["default"] = ScalarValue(_reader->GetInputInfo(control.input).defaultValue);
        metadata["unit"] = control.unit;
        metadata["value"] = ScalarValue(_reader->GetInputValue(control.input));
        result[control.name] = metadata;
    }
    return result;
}

bool RigExecPlayer::_set(const StringName &name, const Variant &value) {
    const String n(name);
    if (!n.begins_with("controls/")) return false;
    if (value.get_type() != Variant::FLOAT && value.get_type() != Variant::INT) return false;
    return set_control(n.substr(9).replace("/", "."), value) && evaluate();
}

bool RigExecPlayer::_get(const StringName &name, Variant &value) const {
    const String n(name);
    const String key = n.substr(9).replace("/", ".");
    if (!n.begins_with("controls/") || !_control_slots.has(key) || !_reader) return false;
    const Control &control = _controls[size_t(int64_t(_control_slots[key]))];
    value = ScalarValue(_reader->GetInputValue(control.input)); return true;
}

void RigExecPlayer::_get_property_list(List<PropertyInfo> *list) const {
    for (const Control &control : _controls)
        list->push_back(PropertyInfo(Variant::FLOAT, "controls/" + control.name.replace(".", "/")));
}

void RigExecPlayer::_validate_property(PropertyInfo &p) const {
    if (_has_presentation && String(p.name) == "skeleton_path")
        p.usage = PROPERTY_USAGE_STORAGE;
}

bool RigExecPlayer::_load_presentation() {
    auto fail = [&](const String &reason) {
        _clear_presentation(); _last_error = "Invalid embedded presentation: " + reason; return false;
    };
    // The reader opened _data, which verified the file's root and bounded
    // and verified its nested presentation.
    const rigExec::fb::File *file = rigExec::fb::GetFile(_data.ptr());
    const flatbuffers::Vector<uint8_t> *nested = file->presentation();
    if (!nested || nested->size() == 0) return true;
    // An owned copy keeps every read aligned whatever the byte array's
    // alignment; it is verified again on its own.
    const std::vector<uint8_t> buffer(nested->data(), nested->data() + nested->size());
    flatbuffers::Verifier verifier(buffer.data(), buffer.size());
    if (!rigExec::fb::VerifyPresentationBuffer(verifier)) return fail("REXP buffer");
    const rigExec::fb::Presentation *asset = rigExec::fb::GetPresentation(buffer.data());
    if (asset->version() != 1) return fail("version");
    if (const auto *controls = asset->controls()) {
        for (const rigExec::fb::PresentationControl *c : *controls) {
            const String name = String::utf8(c->name()->c_str(), int(c->name()->size()));
            if (name.is_empty() || name.contains("/") || _control_slots.has(name))
                return fail("controller name");
            size_t input = 0;
            if (!_reader->FindInput(c->input()->str(), &input))
                return fail("control input " + String::utf8(c->input()->c_str()));
            const rigExec::RrInputTag type = _reader->GetInputInfo(input).type;
            if (type != rigExec::RrInputTag::Double && type != rigExec::RrInputTag::Float)
                return fail("control input type");
            const char *unit = nullptr;
            switch (c->unit()) {
            case rigExec::fb::PresentationUnit::AssetUnits: unit = "asset_units"; break;
            case rigExec::fb::PresentationUnit::Degrees: unit = "degrees"; break;
            case rigExec::fb::PresentationUnit::Ratio: unit = "ratio"; break;
            }
            if (!unit) return fail("control unit");
            _control_slots[name] = int64_t(_controls.size());
            _controls.push_back({name, input, unit});
        }
    }
    const auto *meshes = asset->meshes();
    if (!meshes || meshes->size() == 0) return fail("no render meshes");
    for (const rigExec::fb::PresentationMesh *m : *meshes) {
        Visual visual;
        visual.points_path = m->pointsPath()->str();
        const uint32_t source_count = m->sourcePointCount();
        if (source_count == 0 || source_count > uint32_t(INT32_MAX) || visual.points_path.empty())
            return fail("point binding");
        visual.source_count = int(source_count);
        // CSR stencils: three groups (p, u, v) per render vertex.
        const auto *offsets = m->stencilOffsets();
        const auto *weights = m->stencilWeights();
        const auto *indices16 = m->stencilIndices16();
        const auto *indices32 = m->stencilIndices32();
        if (!offsets || !weights || (indices16 == nullptr) == (indices32 == nullptr))
            return fail("stencil tables");
        const size_t entries = indices16 ? indices16->size() : indices32->size();
        if (entries != weights->size() || (indices16 != nullptr) != (source_count <= 65535))
            return fail("stencil indices");
        if (offsets->size() < 4 || (offsets->size() - 1) % 3 != 0 || offsets->Get(0) != 0 ||
            offsets->Get(offsets->size() - 1) != entries)
            return fail("stencil offsets");
        const size_t vertex_count = (offsets->size() - 1) / 3;
        visual.stencils.resize(vertex_count);
        for (size_t i = 0; i < vertex_count; ++i) {
            Stencil &s = visual.stencils[i];
            std::vector<Coefficient> *dest[] = {&s.p, &s.u, &s.v};
            for (size_t group = 0; group < 3; ++group) {
                const uint32_t begin = offsets->Get(uint32_t(3 * i + group));
                const uint32_t end = offsets->Get(uint32_t(3 * i + group + 1));
                if (end <= begin || end > entries) return fail("stencil group");
                dest[group]->reserve(end - begin);
                for (uint32_t k = begin; k < end; ++k) {
                    const uint32_t index = indices16 ? indices16->Get(k) : indices32->Get(k);
                    const float w = weights->Get(k);
                    if (index >= source_count || !std::isfinite(w)) return fail("stencil coefficient");
                    dest[group]->push_back({int(index), w});
                }
            }
        }
        const auto *vertices = m->vertexIndices();
        const auto *uv = m->uvs();
        if (!vertices || !uv || vertices->size() == 0 || vertices->size() % 3 ||
            uv->size() != 2 * size_t(vertices->size()))
            return fail("triangle/UV count");
        visual.uvs.resize(vertices->size());
        visual.vertices.reserve(vertices->size());
        Vector2 *uvs = visual.uvs.ptrw();
        for (uint32_t i = 0; i < vertices->size(); ++i) {
            const uint32_t vi = vertices->Get(i);
            const float u = uv->Get(2 * i), v = uv->Get(2 * i + 1);
            if (vi >= vertex_count || !std::isfinite(u) || !std::isfinite(v))
                return fail("triangle/UV value");
            visual.vertices.push_back(int(vi)); uvs[i] = Vector2(u, v);
        }
        const rigExec::fb::PresentationMaterial *material = m->material();
        if (!material || !material->model() || material->model()->str() != "UsdPreviewSurface" ||
            material->wrapS() != rigExec::fb::PresentationWrap::Repeat ||
            material->wrapT() != rigExec::fb::PresentationWrap::Clamp)
            return fail("unsupported material model/wrap");
        const auto *png_bytes = material->texturePng();
        if (!png_bytes || png_bytes->size() == 0) return fail("embedded PNG");
        PackedByteArray png;
        png.resize(png_bytes->size());
        std::memcpy(png.ptrw(), png_bytes->data(), png_bytes->size());
        Ref<Image> texture; texture.instantiate();
        if (texture->load_png_from_buffer(png) != OK) return fail("embedded PNG");
        texture->generate_mipmaps();
        Ref<Shader> shader; shader.instantiate();
        shader->set_code(R"(shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;
uniform sampler2D ball_texture : source_color, filter_linear_mipmap, repeat_enable;
uniform vec3 diffuse_scale;
uniform vec3 emission_scale;
uniform float roughness;
uniform float metallic;
uniform float specular;
void fragment() {
    // Keep U continuous for correct mip derivatives and filtering across the
    // wrap seam. Clamp T to texel centres so it never blends opposite poles.
    float half_texel = 0.5 / float(textureSize(ball_texture, 0).y);
    vec3 texel = texture(ball_texture, vec2(UV.x, clamp(UV.y, half_texel, 1.0 - half_texel))).rgb;
    ALBEDO = texel * diffuse_scale;
    EMISSION = texel * emission_scale;
    ROUGHNESS = roughness; METALLIC = metallic; SPECULAR = specular;
})");
        visual.material.instantiate(); visual.material->set_shader(shader);
        visual.material->set_shader_parameter("ball_texture", ImageTexture::create_from_image(texture));
        const rigExec::fb::PresentationColor *colors[] = {material->diffuseScale(), material->emissionScale()};
        const char *color_keys[] = {"diffuse_scale", "emission_scale"};
        for (int k = 0; k < 2; ++k) {
            const rigExec::fb::PresentationColor *c = colors[k];
            if (!c) return fail("material color");
            if (!std::isfinite(c->r()) || !std::isfinite(c->g()) || !std::isfinite(c->b()))
                return fail("material color value");
            visual.material->set_shader_parameter(color_keys[k], Vector3(c->r(), c->g(), c->b()));
        }
        const float scalars[] = {material->roughness(), material->metallic(), material->specular()};
        const char *scalar_keys[] = {"roughness", "metallic", "specular"};
        for (int k = 0; k < 3; ++k) {
            const double v = scalars[k];
            if (!std::isfinite(v) || v < 0 || v > 1) return fail("material scalar");
            visual.material->set_shader_parameter(scalar_keys[k], v);
        }
        visual.mesh.instantiate(); visual.node = memnew(MeshInstance3D);
        visual.node->set_name("RigExecGeometry" + String::num_int64(_visuals.size()));
        visual.node->set_mesh(visual.mesh); visual.node->set_material_override(visual.material);
        add_child(visual.node, false, Node::INTERNAL_MODE_BACK);
        _visuals.push_back(std::move(visual));
    }
    _source_info = Dictionary();
    const flatbuffers::String *source = asset->sourceJson();
    if (source && source->size() > 0) {
        Ref<JSON> parser; parser.instantiate();
        if (parser->parse(String::utf8(source->c_str(), int(source->size()))) != OK ||
            parser->get_data().get_type() != Variant::DICTIONARY) return fail("source description");
        _source_info = parser->get_data();
    }
    _has_presentation = true;
    return true;
}

bool RigExecPlayer::_update_presentation() {
    const auto &outputs = _reader->GetPoints();
    for (auto &v : _visuals) {
        // The output list is fixed per file; the cached slot is found again
        // only if its path ever differs.
        if (v.points_output < 0 || size_t(v.points_output) >= outputs.size() ||
            outputs[size_t(v.points_output)].path != v.points_path) {
            const auto found = std::find_if(outputs.begin(), outputs.end(), [&](const auto &p) { return p.path == v.points_path; });
            v.points_output = found == outputs.end() ? -1 : int(found - outputs.begin());
        }
        if (v.points_output < 0 || outputs[size_t(v.points_output)].points.size() != size_t(v.source_count)) {
            _last_error = "Rig output point binding/count mismatch: " + String(v.points_path.c_str()); return false;
        }
        const auto &output = outputs[size_t(v.points_output)];
        std::vector<Vector3> positions(v.stencils.size()), normals(v.stencils.size());
        auto apply = [&](const std::vector<Coefficient> &weights) {
            Vector3 result;
            for (const auto &w : weights) {
                const auto &p = output.points[w.index];
                result += Vector3(p[0], p[1], p[2]) * w.weight;
            }
            return result;
        };
        for (size_t i = 0; i < v.stencils.size(); ++i) {
            const auto &s = v.stencils[i]; positions[i] = apply(s.p);
            normals[i] = apply(s.u).cross(apply(s.v)).normalized();
        }
        PackedVector3Array points, ns;
        points.resize(v.vertices.size()); ns.resize(v.vertices.size());
        Vector3 *p = points.ptrw(), *n = ns.ptrw();
        for (size_t i = 0; i < v.vertices.size(); ++i) { p[i] = positions[v.vertices[i]]; n[i] = normals[v.vertices[i]]; }
        Array arrays; arrays.resize(Mesh::ARRAY_MAX);
        arrays[Mesh::ARRAY_VERTEX] = points; arrays[Mesh::ARRAY_NORMAL] = ns; arrays[Mesh::ARRAY_TEX_UV] = v.uvs;
        v.mesh->clear_surfaces(); v.mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
    }
    return true;
}
} // namespace rigexec
