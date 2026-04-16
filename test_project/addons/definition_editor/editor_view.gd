@tool
extends Control

var data = {"biomes": [], "effects": [], "interactions": [], "transitions": []}
var mana_names = ["W (1)", "F (2)", "E (4)", "Pl (8)", "Pu (16)", "R (32)", "Mi (64)", "Ma (128)"]

func _ready():
	_setup_checkboxes()
	_load_json()
	_update_ui()

func _setup_checkboxes():
	for container in [%BiomeCheckboxes, %TransStartCheckboxes, %TransEndCheckboxes]:
		for i in range(8):
			var cb = CheckBox.new()
			cb.text = mana_names[i]
			cb.toggled.connect(_on_biome_checkbox_changed)
			container.add_child(cb)
			
	for i in range(8):
		var lbl = Label.new()
		lbl.text = "Bit %d:" % i
		%EffectInputs.add_child(lbl)
		var edit = LineEdit.new()
		edit.size_flags_horizontal = SIZE_EXPAND_FILL
		edit.text_changed.connect(_on_effect_name_changed.bind(i))
		%EffectInputs.add_child(edit)

func _load_json():
	var file = FileAccess.open("res://definitions.json", FileAccess.READ)
	if file:
		var json = JSON.parse_string(file.get_as_text())
		if json:
			data = json
	_refresh_dropdowns()

func _refresh_dropdowns():
	for dd in [%IntA, %IntB, %IntResult, %TransEffect]:
		dd.clear()
		for e in data.effects:
			dd.add_item(e.name, int(e.bit))

func _update_ui():
	_on_section_selected(%SectionSelector.selected)
	_on_biome_checkbox_changed(false)
	_on_byte_changed(%ByteSelector.value)
	_refresh_int_list()
	_refresh_trans_list()

func _on_section_selected(index):
	%BiomesPanel.visible = (index == 0)
	%EffectsPanel.visible = (index == 1)
	%InteractionsPanel.visible = (index == 2)
	%TransitionsPanel.visible = (index == 3)

# --- Biomes Logic ---

func _get_checkbox_id(container):
	var id = 0
	var cbs = container.get_children()
	for i in range(8):
		if cbs[i].button_pressed:
			id |= (1 << i)
	return id

func _set_checkbox_id(container, id):
	var cbs = container.get_children()
	for i in range(8):
		cbs[i].set_pressed_no_signal((id & (1 << i)) != 0)

func _on_biome_checkbox_changed(_toggled):
	var id = _get_checkbox_id(%BiomeCheckboxes)
	%BiomeIDLabel.text = "Target ID: %d" % id
	%BiomeNameInput.text = ""
	%BiomeDescInput.text = ""
	for b in data.biomes:
		if int(b.id) == id:
			%BiomeNameInput.text = b.name
			if b.has("desc"):
				%BiomeDescInput.text = b.desc
			break

func _on_update_biome_pressed():
	var id = _get_checkbox_id(%BiomeCheckboxes)
	var b_name = %BiomeNameInput.text
	var b_desc = %BiomeDescInput.text
	var found = false
	for b in data.biomes:
		if int(b.id) == id:
			b.name = b_name
			if b_desc != "":
				b.desc = b_desc
			elif b.has("desc"):
				b.remove("desc")
			found = true
			break
	if not found:
		var new_biome = {"id": id, "name": b_name}
		if b_desc != "":
			new_biome["desc"] = b_desc
		data.biomes.append(new_biome)
	_refresh_trans_list()

# --- Effects Logic ---

func _on_byte_changed(value):
	var byte_idx = int(value)
	var inputs = %EffectInputs.get_children()
	for i in range(8):
		var bit_idx = (byte_idx * 8) + i
		var bit_val = 1 << bit_idx
		var edit = inputs[i*2 + 1]
		edit.text = ""
		for e in data.effects:
			if int(e.bit) == bit_val:
				edit.text = e.name
				break

func _on_effect_name_changed(new_name, local_bit_idx):
	var byte_idx = int(%ByteSelector.value)
	var bit_idx = (byte_idx * 8) + local_bit_idx
	var bit_val = 1 << bit_idx
	
	var found = false
	for i in range(data.effects.size()):
		if int(data.effects[i].bit) == bit_val:
			if new_name == "":
				data.effects.remove_at(i)
			else:
				data.effects[i].name = new_name
			found = true
			break
	
	if not found and new_name != "":
		data.effects.append({"id": bit_idx, "name": new_name, "bit": bit_val})
	
	_refresh_dropdowns()

