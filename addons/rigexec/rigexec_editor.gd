@tool
extends EditorPlugin

# Editor side of the rigExec addon: registers the .rigexec import plugin.
# Runtime playback needs no editor state -- the player reads bytes -- so
# this file is the only piece the editor loads that a game never does.

var _importer: EditorImportPlugin


func _enter_tree() -> void:
	_importer = preload("rigexec_import.gd").new()
	add_import_plugin(_importer)


func _exit_tree() -> void:
	remove_import_plugin(_importer)
	_importer = null
