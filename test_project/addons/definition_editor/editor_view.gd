@tool
extends Control

var data = {"biomes": [], "effects": [], "interactions": [], "transitions": [], "units": []}
var mana_names = ["W (1)", "F (2)", "E (4)", "Pl (8)", "Pu (16)", "R (32)", "Mi (64)", "Ma (128)"]

func _ready():
	_setup_checkboxes()
	_load_json()
	_update_ui()

func _setup_checkboxes():
	# Clear existing if any (re-ready safety)
	for container in [%BiomeCheckboxes, %TransStartCheckboxes, %TransEndCheckboxes, %UnitManaCheckboxes, %UnitDietCheckboxes]:
		for child in container.get_children(): child.queue_free()
	
	for container in [%BiomeCheckboxes, %TransStartCheckboxes, %TransEndCheckboxes, %UnitManaCheckboxes, %UnitDietCheckboxes]:
		for i in range(8):
			var cb = CheckBox.new()
			cb.text = mana_names[i]
			if container != %UnitManaCheckboxes and container != %UnitDietCheckboxes:
				cb.toggled.connect(_on_biome_checkbox_changed)
			container.add_child(cb)
			
	for child in %EffectInputs.get_children(): child.queue_free()
	
	# Header for Effects
	%EffectInputs.add_child(Label.new()) # Empty
	var lbl_name = Label.new(); lbl_name.text = "Effect Name"; %EffectInputs.add_child(lbl_name)
	var lbl_int = Label.new(); lbl_int.text = "Spread Interval"; %EffectInputs.add_child(lbl_int)

	for i in range(8):
		var lbl = Label.new()
		lbl.text = "Bit %d:" % i
		%EffectInputs.add_child(lbl)
		var edit = LineEdit.new()
		edit.size_flags_horizontal = SIZE_EXPAND_FILL
		edit.text_changed.connect(_on_effect_data_changed.bind(i))
		%EffectInputs.add_child(edit)
		var sb = SpinBox.new()
		sb.min_value = 1; sb.max_value = 255; sb.value = 1
		sb.value_changed.connect(_on_effect_data_changed.bind(i))
		%EffectInputs.add_child(sb)

func _load_json():
	var file = FileAccess.open("res://definitions.json", FileAccess.READ)
	if file:
		var json = JSON.parse_string(file.get_as_text())
		if json:
			data = json
			if not data.has("units"): data["units"] = []
	_refresh_dropdowns()
	_refresh_all_lists()

func _refresh_dropdowns():
	for dd in [%IntA, %IntB, %IntResult, %TransEffect]:
		dd.clear()
		for e in data.effects:
			dd.add_item(e.name, int(e.bit))

func _refresh_all_lists():
	_refresh_biome_list()
	_refresh_int_list()
	_refresh_trans_list()
	_refresh_unit_list()

func _update_ui():
	_on_section_selected(%SectionSelector.selected)
	_on_biome_checkbox_changed(false)
	_on_byte_changed(%ByteSelector.value)

func _on_section_selected(index):
	%BiomesPanel.visible = (index == 0)
	%EffectsPanel.visible = (index == 1)
	%InteractionsPanel.visible = (index == 2)
	%TransitionsPanel.visible = (index == 3)
	%UnitsPanel.visible = (index == 4)

# --- Biomes Logic ---

func _refresh_biome_list():
	%BiomeList.clear()
	var filter = %BiomeSearch.text.to_lower()
	for b in data.biomes:
		if filter == "" or b.name.to_lower().contains(filter):
			%BiomeList.add_item("[%d] %s" % [int(b.id), b.name])
			%BiomeList.set_item_metadata(%BiomeList.get_item_count()-1, int(b.id))

func _on_biome_search_changed(_new_text):
	_refresh_biome_list()

func _on_biome_selected(index):
	var id = %BiomeList.get_item_metadata(index)
	_set_checkbox_id(%BiomeCheckboxes, id)
	_on_biome_checkbox_changed(true)

func _get_checkbox_id(container):
	var id = 0; var cbs = container.get_children()
	for i in range(8): if i < cbs.size() and cbs[i].button_pressed: id |= (1 << i)
	return id

func _set_checkbox_id(container, id):
	var cbs = container.get_children()
	for i in range(8): if i < cbs.size(): cbs[i].set_pressed_no_signal((id & (1 << i)) != 0)

