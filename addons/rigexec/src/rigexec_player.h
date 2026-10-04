// RigExecPlayer: a self-contained rig asset, with public controls and
// embedded rendering driven by zero-USD runtime geometry outputs.
// The optional Skeleton3D adapter remains for legacy program-only files.
//
// Transform mapping (PLAN.md section 5): the runtime publishes row-major
// 16-float asset-space matrices; each becomes a Godot Transform3D
// (Basis columns = the matrix rows' XYZ, origin = the translation row).
// Bones bind by leaf name: the joint path's last element must equal the
// bone name. Coordinates remain in authored asset units; the caller is
// responsible for choosing a world-unit scale. No handedness conversion.

#ifndef RIGEXEC_PLAYER_H
#define RIGEXEC_PLAYER_H

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/core/binder_common.hpp>

#include <memory>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/variant/dictionary.hpp>
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

    // The selected frame, in the binary's frame units. evaluate reports
    // an error when the file carries no such frame.
    void set_frame(double frame);
    double get_frame() const;

    void set_autoplay(bool autoplay);
    bool get_autoplay() const;

    void play();
    void stop();
    bool is_playing() const;

    // Replays the selected frame through the runtime. False with the
    // reason in get_last_error() when the binary refuses the frame.
    bool evaluate();
    bool set_control(const godot::String &name, double value);
    godot::Dictionary get_controls() const;
    bool has_presentation() const { return _has_presentation; }
    godot::Dictionary get_source_info() const { return _source_info.duplicate(true); }
    bool set_avar(const godot::String &property_path, double value);
    void clear_avars();
    godot::String get_last_error() const;

    // Writes the last evaluated joint matrices into the skeleton bound
    // by set_skeleton_path. False when no skeleton or no evaluated frame.
    bool apply_to_skeleton();

    // Skinning deltas (rest -> posed), preserved for parity consumers.
    // Use pose transforms for bones and rest transforms to build a skeleton.
    godot::Array get_joint_transforms() const;
    godot::Array get_joint_pose_transforms() const;
    godot::Array get_joint_rest_transforms() const;

    // Godot's per-frame callback: advances and evaluates when playing.
    void _process(double delta) override;

private:
    bool _ensure_reader(godot::String *error);
    void _advance(double delta);
    bool _load_presentation();
    bool _update_presentation();
    void _clear_presentation();

    struct Coefficient { int index; float weight; };
    struct Stencil { std::vector<Coefficient> p, u, v; };
    struct Visual {
        std::string points_path;
        int source_count = 0;
        std::vector<Stencil> stencils;
        std::vector<int> vertices;
        godot::PackedVector2Array uvs;
        godot::Ref<godot::ArrayMesh> mesh;
        godot::Ref<godot::ShaderMaterial> material;
        godot::MeshInstance3D *node = nullptr;
    };
    std::vector<Visual> _visuals;
    godot::Dictionary _controls, _control_values, _source_info;
    bool _has_presentation = false;
    bool _asset_valid = true;

    godot::Ref<RigExecCharacter> _character;
    godot::NodePath _skeleton_path;
    std::shared_ptr<rigExec::RigExecRuntimeReader> _reader;
    double _frame = 0.0;
    double _carry = 0.0;
    bool _autoplay = false;
    bool _playing = false;
    bool _evaluated = false;
    godot::String _last_error;
};

} // namespace rigexec

#endif // RIGEXEC_PLAYER_H
