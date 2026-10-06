#include "rigexec_player.h"

#include "rigexec_character.h"
#include "rigExecRuntime/runtime.h"

#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstdint>

namespace rigexec {

namespace {

// Row-major 16-float asset-space matrix -> Godot Transform3D. Basis
// columns are the matrix rows' XYZ halves; the origin is row 3's.
godot::Transform3D ToGodot(const rigExec::RrMat4d &m) {
    return godot::Transform3D(
        godot::Basis(godot::Vector3(m[0][0], m[0][1], m[0][2]),
                     godot::Vector3(m[1][0], m[1][1], m[1][2]),
                     godot::Vector3(m[2][0], m[2][1], m[2][2])),
        godot::Vector3(m[3][0], m[3][1], m[3][2]));
}

godot::String LeafName(const godot::String &path) {
    const int slash = path.rfind("/");
    return slash >= 0 ? path.substr(slash + 1) : path;
}

const char *TypeName(rigExec::RrInputTag tag) {
    switch (tag) {
    case rigExec::RrInputTag::Double: return "double";
    case rigExec::RrInputTag::Float: return "float";
    case rigExec::RrInputTag::Bool: return "bool";
    case rigExec::RrInputTag::Int: return "int";
    case rigExec::RrInputTag::Matrix4d: return "matrix4d";
    case rigExec::RrInputTag::Token: return "token";
    case rigExec::RrInputTag::Vec3d: return "vec3d";
    case rigExec::RrInputTag::Vec3f: return "vec3f";
    }
    return "unknown";
}

// Vectors and matrices come back as PackedFloat64Array (matrices
// row-major), tokens as their text.
godot::Variant InputToVariant(const rigExec::RigExecRuntimeReader &reader,
                              const rigExec::RrInputValue &v) {
    godot::PackedFloat64Array values;
    switch (v.tag) {
    case rigExec::RrInputTag::Double: return v.f64;
    case rigExec::RrInputTag::Float: return double(v.f32);
    case rigExec::RrInputTag::Bool: return v.boolean;
    case rigExec::RrInputTag::Int: return int64_t(v.i32);
    case rigExec::RrInputTag::Token:
        return godot::String::utf8(reader.GetTokenText(v.token).c_str());
    case rigExec::RrInputTag::Vec3d:
        for (size_t i = 0; i < 3; ++i) values.push_back(v.vec[i]);
        return values;
    case rigExec::RrInputTag::Vec3f:
        for (size_t i = 0; i < 3; ++i) values.push_back(double(v.vec3f[i]));
        return values;
    case rigExec::RrInputTag::Matrix4d:
        for (size_t r = 0; r < 4; ++r)
            for (size_t c = 0; c < 4; ++c) values.push_back(v.matrix[r][c]);
        return values;
    }
    return godot::Variant();
}

// A non-token input value from a Variant: FLOAT or INT for Double and
// Float (a Double sets a Float input through static_cast<float>), INT
// within int32 for Int, BOOL for Bool, VECTOR3 or 3 doubles for Vec3d and
// Vec3f, TRANSFORM3D or 16 row-major doubles for Matrix4d. The runtime
// checks finiteness.
bool InputFromVariant(const godot::Variant &value, rigExec::RrInputTag type,
                      rigExec::RrInputValue *out, godot::String *reason) {
    const godot::Variant::Type vt = value.get_type();
    *reason = "unsupported value type";
    switch (type) {
    case rigExec::RrInputTag::Double:
    case rigExec::RrInputTag::Float:
        if (vt != godot::Variant::FLOAT && vt != godot::Variant::INT) return false;
        out->tag = rigExec::RrInputTag::Double;
        out->f64 = double(value);
        return true;
    case rigExec::RrInputTag::Bool:
        if (vt != godot::Variant::BOOL) return false;
        out->tag = rigExec::RrInputTag::Bool;
        out->boolean = bool(value);
        return true;
    case rigExec::RrInputTag::Int: {
        if (vt != godot::Variant::INT) return false;
        const int64_t i = value;
        if (i < int64_t(INT32_MIN) || i > int64_t(INT32_MAX)) {
            *reason = "value out of int32 range";
            return false;
        }
        out->tag = rigExec::RrInputTag::Int;
        out->i32 = int32_t(i);
        return true;
    }
    case rigExec::RrInputTag::Vec3d:
    case rigExec::RrInputTag::Vec3f: {
        double c[3];
        if (vt == godot::Variant::VECTOR3) {
            const godot::Vector3 v = value;
            c[0] = v.x; c[1] = v.y; c[2] = v.z;
        } else if (vt == godot::Variant::PACKED_FLOAT64_ARRAY) {
            const godot::PackedFloat64Array a = value;
            if (a.size() != 3) return false;
            for (int i = 0; i < 3; ++i) c[i] = a[i];
        } else {
            return false;
        }
        out->tag = type;
        if (type == rigExec::RrInputTag::Vec3d) {
            out->vec = rigExec::RrVec3d(c[0], c[1], c[2]);
        } else {
            out->vec3f = rigExec::RrVec3f(float(c[0]), float(c[1]), float(c[2]));
        }
        return true;
    }
    case rigExec::RrInputTag::Matrix4d:
        if (vt == godot::Variant::TRANSFORM3D) {
            const godot::Transform3D t = value;
            const godot::Vector3 x = t.basis.get_column(0);
            const godot::Vector3 y = t.basis.get_column(1);
            const godot::Vector3 z = t.basis.get_column(2);
            const godot::Vector3 &o = t.origin;
            out->matrix = rigExec::RrMat4d(x.x, x.y, x.z, 0.0, y.x, y.y, y.z, 0.0,
                                           z.x, z.y, z.z, 0.0, o.x, o.y, o.z, 1.0);
        } else if (vt == godot::Variant::PACKED_FLOAT64_ARRAY) {
            const godot::PackedFloat64Array a = value;
            if (a.size() != 16) return false;
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c) out->matrix[r][c] = a[r * 4 + c];
        } else {
            return false;
        }
        out->tag = rigExec::RrInputTag::Matrix4d;
        return true;
    case rigExec::RrInputTag::Token:
        return false;
    }
    return false;
}

} // namespace

