// RigExecPlayer: a self-contained rig asset, with public controls and
// embedded rendering driven by zero-USD runtime geometry outputs.
// The optional Skeleton3D adapter remains for program-only files.
// Nothing advances on its own: the caller sets inputs (set_control on
// presentation assets, set_input on program-only files), then evaluate()
// executes the rig over them.
//
// Transform mapping: the runtime publishes row-major 16-float asset-space
// matrices; each becomes a Godot Transform3D (Basis columns = the matrix
// rows' XYZ, origin = the translation row).
// set_input maps a Transform3D to a Matrix4d input the inverse way: row i
// is basis column i with w 0, row 3 the origin with w 1.
// Bones bind by leaf name: the joint path's last element must equal the
// bone name. Coordinates remain in authored asset units; the caller is
// responsible for choosing a world-unit scale. No handedness conversion.

#ifndef RIGEXEC_PLAYER_H
#define RIGEXEC_PLAYER_H

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/core/binder_common.hpp>

#include <cstddef>
#include <memory>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <string>
#include <vector>

namespace rigExec {
class RigExecRuntimeReader;
}

namespace rigexec {

class RigExecCharacter;

class RigExecPlayer : public godot::Node3D {
    GDCLASS(RigExecPlayer, godot::Node3D);

protected:
    static void _bind_methods();
    bool _set(const godot::StringName &name, const godot::Variant &value);
    bool _get(const godot::StringName &name, godot::Variant &value) const;
    void _get_property_list(godot::List<godot::PropertyInfo> *list) const;
    void _validate_property(godot::PropertyInfo &property) const;

public:
    RigExecPlayer() = default;
    ~RigExecPlayer() override = default;

    void set_character(const godot::Ref<RigExecCharacter> &character);
    godot::Ref<RigExecCharacter> get_character() const;

    void set_skeleton_path(const godot::NodePath &path);
    godot::NodePath get_skeleton_path() const;

    // Executes the rig over the current inputs. False with the reason in
    // get_last_error() when a step fails; the outputs keep their values.
    bool evaluate();
    // Presentation assets: the public controls, each bound to one input.
    bool set_control(const godot::String &name, double value);
    godot::Dictionary get_controls() const;
    bool has_presentation() const { return _has_presentation; }
    godot::Dictionary get_source_info() const { return _source_info.duplicate(true); }
    // Program-only files: the file's inputs by attribute path. Refused on
    // presentation assets, whose game interface is the controls.
    bool set_input(const godot::String &name, const godot::Variant &value);
    godot::Dictionary get_inputs() const;
    // Every input (and so every control) returns to its bake-time default.
    void reset_inputs();
    // Call when the timeline moves, as a stage sampler does: the next
    // evaluate recomputes what a change of time recomputes.
    void touch_animated_inputs();
    godot::String get_last_error() const;

    // Writes the last evaluated joint matrices into the skeleton bound
    // by set_skeleton_path. False when no skeleton or no evaluation.
    bool apply_to_skeleton();

    // Skinning deltas (rest -> posed), preserved for parity consumers.
    // Use pose transforms for bones and rest transforms to build a skeleton.
    godot::Array get_joint_transforms() const;
    godot::Array get_joint_pose_transforms() const;
    godot::Array get_joint_rest_transforms() const;

private:
    bool _ensure_reader(godot::String *error);
    bool _load_presentation();
    bool _update_presentation();
    void _clear_presentation();

    struct Coefficient { int index; float weight; };
    struct Stencil { std::vector<Coefficient> p, u, v; };
    struct Visual {
        std::string points_path;
        // The cached index into the runtime's GetPoints(); -1 until found.
        int points_output = -1;
        int source_count = 0;
        std::vector<Stencil> stencils;
        std::vector<int> vertices;
        godot::PackedVector2Array uvs;
        godot::Ref<godot::ArrayMesh> mesh;
        godot::Ref<godot::ShaderMaterial> material;
        godot::MeshInstance3D *node = nullptr;
    };
    std::vector<Visual> _visuals;
    struct Control {
        godot::String name;
        size_t input = 0;
        const char *unit = "";
    };
    std::vector<Control> _controls;
    // Control name -> index into _controls.
    godot::Dictionary _control_slots;
    godot::Dictionary _source_info;
    bool _has_presentation = false;
    // False once the file's presentation is rejected; _asset_error says why.
    bool _asset_valid = true;
    godot::String _asset_error;

    godot::Ref<RigExecCharacter> _character;
    godot::NodePath _skeleton_path;
    // The bytes the reader opened; the presentation is read from these.
    godot::PackedByteArray _data;
    std::shared_ptr<rigExec::RigExecRuntimeReader> _reader;
    bool _evaluated = false;
    godot::String _last_error;
};

} // namespace rigexec

#endif // RIGEXEC_PLAYER_H
