#include "rigexec_player.h"

#include "rigexec_character.h"
#include "rigExecRuntime/runtime.h"

#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/variant/transform3d.hpp>

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
    godot::ClassDB::bind_method(godot::D_METHOD("set_frame", "frame"),
                                &RigExecPlayer::set_frame);
    godot::ClassDB::bind_method(godot::D_METHOD("get_frame"),
                                &RigExecPlayer::get_frame);
    godot::ClassDB::bind_method(godot::D_METHOD("set_autoplay", "autoplay"),
                                &RigExecPlayer::set_autoplay);
    godot::ClassDB::bind_method(godot::D_METHOD("get_autoplay"),
                                &RigExecPlayer::get_autoplay);
    godot::ClassDB::bind_method(godot::D_METHOD("play"), &RigExecPlayer::play);
    godot::ClassDB::bind_method(godot::D_METHOD("stop"), &RigExecPlayer::stop);
    godot::ClassDB::bind_method(godot::D_METHOD("is_playing"),
                                &RigExecPlayer::is_playing);
    godot::ClassDB::bind_method(godot::D_METHOD("evaluate"),
                                &RigExecPlayer::evaluate);
    godot::ClassDB::bind_method(godot::D_METHOD("set_control", "name", "value"), &RigExecPlayer::set_control);
    godot::ClassDB::bind_method(godot::D_METHOD("get_controls"), &RigExecPlayer::get_controls);
    godot::ClassDB::bind_method(godot::D_METHOD("has_presentation"), &RigExecPlayer::has_presentation);
    godot::ClassDB::bind_method(godot::D_METHOD("get_source_info"), &RigExecPlayer::get_source_info);
    godot::ClassDB::bind_method(godot::D_METHOD("set_avar", "property_path", "value"),
                                &RigExecPlayer::set_avar);
    godot::ClassDB::bind_method(godot::D_METHOD("clear_avars"),
                                &RigExecPlayer::clear_avars);
    godot::ClassDB::bind_method(godot::D_METHOD("reset_controls"), &RigExecPlayer::clear_avars);
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
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::FLOAT, "frame"),
                 "set_frame", "get_frame");
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::BOOL, "autoplay"),
                 "set_autoplay", "get_autoplay");
}

void RigExecPlayer::set_character(const godot::Ref<RigExecCharacter> &character) {
    _clear_presentation();
    _asset_valid = true;
    _character = character;
    _reader.reset();
    _evaluated = false;
    _frame = 0.0;
    if (_character.is_valid() && _ensure_reader(&_last_error)) {
        if (!_load_presentation()) { _asset_valid = false; return; }
        const auto frames = _reader->GetFrameTimes();
        if (_has_presentation && !frames.empty()) {
            _frame = frames.front();
            evaluate();
        }
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

void RigExecPlayer::set_frame(double frame) {
    _frame = frame;
}

double RigExecPlayer::get_frame() const {
    return _frame;
}

void RigExecPlayer::set_autoplay(bool autoplay) {
    _autoplay = autoplay;
}

bool RigExecPlayer::get_autoplay() const {
    return _autoplay;
}

void RigExecPlayer::play() {
    _playing = true;
}

void RigExecPlayer::stop() {
    _playing = false;
}

bool RigExecPlayer::is_playing() const {
    return _playing;
}

bool RigExecPlayer::_ensure_reader(godot::String *error) {
    if (!_asset_valid) { *error = _last_error; return false; }
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
    const godot::PackedByteArray data = _character->get_data();
    std::string why;
    _reader.reset(rigExec::RigExecRuntimeReader::Open(data.ptr(),
                                                      data.size(), &why)
                      .release());
    if (!_reader) {
        *error = godot::String(why.c_str());
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
    if (!_reader->SetFrame(_frame, &why) || !_reader->Execute(&why)) {
        _last_error = godot::String(why.c_str());
        return false;
    }
    _evaluated = true;
    if (_has_presentation && !_update_presentation()) return false;
    _last_error = "";
    return true;
}

bool RigExecPlayer::set_avar(const godot::String &path, double value) {
    if (_has_presentation) {
        _last_error = "This asset exposes named controllers; use set_control(), not internal avar paths";
        return false;
    }
    if (!_ensure_reader(&_last_error)) return false;
    std::string why;
    if (!_reader->SetAvar(path.utf8().get_data(), value, &why)) {
        _last_error = godot::String(why.c_str());
        return false;
    }
    _last_error = "";
    return true;
}

void RigExecPlayer::clear_avars() {
    if (_reader) _reader->ClearAvars();
    _control_values.clear();
    const godot::Array names = _controls.keys();
    for (int i = 0; i < names.size(); ++i) {
        const godot::Dictionary entry = _controls[names[i]];
        _control_values[names[i]] = entry["default"];
    }
}

godot::String RigExecPlayer::get_last_error() const {
    return _last_error;
}

bool RigExecPlayer::apply_to_skeleton() {
    if (!_evaluated || !_reader) {
        _last_error = "no evaluated frame";
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

void RigExecPlayer::_advance(double delta) {
    if (_character.is_null() || !_character->is_bound()) {
        return;
    }
    const godot::PackedFloat64Array frames = _character->get_frame_times();
    if (frames.is_empty()) {
        return;
    }
    // Step to the next baked frame at 24fps; time is frame units.
    _carry += delta;
    const double step = 1.0 / 24.0;
    while (_carry >= step) {
        _carry -= step;
        // Next baked frame past the current one, wrapping to the first
        // at the end so autoplay loops instead of sticking.
        int next = 0;
        for (int i = 0; i < frames.size(); ++i) {
            if (frames[i] > _frame) {
                next = i;
                break;
            }
        }
        _frame = frames[next];
        if (evaluate()) {
            if (!_has_presentation && !_skeleton_path.is_empty()) apply_to_skeleton();
        }
    }
}

void RigExecPlayer::_process(double delta) {
    if (_autoplay && !_playing) {
        _playing = true;
    }
    if (_playing) {
        _advance(delta);
    }
}

} // namespace rigexec