void RigExecPlayer::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("set_character", "character"),
                                &RigExecPlayer::set_character);
    godot::ClassDB::bind_method(godot::D_METHOD("get_character"),
                                &RigExecPlayer::get_character);
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_skeleton_path", "path"),
        &RigExecPlayer::set_skeleton_path);
    godot::ClassDB::bind_method(godot::D_METHOD("get_skeleton_path"),
                                &RigExecPlayer::get_skeleton_path);
    godot::ClassDB::bind_method(godot::D_METHOD("evaluate"),
                                &RigExecPlayer::evaluate);
    godot::ClassDB::bind_method(godot::D_METHOD("set_control", "name", "value"), &RigExecPlayer::set_control);
    godot::ClassDB::bind_method(godot::D_METHOD("get_controls"), &RigExecPlayer::get_controls);
    godot::ClassDB::bind_method(godot::D_METHOD("has_presentation"), &RigExecPlayer::has_presentation);
    godot::ClassDB::bind_method(godot::D_METHOD("get_source_info"), &RigExecPlayer::get_source_info);
    godot::ClassDB::bind_method(godot::D_METHOD("set_input", "name", "value"),
                                &RigExecPlayer::set_input);
    godot::ClassDB::bind_method(godot::D_METHOD("get_inputs"),
                                &RigExecPlayer::get_inputs);
    godot::ClassDB::bind_method(godot::D_METHOD("reset_inputs"),
                                &RigExecPlayer::reset_inputs);
    godot::ClassDB::bind_method(godot::D_METHOD("reset_controls"),
                                &RigExecPlayer::reset_inputs);
    godot::ClassDB::bind_method(godot::D_METHOD("touch_animated_inputs"),
                                &RigExecPlayer::touch_animated_inputs);
    godot::ClassDB::bind_method(godot::D_METHOD("get_last_error"),
                                &RigExecPlayer::get_last_error);
    godot::ClassDB::bind_method(godot::D_METHOD("apply_to_skeleton"),
                                &RigExecPlayer::apply_to_skeleton);
    godot::ClassDB::bind_method(godot::D_METHOD("get_joint_transforms"),
                                &RigExecPlayer::get_joint_transforms);
    godot::ClassDB::bind_method(godot::D_METHOD("get_joint_pose_transforms"),
                                &RigExecPlayer::get_joint_pose_transforms);
    godot::ClassDB::bind_method(godot::D_METHOD("get_joint_rest_transforms"),
                                &RigExecPlayer::get_joint_rest_transforms);
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::OBJECT, "character",
                                     godot::PROPERTY_HINT_RESOURCE_TYPE,
                                     "RigExecCharacter"),
                 "set_character", "get_character");
    ADD_PROPERTY(
        godot::PropertyInfo(godot::Variant::NODE_PATH, "skeleton_path"),
        "set_skeleton_path", "get_skeleton_path");
}