func _on_biome_checkbox_changed(_toggled):
	var id = _get_checkbox_id(%BiomeCheckboxes)
	%BiomeIDLabel.text = "Target ID: %d" % id
	%BiomeNameInput.text = ""; %BiomeDescInput.text = ""; %BiomeFlammableCheck.button_pressed = false
	%BiomeWeightInput.value = 0
	for b in data.biomes:
		if int(b.id) == id:
			%BiomeNameInput.text = b.name
			if b.has("desc"): %BiomeDescInput.text = b.desc
			if b.has("flammable"): %BiomeFlammableCheck.button_pressed = b.flammable
			if b.has("weight"): %BiomeWeightInput.value = b.weight
			break

func _on_update_biome_pressed():
	var id = _get_checkbox_id(%BiomeCheckboxes)
	var b_name = %BiomeNameInput.text
	var b_desc = %BiomeDescInput.text
	var b_flammable = %BiomeFlammableCheck.button_pressed
	var b_weight = int(%BiomeWeightInput.value)
	var found = false
	for b in data.biomes:
		if int(b.id) == id:
			b.name = b_name
			if b_desc != "": b.desc = b_desc
			elif b.has("desc"): b.erase("desc")
			
			if b_flammable: b.flammable = true
			elif b.has("flammable"): b.erase("flammable")
			
			b.weight = b_weight
			
			found = true; break
	if not found:
		var new_biome = {"id": id, "name": b_name}
		if b_desc != "": new_biome["desc"] = b_desc
		if b_flammable: new_biome["flammable"] = true
		new_biome["weight"] = b_weight
		data.biomes.append(new_biome)
	_refresh_biome_list(); _refresh_trans_list()

# --- Effects Logic ---

func _on_byte_changed(value):
	var byte_idx = int(value); var inputs = %EffectInputs.get_children()
	for i in range(8):
		var bit_idx = (byte_idx * 8) + i; var bit_val = 1 << bit_idx; var edit = inputs[3 + i*3 + 1]; var sb = inputs[3 + i*3 + 2]
		edit.text = ""; sb.set_value_no_signal(1)
		for e in data.effects:
			if int(e.bit) == bit_val:
				edit.text = e.name; if e.has("interval"): sb.set_value_no_signal(e.interval)
				break

func _on_effect_data_changed(_val, local_bit_idx):
	var byte_idx = int(%ByteSelector.value)
	var bit_idx = (byte_idx * 8) + local_bit_idx
	var bit_val = 1 << bit_idx
	
	var inputs = %EffectInputs.get_children()
	var edit = inputs[3 + local_bit_idx*3 + 1]
	var sb = inputs[3 + local_bit_idx*3 + 2]
	
	var new_name = edit.text
	var new_interval = int(sb.value)
	
	var found = false
	for i in range(data.effects.size()):
		if int(data.effects[i].bit) == bit_val:
			if new_name == "":
				data.effects.remove_at(i)
			else:
				data.effects[i].name = new_name
				data.effects[i].interval = new_interval
			found = true; break
	
	if not found and new_name != "":
		data.effects.append({
			"id": float(bit_idx), 
			"name": new_name, 
			"bit": float(bit_val), 
			"interval": float(new_interval)
		})
	_refresh_dropdowns()

# --- Interactions Logic ---

func _refresh_int_list():
	%IntList.clear(); var filter = %IntSearch.text.to_lower()
	for i in range(data.interactions.size()):
		var interaction = data.interactions[i]; var text = "[%s] %s + %s" % [interaction.type.capitalize(), interaction.a, interaction.b]
		if interaction.has("result"): text += " -> " + interaction.result
		if filter == "" or text.to_lower().contains(filter):
			%IntList.add_item(text); %IntList.set_item_metadata(%IntList.get_item_count()-1, i)

func _on_int_search_changed(_new_text): _refresh_int_list()
func _on_int_type_changed(index): %LResult.visible = (index == 1); %IntResult.visible = (index == 1)
func _on_int_selected(index):
	var actual_idx = %IntList.get_item_metadata(index); var interaction = data.interactions[actual_idx]
	%IntType.selected = 0 if interaction.type == "annihilation" else 1
	_on_int_type_changed(%IntType.selected); _set_option_text(%IntA, interaction.a); _set_option_text(%IntB, interaction.b)
	if interaction.has("result"): _set_option_text(%IntResult, interaction.result)

func _set_option_text(opt, text):
	for i in range(opt.item_count): if opt.get_item_text(i) == text: opt.selected = i; return

func _on_add_int_pressed():
	var interaction = {"type": "annihilation" if %IntType.selected == 0 else "chemistry", "a": %IntA.get_item_text(%IntA.selected), "b": %IntB.get_item_text(%IntB.selected)}
	if interaction.type == "chemistry": interaction["result"] = %IntResult.get_item_text(%IntResult.selected)
	var found = false
	for i in range(data.interactions.size()):
		var existing = data.interactions[i]
		if existing.a == interaction.a and existing.b == interaction.b: data.interactions[i] = interaction; found = true; break
	if not found: data.interactions.append(interaction)
	_refresh_int_list()

