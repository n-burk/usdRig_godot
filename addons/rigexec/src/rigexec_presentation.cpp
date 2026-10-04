#include "rigexec_player.h"
#include "rigexec_character.h"
#include "rigExecRuntime/runtime.h"
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/marshalls.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <cmath>
#include <algorithm>

namespace rigexec {
using namespace godot;

void RigExecPlayer::_clear_presentation() {
    for (auto &v : _visuals) {
        if (v.node) { remove_child(v.node); memdelete(v.node); }
    }
    _visuals.clear();
    _controls.clear(); _control_values.clear(); _source_info.clear();
    _has_presentation = false;
}

bool RigExecPlayer::set_control(const String &name, double value) {
    if (!_controls.has(name) || !std::isfinite(value)) {
        _last_error = "Expected a finite value for an exposed controller: " + name;
        return false;
    }
    if (!_ensure_reader(&_last_error)) return false;
    const Dictionary entry = _controls[name];
    const String path = entry["path"];
    std::string why;
    if (!_reader->SetAvar(path.utf8().get_data(), value, &why)) {
        _last_error = String(why.c_str()); return false;
    }
    _control_values[name] = value;
    _last_error = "";
    return true;
}

Dictionary RigExecPlayer::get_controls() const {
    Dictionary result;
    for (const auto &key : _controls.keys()) {
        Dictionary metadata = Dictionary(_controls[key]).duplicate(true);
        metadata.erase("path"); // Internal USD wiring is not the game interface.
        metadata["value"] = _control_values[key];
        result[key] = metadata;
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
    if (!n.begins_with("controls/") || !_control_values.has(key)) return false;
    value = _control_values[key]; return true;
}

void RigExecPlayer::_get_property_list(List<PropertyInfo> *list) const {
    for (const auto &key : _controls.keys())
        list->push_back(PropertyInfo(Variant::FLOAT, "controls/" + String(key).replace(".", "/")));
}

void RigExecPlayer::_validate_property(PropertyInfo &p) const {
    if (_has_presentation && (String(p.name) == "skeleton_path" || String(p.name) == "frame" || String(p.name) == "autoplay"))
        p.usage = PROPERTY_USAGE_STORAGE;
}

bool RigExecPlayer::_load_presentation() {
    auto fail = [&](const String &reason) {
        _clear_presentation(); _last_error = "Invalid embedded presentation: " + reason; return false;
    };
    const PackedByteArray bytes = _character->get_data();
    std::string why;
    auto binary = rigExec::RigExecBinaryReader::Open(bytes.ptr(), bytes.size(), &why);
    if (!binary) return fail(String(why.c_str()));
    const uint8_t *data = nullptr; size_t size = 0;
    if (!binary->FindSection(rigExec::RigExecBinarySection::Presentation, &data, &size)) return true;
    Ref<JSON> parser; parser.instantiate();
    if (parser->parse(String::utf8(reinterpret_cast<const char *>(data), size)) != OK ||
        parser->get_data().get_type() != Variant::DICTIONARY) return fail("JSON payload");
    const Dictionary asset = parser->get_data();
    if (int(asset.get("version", 0)) != 1 ||
        asset.get("controls", Variant()).get_type() != Variant::ARRAY ||
        asset.get("meshes", Variant()).get_type() != Variant::ARRAY) return fail("version or tables");
    const Array controls = asset["controls"];
    for (const Variant &item : controls) {
        if (item.get_type() != Variant::DICTIONARY) return fail("controller record");
        const Dictionary c = item;
        const String name = c.get("name", ""), path = c.get("path", "");
        if (name.is_empty() || name.contains("/") || _controls.has(name) || !c.has("default"))
            return fail("controller name/default");
        if (!_reader->SetAvar(path.utf8().get_data(), double(c["default"]), &why)) return fail(String(why.c_str()));
        _controls[name] = c; _control_values[name] = c["default"];
    }
    _reader->ClearAvars();
    const Array meshes = asset["meshes"];
    if (meshes.is_empty()) return fail("no render meshes");
    for (const Variant &item : meshes) {
        if (item.get_type() != Variant::DICTIONARY) return fail("mesh record");
        const Dictionary m = item;
        if (m.get("stencils", Variant()).get_type() != Variant::ARRAY ||
            m.get("vertex_indices", Variant()).get_type() != Variant::ARRAY ||
            m.get("uvs", Variant()).get_type() != Variant::ARRAY ||
            m.get("material", Variant()).get_type() != Variant::DICTIONARY) return fail("mesh tables");
        Visual visual;
        visual.points_path = String(m.get("points_path", "")).utf8().get_data();
        visual.source_count = m.get("source_point_count", 0);
        if (visual.source_count <= 0 || visual.points_path.empty()) return fail("point binding");
        const Array stencils = m["stencils"];
        for (const Variant &record : stencils) {
            if (record.get_type() != Variant::ARRAY || Array(record).size() != 3) return fail("stencil record");
            const Array groups = record; Stencil s;
            std::vector<Coefficient> *dest[] = {&s.p, &s.u, &s.v};
            for (int group = 0; group < 3; ++group) {
                if (groups[group].get_type() != Variant::ARRAY) return fail("stencil group");
                const Array weights = groups[group];
                if (weights.is_empty() || weights.size() % 2) return fail("stencil length");
                for (int k = 0; k < weights.size(); k += 2) {
                    double index = weights[k], w = weights[k+1];
                    if (!std::isfinite(index) || index != std::floor(index) || index < 0 || index >= visual.source_count || !std::isfinite(w))
                        return fail("stencil coefficient");
                    dest[group]->push_back({int(index), float(w)});
                }
            }
            visual.stencils.push_back(std::move(s));
        }
        const Array indices = m["vertex_indices"], uv = m["uvs"];
        if (indices.is_empty() || indices.size() % 3 || uv.size() != 2*indices.size()) return fail("triangle/UV count");
        visual.uvs.resize(indices.size());
        for (int i = 0; i < indices.size(); ++i) {
            const double vi = indices[i], u = uv[2*i], v = uv[2*i+1];
            if (!std::isfinite(vi) || vi != std::floor(vi) || vi < 0 || vi >= visual.stencils.size() || !std::isfinite(u) || !std::isfinite(v))
                return fail("triangle/UV value");
            visual.vertices.push_back(int(vi)); visual.uvs.set(i, Vector2(u, v));
        }
        const Dictionary material = m["material"];
        if (String(material.get("model", "")) != "UsdPreviewSurface" ||
            String(material.get("wrap_s", "")) != "repeat" || String(material.get("wrap_t", "")) != "clamp")
            return fail("unsupported material model/wrap");
        Ref<Image> texture; texture.instantiate();
        PackedByteArray png = Marshalls::get_singleton()->base64_to_raw(material.get("texture_png", ""));
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
        for (const char *key : {"diffuse_scale", "emission_scale"}) {
            if (material.get(key, Variant()).get_type() != Variant::ARRAY || Array(material[key]).size() != 3) return fail("material color");
            const Array c = material[key];
            for (const Variant &v : c) if (!std::isfinite(double(v))) return fail("material color value");
            visual.material->set_shader_parameter(key, Vector3(c[0], c[1], c[2]));
        }
        for (const char *key : {"roughness", "metallic", "specular"}) {
            const double v = material.get(key, -1);
            if (!std::isfinite(v) || v < 0 || v > 1) return fail("material scalar");
            visual.material->set_shader_parameter(key, v);
        }
        visual.mesh.instantiate(); visual.node = memnew(MeshInstance3D);
        visual.node->set_name("RigExecGeometry" + String::num_int64(_visuals.size()));
        visual.node->set_mesh(visual.mesh); visual.node->set_material_override(visual.material);
        add_child(visual.node, false, Node::INTERNAL_MODE_BACK);
        _visuals.push_back(std::move(visual));
    }
    _source_info = asset.get("source", Dictionary());
    _has_presentation = true;
    return true;
}

bool RigExecPlayer::_update_presentation() {
    const auto &outputs = _reader->GetPoints();
    for (auto &v : _visuals) {
        const auto output = std::find_if(outputs.begin(), outputs.end(), [&](const auto &p) { return p.path == v.points_path; });
        if (output == outputs.end() || output->points.size() != size_t(v.source_count)) {
            _last_error = "Rig output point binding/count mismatch: " + String(v.points_path.c_str()); return false;
        }
        std::vector<Vector3> positions(v.stencils.size()), normals(v.stencils.size());
        auto apply = [&](const std::vector<Coefficient> &weights) {
            Vector3 result;
            for (const auto &w : weights) {
                const auto &p = output->points[w.index];
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
