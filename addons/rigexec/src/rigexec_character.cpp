#include "rigexec_character.h"

#include "rigExecRuntime/runtime.h"

namespace rigexec {

void RigExecCharacter::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("set_data", "data"),
                                &RigExecCharacter::set_data);
    godot::ClassDB::bind_method(godot::D_METHOD("get_data"),
                                &RigExecCharacter::get_data);
    godot::ClassDB::bind_method(godot::D_METHOD("get_joint_paths"),
                                &RigExecCharacter::get_joint_paths);
    godot::ClassDB::bind_method(godot::D_METHOD("get_frame_times"),
                                &RigExecCharacter::get_frame_times);
    godot::ClassDB::bind_method(godot::D_METHOD("bind"),
                                &RigExecCharacter::bind);
    godot::ClassDB::bind_method(godot::D_METHOD("get_bind_error"),
                                &RigExecCharacter::get_bind_error);
    godot::ClassDB::bind_method(godot::D_METHOD("is_bound"),
                                &RigExecCharacter::is_bound);
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::PACKED_BYTE_ARRAY,
                                     "data"),
                 "set_data", "get_data");
}

void RigExecCharacter::set_data(const godot::PackedByteArray &data) {
    _data = data;
    _bound = false;
    _bind_error = "";
    _joint_paths.clear();
    _frame_times.clear();
}

godot::PackedByteArray RigExecCharacter::get_data() const {
    return _data;
}

godot::PackedStringArray RigExecCharacter::get_joint_paths() const {
    return _joint_paths;
}

godot::PackedFloat64Array RigExecCharacter::get_frame_times() const {
    return _frame_times;
}

bool RigExecCharacter::bind() {
    _joint_paths.clear();
    _frame_times.clear();
    _bound = false;
    if (_data.is_empty()) {
        _bind_error = "no .rigexec data";
        return false;
    }
    std::string error;
    std::unique_ptr<rigExec::RigExecRuntimeReader> reader =
        rigExec::RigExecRuntimeReader::Open(_data.ptr(), _data.size(),
                                            &error);
    if (!reader) {
        _bind_error = godot::String(error.c_str());
        return false;
    }
    // Joint paths need an evaluated frame; the binding pass runs the
    // first baked frame and keeps the paths alone.
    const std::vector<double> frames = reader->GetFrameTimes();
    for (double frame : frames) {
        _frame_times.push_back(frame);
    }
    if (!frames.empty() && reader->SetFrame(frames[0], &error) &&
        reader->Execute(&error)) {
        for (const rigExec::RigExecRuntimeJointMatrix &joint :
             reader->GetJointMatrices()) {
            _joint_paths.push_back(godot::String(joint.path.c_str()));
        }
    }
    // A file that opens but does not evaluate yet still binds: the player
    // reports the per-frame reason when it evaluates for real.
    _bound = true;
    _bind_error = "";
    return true;
}

godot::String RigExecCharacter::get_bind_error() const {
    return _bind_error;
}

bool RigExecCharacter::is_bound() const {
    return _bound;
}

} // namespace rigexec