func _on_delete_int_pressed():
	var selected = %IntList.get_selected_items()
	if selected.size() > 0: var actual_idx = %IntList.get_item_metadata(selected[0]); data.interactions.remove_at(actual_idx); _refresh_int_list()

# --- Transitions Logic ---

func _refresh_trans_list():
	%TransList.clear(); var filter = %TransSearch.text.to_lower()
	for i in range(data.transitions.size()):
		var t = data.transitions[i]; var text = "%s + %s -> %s" % [t.biome, t.effect, t.result]
		if filter == "" or text.to_lower().contains(filter): %TransList.add_item(text); %TransList.set_item_metadata(%TransList.get_item_count()-1, i)

func _on_trans_search_changed(_new_text): _refresh_trans_list()
func _on_trans_selected(index):
	var actual_idx = %TransList.get_item_metadata(index); var t = data.transitions[actual_idx]
	_set_checkbox_id(%TransStartCheckboxes, _find_biome_id(t.biome)); _set_option_text(%TransEffect, t.effect); _set_checkbox_id(%TransEndCheckboxes, _find_biome_id(t.result))

func _find_biome_id(name):
	for b in data.biomes: if b.name == name: return int(b.id)
	return 0

func _find_biome_name(id):
	for b in data.biomes: if int(b.id) == id: return b.name
	return "Unknown (%d)" % id

func _on_add_trans_pressed():
	var start_id = _get_checkbox_id(%TransStartCheckboxes); var end_id = _get_checkbox_id(%TransEndCheckboxes)
	var trans = {"biome": _find_biome_name(start_id), "effect": %TransEffect.get_item_text(%TransEffect.selected), "result": _find_biome_name(end_id)}
	var found = false
	for i in range(data.transitions.size()):
		var existing = data.transitions[i]
		if existing.biome == trans.biome and existing.effect == trans.effect: data.transitions[i] = trans; found = true; break
	if not found: data.transitions.append(trans)
	_refresh_trans_list()

func _on_delete_trans_pressed():
	var selected = %TransList.get_selected_items()
	if selected.size() > 0: var actual_idx = %TransList.get_item_metadata(selected[0]); data.transitions.remove_at(actual_idx); _refresh_trans_list()

# --- Units Logic ---

func _refresh_unit_list():
	%UnitList.clear()
	for u in data.units: %UnitList.add_item(u.name)

func _on_unit_selected(index):
	var u = data.units[index]
	%UnitNameInput.text = u.name
	%UnitWeightInput.value = u.weight
	%UnitVelInput.value = u.velocity
	%UnitSpeciesInput.value = u.get("species", 0)
	%UnitSatedInput.value = u.get("sated_duration", 10)
	_set_checkbox_id(%UnitManaCheckboxes, int(u.get("mana", 0)))
	_set_checkbox_id(%UnitDietCheckboxes, int(u.get("diet", 0)))
	%UnitPushCheck.button_pressed = u.get("push", false)
	%UnitHerbivoreCheck.button_pressed = u.get("herbivore", false)
	%UnitCarnivoreCheck.button_pressed = u.get("carnivore", false)

func _on_add_unit_pressed():
	var unit = {
		"name": %UnitNameInput.text,
		"weight": int(%UnitWeightInput.value),
		"velocity": int(%UnitVelInput.value),
		"species": int(%UnitSpeciesInput.value),
		"sated_duration": int(%UnitSatedInput.value),
		"mana": _get_checkbox_id(%UnitManaCheckboxes),
		"diet": _get_checkbox_id(%UnitDietCheckboxes),
		"push": %UnitPushCheck.button_pressed,
		"herbivore": %UnitHerbivoreCheck.button_pressed,
		"carnivore": %UnitCarnivoreCheck.button_pressed
	}
	var found = false
	for i in range(data.units.size()):
		if data.units[i].name == unit.name:
			data.units[i] = unit
			found = true; break
	if not found: data.units.append(unit)
	_refresh_unit_list()

func _on_delete_unit_pressed():
	var selected = %UnitList.get_selected_items()
	if selected.size() > 0:
		data.units.remove_at(selected[0])
		_refresh_unit_list()

# --- Global Save ---

func _on_save_all_pressed():
	var abs_path = ProjectSettings.globalize_path("res://definitions.json")
	var file = FileAccess.open(abs_path, FileAccess.WRITE)
	if file:
		file.store_string(JSON.stringify(data, "\t"))
		file.flush(); file.close()
		EditorInterface.get_resource_filesystem().scan()
		print("DefinitionEditor: Saved to ", abs_path)