# --- Interactions Logic ---

func _refresh_int_list():
	%IntList.clear()
	for i in data.interactions:
		var text = "[%s] %s + %s" % [i.type.capitalize(), i.a, i.b]
		if i.has("result"): text += " -> " + i.result
		%IntList.add_item(text)

func _on_int_type_changed(index):
	%LResult.visible = (index == 1)
	%IntResult.visible = (index == 1)

func _on_int_selected(index):
	var i = data.interactions[index]
	%IntType.selected = 0 if i.type == "annihilation" else 1
	_on_int_type_changed(%IntType.selected)
	_set_option_text(%IntA, i.a)
	_set_option_text(%IntB, i.b)
	if i.has("result"): _set_option_text(%IntResult, i.result)

func _set_option_text(opt, text):
	for i in range(opt.item_count):
		if opt.get_item_text(i) == text:
			opt.selected = i
			return

func _on_add_int_pressed():
	var interaction = {
		"type": "annihilation" if %IntType.selected == 0 else "chemistry",
		"a": %IntA.get_item_text(%IntA.selected),
		"b": %IntB.get_item_text(%IntB.selected)
	}
	if interaction.type == "chemistry":
		interaction["result"] = %IntResult.get_item_text(%IntResult.selected)
	
	# Check if exists
	var found = false
	for i in range(data.interactions.size()):
		var existing = data.interactions[i]
		if existing.a == interaction.a and existing.b == interaction.b:
			data.interactions[i] = interaction
			found = true
			break
	if not found:
		data.interactions.append(interaction)
	_refresh_int_list()

func _on_delete_int_pressed():
	var selected = %IntList.get_selected_items()
	if selected.size() > 0:
		data.interactions.remove_at(selected[0])
		_refresh_int_list()

# --- Transitions Logic ---

func _refresh_trans_list():
	%TransList.clear()
	for t in data.transitions:
		%TransList.add_item("%s + %s -> %s" % [t.biome, t.effect, t.result])

func _on_trans_selected(index):
	var t = data.transitions[index]
	_set_checkbox_id(%TransStartCheckboxes, _find_biome_id(t.biome))
	_set_option_text(%TransEffect, t.effect)
	_set_checkbox_id(%TransEndCheckboxes, _find_biome_id(t.result))

func _find_biome_id(name):
	for b in data.biomes:
		if b.name == name: return int(b.id)
	return 0

func _find_biome_name(id):
	for b in data.biomes:
		if int(b.id) == id: return b.name
	return "Unknown (%d)" % id

func _on_add_trans_pressed():
	var start_id = _get_checkbox_id(%TransStartCheckboxes)
	var end_id = _get_checkbox_id(%TransEndCheckboxes)
	var trans = {
		"biome": _find_biome_name(start_id),
		"effect": %TransEffect.get_item_text(%TransEffect.selected),
		"result": _find_biome_name(end_id)
	}
	# Update or Add
	var found = false
	for i in range(data.transitions.size()):
		var existing = data.transitions[i]
		if existing.biome == trans.biome and existing.effect == trans.effect:
			data.transitions[i] = trans
			found = true
			break
	if not found:
		data.transitions.append(trans)
	_refresh_trans_list()

func _on_delete_trans_pressed():
	var selected = %TransList.get_selected_items()
	if selected.size() > 0:
		data.transitions.remove_at(selected[0])
		_refresh_trans_list()

# --- Global Save ---

func _on_save_all_pressed():
	var file = FileAccess.open("res://definitions.json", FileAccess.WRITE)
	if file:
		file.store_string(JSON.stringify(data, "\t"))
		print("Definitions saved to res://definitions.json")
		# Force Godot to re-scan the file
		if Engine.is_editor_hint():
			var plugin = EditorPlugin.new() # Dummy to get interface if needed, but easier to use scan
			# EditorInterface is not directly available in global scope but usually through EditorPlugin
			# For this tool, the notification is enough or manual scan.
