@tool
extends EditorImportPlugin

# Editor-only .rigexec importer: reads the binary, binds it through the
# runtime for validation, and saves a RigExecCharacter resource. Baking
# happens outside the editor (rigExecBake on a machine with USD).

func _get_importer_name() -> String:
	return "rigexec.character"


func _get_visible_name() -> String:
	return "RigExec Character"


func _get_recognized_extensions() -> PackedStringArray:
	return PackedStringArray(["rigexec"])


func _get_save_extension() -> String:
	return "res"


func _get_resource_type() -> String:
	return "RigExecCharacter"


# Version 3 reads .rigexec format 21 (usdRig v0.1.0; format 20 still
# opens). Bumping it re-imports resources cached from older bytes, so a
# file that needs a rebake fails at import with the runtime's reason
# rather than at play time.
func _get_format_version() -> int:
	return 3


func _get_preset_count() -> int:
	return 1


func _get_preset_name(_preset_index: int) -> String:
	return "Default"


func _get_import_options(_path: String, _preset_index: int) -> Array:
	return []


func _get_option_visibility(
	_path: String, _option_name: StringName, _options: Dictionary
) -> bool:
	return false


func _import(
	source_file: String, save_path: String, _options: Dictionary,
	_platform_variants: Array, _gen_files: Array
) -> Error:
	var character := RigExecCharacter.new()
	var data := FileAccess.get_file_as_bytes(source_file)
	if data.is_empty():
		printerr("rigexec: cannot read ", source_file)
		return ERR_FILE_CANT_READ
	character.set_data(data)
	if not character.bind():
		printerr("rigexec: ", character.get_bind_error())
		return ERR_PARSE_ERROR
	return ResourceSaver.save(
		character, save_path + "." + _get_save_extension())