void RigExecPlayer::set_character(const godot::Ref<RigExecCharacter> &character) {
    _clear_presentation();
    _asset_valid = true;
    _asset_error = "";
    _character = character;
    _reader.reset();
    _data = godot::PackedByteArray();
    _evaluated = false;
    if (_character.is_valid() && _ensure_reader(&_last_error)) {
        // A fresh reader holds the input defaults: evaluating them shows
        // the rig as baked, and makes the getters valid at once.
        evaluate();
    }
    notify_property_list_changed();
}

godot::Ref<RigExecCharacter> RigExecPlayer::get_character() const {
    return _character;
}

void RigExecPlayer::set_skeleton_path(const godot::NodePath &path) {
    _skeleton_path = path;
}

godot::NodePath RigExecPlayer::get_skeleton_path() const {
    return _skeleton_path;
}

bool RigExecPlayer::_ensure_reader(godot::String *error) {
    if (!_asset_valid) { *error = _asset_error; return false; }
    if (_reader) {
        return true;
    }
    if (_character.is_null()) {
        *error = "no character";
        return false;
    }
    if (!_character->is_bound() && !_character->bind()) {
        *error = _character->get_bind_error();
        return false;
    }
    _data = _character->get_data();
    std::string why;
    _reader.reset(rigExec::RigExecRuntimeReader::Open(_data.ptr(),
                                                      _data.size(), &why)
                      .release());
    if (!_reader) {
        _data = godot::PackedByteArray();
        *error = godot::String(why.c_str());
        return false;
    }
    // Godot installs no plugin-mover kernels; those movers pass their
    // points through.
    const std::vector<std::string> missing =
        _reader->GetMissingExternalKernels();
    if (!missing.empty()) {
        godot::String types;
        for (const std::string &type : missing) {
            if (!types.is_empty()) types += ", ";
            types += godot::String::utf8(type.c_str());
        }
        godot::UtilityFunctions::push_warning(
            "rigexec: no kernel for plugin mover type(s) " + types +
            "; their points pass through");
    }
    // A rejected presentation leaves no reader behind, so the asset's
    // inputs and outputs stay hidden too.
    if (!_load_presentation()) {
        _asset_valid = false;
        _asset_error = _last_error;
        _reader.reset();
        _data = godot::PackedByteArray();
        *error = _asset_error;
        return false;
    }
    return true;
}

bool RigExecPlayer::evaluate() {
    godot::String error;
    if (!_ensure_reader(&error)) {
        _last_error = error;
        return false;
    }
    std::string why;
    if (!_reader->Execute(&why)) {
        _last_error = godot::String(why.c_str());
        return false;
    }
    _evaluated = true;
    if (_has_presentation && !_update_presentation()) return false;
    _last_error = "";
    return true;
}

