// RigExecCharacter: a Resource wrapping .rigexec bytes plus the binding the
// player needs (joint paths in publication order). The editor import plugin
// builds it from a .rigexec file; at runtime it is just bytes + metadata.

#ifndef RIGEXEC_CHARACTER_H
#define RIGEXEC_CHARACTER_H

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace rigexec {

class RigExecCharacter : public godot::Resource {
    GDCLASS(RigExecCharacter, godot::Resource);

protected:
    static void _bind_methods();

public:
    RigExecCharacter() = default;
    ~RigExecCharacter() override = default;

    // The whole .rigexec file. Set by the importer; replacing it drops the
    // cached binding until the next successful bind.
    void set_data(const godot::PackedByteArray &data);
    godot::PackedByteArray get_data() const;

    // Joint paths in the binary's publication order, filled by bind().
    godot::PackedStringArray get_joint_paths() const;
    godot::PackedFloat64Array get_frame_times() const;

    // Opens the bytes with the runtime and caches the binding. False with
    // the reason in get_bind_error() when the file is malformed.
    bool bind();
    godot::String get_bind_error() const;
    bool is_bound() const;

private:
    godot::PackedByteArray _data;
    godot::PackedStringArray _joint_paths;
    godot::PackedFloat64Array _frame_times;
    godot::String _bind_error;
    bool _bound = false;
};

} // namespace rigexec

#endif // RIGEXEC_CHARACTER_H
