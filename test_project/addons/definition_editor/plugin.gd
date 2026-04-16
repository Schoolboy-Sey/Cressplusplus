@tool
extends EditorPlugin

var editor_instance

func _enter_tree():
	editor_instance = preload("res://addons/definition_editor/editor_view.tscn").instantiate()
	# Add the main editor to the center viewport
	get_editor_interface().get_editor_main_screen().add_child(editor_instance)
	_make_visible(false)

func _exit_tree():
	if editor_instance:
		editor_instance.queue_free()

func _has_main_screen():
	return true

func _make_visible(visible):
	if editor_instance:
		editor_instance.visible = visible

func _get_plugin_name():
	return "Definition Editor"

func _get_plugin_icon():
	# Use a built-in Godot editor icon
	return get_editor_interface().get_base_control().get_theme_icon("Edit", "EditorIcons")