bool RigExecPlayer::set_input(const godot::String &name,
                              const godot::Variant &value) {
    if (_has_presentation) {
        _last_error = "This asset exposes named controllers; use set_control(), not internal input paths";
        return false;
    }
    if (!_ensure_reader(&_last_error)) return false;
    const std::string path = name.utf8().get_data();
    size_t index = 0;
    if (!_reader->FindInput(path, &index)) {
        _last_error = "no input named " + name;
        return false;
    }
    const rigExec::RrInputTag type = _reader->GetInputInfo(index).type;
    std::string why;
    bool ok = false;
    if (type == rigExec::RrInputTag::Token) {
        const godot::Variant::Type vt = value.get_type();
        if (vt != godot::Variant::STRING && vt != godot::Variant::STRING_NAME) {
            _last_error = "unsupported value type for token input " + name;
            return false;
        }
        ok = _reader->SetInputToken(
            path, godot::String(value).utf8().get_data(), &why);
    } else {
        rigExec::RrInputValue held{};
        godot::String reason;
        if (!InputFromVariant(value, type, &held, &reason)) {
            _last_error = reason + " for " + TypeName(type) + " input " + name;
            return false;
        }
        ok = _reader->SetInputAt(index, held, &why);
    }
    if (!ok) {
        _last_error = godot::String::utf8(why.c_str());
        return false;
    }
    _last_error = "";
    return true;
}

godot::Dictionary RigExecPlayer::get_inputs() const {
    godot::Dictionary result;
    // Presentation assets keep their internal wiring off the game interface.
    if (_has_presentation || !_reader) return result;
    for (size_t i = 0; i < _reader->GetInputCount(); ++i) {
        const rigExec::RigExecRuntimeInputInfo &info = _reader->GetInputInfo(i);
        godot::Dictionary entry;
        entry["type"] = TypeName(info.type);
        entry["animated"] = info.animated;
        entry["default"] = InputToVariant(*_reader, info.defaultValue);
        entry["value"] = InputToVariant(*_reader, _reader->GetInputValue(i));
        result[godot::String::utf8(info.name.c_str())] = entry;
    }
    return result;
}

void RigExecPlayer::reset_inputs() {
    if (_reader) _reader->ResetInputs();
}

void RigExecPlayer::touch_animated_inputs() {
    if (_reader) _reader->TouchAnimatedInputs();
}

godot::String RigExecPlayer::get_last_error() const {
    return _last_error;
}

bool RigExecPlayer::apply_to_skeleton() {
    if (!_evaluated || !_reader) {
        _last_error = "no evaluation";
        return false;
    }
    godot::Node *node = get_node_or_null(_skeleton_path);
    godot::Skeleton3D *skeleton =
        godot::Object::cast_to<godot::Skeleton3D>(node);
    if (skeleton == nullptr) {
        _last_error = "skeleton path names no Skeleton3D";
        return false;
    }
    // Bones bind by leaf name; joints the skeleton does not have are
    // skipped, so a partial skeleton still poses.
    for (const rigExec::RigExecRuntimeJointMatrix &joint :
         _reader->GetJointPoseMatrices()) {
        const int bone = skeleton->find_bone(
            LeafName(godot::String(joint.path.c_str())));
        if (bone >= 0) {
            skeleton->set_bone_global_pose(bone, ToGodot(joint.matrix));
        }
    }
    _last_error = "";
    return true;
}

godot::Array RigExecPlayer::get_joint_transforms() const {
    godot::Array out;
    if (!_evaluated || !_reader) {
        return out;
    }
    for (const rigExec::RigExecRuntimeJointMatrix &joint :
         _reader->GetJointMatrices()) {
        out.push_back(ToGodot(joint.matrix));
    }
    return out;
}

godot::Array RigExecPlayer::get_joint_pose_transforms() const {
    godot::Array out;
    if (_evaluated && _reader) {
        for (const auto &joint : _reader->GetJointPoseMatrices())
            out.push_back(ToGodot(joint.matrix));
    }
    return out;
}

godot::Array RigExecPlayer::get_joint_rest_transforms() const {
    godot::Array out;
    if (_reader) {
        for (const auto &joint : _reader->GetJointRestMatrices())
            out.push_back(ToGodot(joint.matrix));
    }
    return out;
}

} // namespace rigexec
