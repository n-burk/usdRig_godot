@tool
extends EditorImportPlugin

# Editor-only .rigexec importer: reads the binary, binds it through the
# runtime for validation, and saves a RigExecCharacter resource. Re-bake
# shells out to the dev machine's rigExecBake (never shipped).

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
