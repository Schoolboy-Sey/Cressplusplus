extends Node2D

enum MODE { EDIT, PLAY }
var current_mode = MODE.EDIT

var sim: SimulationManager
var tile_size: int = 20
var show_scent: bool = true
var show_biome: bool = true
var show_impassable: bool = true

var selected_tile: Vector2i = Vector2i(-1, -1)
var selected_unit_id: int = -1
var is_painting: bool = false
var spawn_team: int = 0 # 0=Ally, 1=Enemy

var definitions = {}
var effect_names = {}
var biome_names = {}
var wave_bit: int = 0

@onready var ui_panel = $CanvasLayer/Control/Panel
@onready var play_hud = %PlayHUD
@onready var comp_input = $CanvasLayer/Control/Panel/VBoxContainer/CompInput
@onready var biome_name_label = $CanvasLayer/Control/Panel/VBoxContainer/BiomeName
@onready var bit_container = $CanvasLayer/Control/Panel/VBoxContainer/BitContainer
@onready var impassable_check = $CanvasLayer/Control/Panel/VBoxContainer/ImpassableCheck
@onready var effect_dropdown = $CanvasLayer/Control/Panel/VBoxContainer/EffectDropdown

var mana_names = ["W (1)", "F (2)", "E (4)", "Pl (8)", "Pu (16)", "R (32)", "Mi (64)", "Ma (128)"]

func _ready():
	sim = SimulationManager.new()
	add_child(sim)
	_load_definitions()
	_setup_bit_checkboxes()
	sim.generate_new_world(42)
	_refresh_map_list()
	_update_mode_ui()
	
	for i in range(8):
		%WaveBitSelector.add_item(mana_names[i], i)
	%WaveBitSelector.item_selected.connect(func(idx): wave_bit = idx; queue_redraw())
	%WaveToggle.toggled.connect(func(_p): queue_redraw())
	
	queue_redraw()

func _update_mode_ui():
	if current_mode == MODE.EDIT:
		ui_panel.show(); play_hud.hide()
	else:
		ui_panel.hide(); play_hud.show()

func _on_play_mode_pressed():
	sim.save_state_snapshot()
	sim.auto_update_scent()
	sim.process_ai_intents()
	current_mode = MODE.PLAY
	_update_mode_ui()
	queue_redraw()

func _on_exit_play_pressed():
	sim.load_state_snapshot()
	current_mode = MODE.EDIT
	selected_unit_id = -1
	_update_mode_ui()
	queue_redraw()

func _on_execute_turn_pressed():
	for i in range(10): sim.run_step()
	sim.auto_update_scent()
	sim.process_ai_intents()
	queue_redraw()

func _setup_bit_checkboxes():
	for i in range(8):
		var cb = CheckBox.new(); cb.text = mana_names[i]
		cb.toggled.connect(_on_bit_toggled.bind(1 << i))
		bit_container.add_child(cb)

func _on_bit_toggled(toggled: bool, bit: int):
	if selected_tile.x != -1:
		var cur = sim.get_tile_composition(selected_tile.x, selected_tile.y)
		if toggled: cur |= bit
		else: cur &= ~bit
		sim.set_tile_composition(selected_tile.x, selected_tile.y, cur)
		comp_input.text = str(cur); queue_redraw()

func _load_definitions():
	var file = FileAccess.open("res://definitions.json", FileAccess.READ)
	if file:
		var json = JSON.parse_string(file.get_as_text())
		if json:
			definitions = json; sim.clear_interaction_tables()
			for b in definitions.biomes:
				biome_names[int(b.id)] = b.name
				if b.has("flammable") and b.flammable: sim.set_flammable(int(b.id), true)
				if b.has("weight"): sim.set_biome_weight(int(b.id), int(b.weight))
			for e in definitions.effects:
				var bit = int(e.bit); effect_names[bit] = e.name; effect_dropdown.add_item(e.name, bit)
				var b_idx = _get_bit_index(bit)
				sim.set_propagation_rule(b_idx, true, false) # Default: check flammable, ignore elevation for now
				if e.has("interval"): sim.set_propagation_interval(b_idx, int(e.interval))
				if e.has("weight"): sim.set_effect_weight(b_idx, int(e.weight))
			
			%UnitSpawnDropdown.clear()
			if definitions.has("units"):
				for u in definitions.units: %UnitSpawnDropdown.add_item(u.name)
				
			for i in definitions.interactions:
				var bit_a = _get_effect_bit(i.a); var bit_b = _get_effect_bit(i.b)
				if i.type == "annihilation":
					if bit_a != 0 and bit_b != 0: sim.add_annihilation(_get_bit_index(bit_a), _get_bit_index(bit_b))
				elif i.type == "chemistry":
					var res_bit = _get_effect_bit(i.result)
					if bit_a != 0 and bit_b != 0 and res_bit != 0: sim.add_chemistry(_get_bit_index(bit_a), _get_bit_index(bit_b), res_bit)
			for t in definitions.transitions:
				var b_id = _get_biome_id(t.biome); var e_bit = _get_effect_bit(t.effect); var res_b = _get_biome_id(t.result)
				if e_bit != 0: sim.add_biome_transition(b_id, _get_bit_index(e_bit), res_b)

func _on_team_radio_toggled(pressed: bool, team: int):
	if pressed:
		spawn_team = team
		if team == 0: %EnemyRadio.set_pressed_no_signal(false)
		else: %AllyRadio.set_pressed_no_signal(false)

func _on_spawn_unit_pressed():
	if selected_tile.x == -1: return
	var idx = %UnitSpawnDropdown.selected
	if idx == -1: return
	var u_def = definitions.units[idx]
	var team = 0 if %AllyRadio.button_pressed else 1
	var flags = 0
	if u_def.get("push", false): flags |= 1 << 1 # FLAG_PUSH
	if u_def.get("herbivore", false): flags |= 1 << 3 # FLAG_HERBIVORE
	if u_def.get("carnivore", false): flags |= 1 << 4 # FLAG_CARNIVORE
	
	var id = sim.spawn_unit_full(selected_tile.x, selected_tile.y, team, u_def.weight, u_def.velocity, flags)
	if id != -1:
		sim.set_unit_diet(id, int(u_def.get("diet", 0)))
	queue_redraw()

func _get_effect_bit(n: String) -> int:
	for e in definitions.effects: if e.name.strip_edges().to_lower() == n.strip_edges().to_lower(): return int(e.bit)
	return 0

func _get_bit_index(bit: int) -> int:
	for i in range(64): if bit == (1 << i): return i
	return 0

func _get_biome_id(n: String) -> int:
	for b in definitions.biomes: if b.name.strip_edges().to_lower() == n.strip_edges().to_lower(): return int(b.id)
	return 0

func _input(event):
	if event is InputEventMouseButton:
		var grid_pos = Vector2i(event.position / tile_size)
		if current_mode == MODE.EDIT:
			if event.button_index == MOUSE_BUTTON_LEFT:
				if event.pressed:
					if _is_valid_pos(grid_pos): is_painting = true; selected_tile = grid_pos; _update_ui(); queue_redraw()
					else: is_painting = false
				else: is_painting = false
			elif event.button_index == MOUSE_BUTTON_RIGHT and event.pressed:
				if _is_valid_pos(grid_pos):
					if Input.is_key_pressed(KEY_SHIFT): sim.set_tile_effect(grid_pos.x, grid_pos.y, effect_dropdown.get_selected_id())
					else: selected_tile = grid_pos; _update_ui()
					queue_redraw()
		elif current_mode == MODE.PLAY:
			if event.button_index == MOUSE_BUTTON_LEFT and event.pressed:
				if _is_valid_pos(grid_pos):
					var uid_at_click = sim.get_unit_at(grid_pos.x, grid_pos.y)
					if selected_unit_id != -1 and sim.get_unit_team(selected_unit_id) == 0:
						if uid_at_click != -1 and sim.get_unit_team(uid_at_click) == 1:
							var u_pos = sim.get_unit_pos(selected_unit_id)
							if (abs(grid_pos.x - u_pos.x) + abs(grid_pos.y - u_pos.y)) <= sim.get_unit_velocity(selected_unit_id):
								sim.move_unit_intent(selected_unit_id, grid_pos.x, grid_pos.y)
								sim.auto_update_scent(); sim.process_ai_intents(); queue_redraw(); return
					if uid_at_click != -1: selected_unit_id = uid_at_click; selected_tile = grid_pos; queue_redraw()
					elif selected_unit_id != -1 and sim.get_unit_team(selected_unit_id) == 0:
						var u_pos = sim.get_unit_pos(selected_unit_id)
						if (abs(grid_pos.x - u_pos.x) + abs(grid_pos.y - u_pos.y)) <= sim.get_unit_velocity(selected_unit_id):
							sim.move_unit_intent(selected_unit_id, grid_pos.x, grid_pos.y)
							selected_tile = grid_pos; sim.auto_update_scent(); sim.process_ai_intents(); queue_redraw()
					else: selected_tile = grid_pos; selected_unit_id = -1; queue_redraw()

	elif event is InputEventMouseMotion and is_painting and current_mode == MODE.EDIT:
		var grid_pos = Vector2i(event.position / tile_size)
		if _is_valid_pos(grid_pos) and grid_pos != selected_tile:
			if Input.is_key_pressed(KEY_CTRL): sim.set_impassable(grid_pos.x, grid_pos.y, impassable_check.button_pressed)
			elif Input.is_key_pressed(KEY_SHIFT): sim.set_tile_effect(grid_pos.x, grid_pos.y, effect_dropdown.get_selected_id())
			else: sim.set_tile_composition(grid_pos.x, grid_pos.y, int(comp_input.text))
			queue_redraw()

func _is_valid_pos(pos: Vector2i) -> bool: return pos.x >= 0 and pos.x < sim.map_width and pos.y >= 0 and pos.y < sim.map_height

func _update_ui():
	if selected_tile.x != -1:
		var comp = sim.get_tile_composition(selected_tile.x, selected_tile.y)
		comp_input.text = str(comp); biome_name_label.text = biome_names.get(comp, "Unknown Biome (%d)" % comp)
		impassable_check.button_pressed = sim.is_impassable(selected_tile.x, selected_tile.y)
		var cbs = bit_container.get_children()
		for i in range(8): if i < cbs.size(): cbs[i].set_pressed_no_signal((comp & (1 << i)) != 0)

func _on_run_step_pressed(): sim.run_step(); sim.auto_update_scent(); queue_redraw()
func _on_run_turn_pressed(): 
	for i in range(10): sim.run_step()
	sim.auto_update_scent(); queue_redraw()

func _on_spawn_player_pressed(): if selected_tile.x != -1: sim.spawn_unit(selected_tile.x, selected_tile.y, 0, 10); queue_redraw()
func _on_spawn_enemy_pressed(): if selected_tile.x != -1: sim.spawn_unit(selected_tile.x, selected_tile.y, 1, 5); queue_redraw()
func _on_despawn_unit_pressed():
	if selected_tile.x != -1:
		var uid = sim.get_unit_at(selected_tile.x, selected_tile.y)
		if uid != -1: sim.despawn_unit(uid); queue_redraw()

func _on_comp_input_text_changed(new_text):
	if selected_tile.x != -1: sim.set_tile_composition(selected_tile.x, selected_tile.y, int(new_text)); _update_ui(); queue_redraw()
func _on_impassable_check_toggled(button_pressed):
	if selected_tile.x != -1: sim.set_impassable(selected_tile.x, selected_tile.y, button_pressed); sim.auto_update_scent(); queue_redraw()

func _refresh_map_list():
	%MapDropdown.clear()
	var dir = DirAccess.open("user://")
	if not dir.dir_exists("maps"): dir.make_dir("maps")
	dir = DirAccess.open("user://maps")
	if dir:
		dir.list_dir_begin(); var file_name = dir.get_next()
		while file_name != "":
			if not dir.current_is_dir() and file_name.ends_with(".json"): %MapDropdown.add_item(file_name)
			file_name = dir.get_next()

func _on_save_map_as_pressed():
	var map_name = %MapNameInput.text
	if map_name == "": return
	if not map_name.ends_with(".json"): map_name += ".json"
	
	var data = {
		"width": sim.map_width, 
		"height": sim.map_height, 
		"tiles": [],
		"units": []
	}
	
	for z in range(sim.map_height):
		for x in range(sim.map_width):
			var tile = {
				"x": x, "z": z, 
				"comp": sim.get_tile_composition(x, z), 
				"imp": sim.is_impassable(x, z), 
				"eff": sim.get_tile_effects(x, z)
			}
			if tile.comp != 0 or tile.imp or tile.eff != 0: 
				data.tiles.append(tile)
	
	var all_units = sim.get_all_units()
	for id in all_units:
		var pos = all_units[id]
		var u_data = {
			"x": pos.x,
			"z": pos.y,
			"team": sim.get_unit_team(id),
			"weight": sim.get_unit_weight(id),
			"velocity": sim.get_unit_velocity(id),
			"flags": sim.get_unit_flags(id),
			"diet": sim.get_unit_diet(id)
		}
		data.units.append(u_data)
		
	var file = FileAccess.open("user://maps/" + map_name, FileAccess.WRITE)
	file.store_string(JSON.stringify(data))
	_refresh_map_list()

func _on_load_map_pressed():
	var idx = %MapDropdown.selected
	if idx == -1: return
	var map_name = %MapDropdown.get_item_text(idx)
	%MapNameInput.text = map_name.replace(".json", "")
	var file = FileAccess.open("user://maps/" + map_name, FileAccess.READ)
	if not file: return
	
	var data = JSON.parse_string(file.get_as_text())
	sim.generate_new_world(0)
	
	for t in data.tiles: 
		sim.set_tile_composition(t.x, t.z, int(t.comp))
		sim.set_impassable(t.x, t.z, bool(t.imp))
		sim.set_tile_effect(t.x, t.z, int(t.eff))
	
	if data.has("units"):
		for u in data.units:
			var id = sim.spawn_unit_full(int(u.x), int(u.z), int(u.team), int(u.weight), int(u.velocity), int(u.flags))
			if id != -1:
				sim.set_unit_diet(id, int(u.get("diet", 0)))
	
	sim.auto_update_scent()
	queue_redraw()

func _on_toggle_scent_toggled(button_pressed): show_scent = button_pressed; queue_redraw()
func _on_toggle_biome_toggled(button_pressed): show_biome = button_pressed; queue_redraw()
func _on_toggle_impassable_toggled(button_pressed): show_impassable = button_pressed; queue_redraw()
func _on_load_definitions_pressed(): effect_dropdown.clear(); biome_names.clear(); _load_definitions(); _update_ui()

func _draw():
	if not sim: return
	var width = sim.map_width; var height = sim.map_height
	var grid_data = sim.get_grid_data()
	
	for z in range(height):
		for x in range(width):
			var idx = z * width + x; var tile_offset = idx * 16; var rect = Rect2(x * tile_size, z * tile_size, tile_size, tile_size)
			if show_biome: draw_rect(rect, _get_biome_color(grid_data[tile_offset]))
			else: draw_rect(rect, Color.DARK_GRAY, false)
			if show_impassable and (grid_data[tile_offset + 3] & 0x20): draw_rect(rect.grow(-2), Color.BLACK, false, 2.0)
			
			if %WaveToggle.button_pressed:
				var imprint = grid_data.decode_u16(tile_offset + 13)
				var state = (imprint >> (wave_bit * 2)) & 3
				if state == 3: draw_circle(rect.get_center(), tile_size * 0.4, Color.GOLD)
				elif state == 2: draw_circle(rect.get_center(), tile_size * 0.3, Color.CYAN)
				elif state == 1: draw_circle(rect.get_center(), tile_size * 0.2, Color(0.2, 0.5, 1, 0.5))

			var effects = grid_data.decode_u64(tile_offset + 4)
			if effects != 0:
				var center = rect.get_center(); var angle = 0.0; var count = 0
				for i in range(16): if effects & (1 << i): count += 1
				for i in range(16):
					if effects & (1 << i):
						var bp = center; if count > 1: bp += Vector2(cos(angle), sin(angle)) * 5.0; angle += (2.0 * PI) / count
						draw_circle(bp, 3, _get_mana_color(1 << i))
			if show_scent:
				var scent = grid_data[tile_offset + 3] & 0x0F
				if scent > 0: draw_rect(rect, Color(0.8, 0, 0.8, 0.2)); draw_string($CanvasLayer/Control.get_theme_default_font(), rect.position + Vector2(2, 14), str(scent), HORIZONTAL_ALIGNMENT_LEFT, -1, 10)
			draw_rect(rect, Color(0.2, 0.2, 0.2, 0.5), false)
	_draw_unit_intents(); _draw_units()
	if selected_tile.x != -1:
		draw_rect(Rect2(selected_tile.x * tile_size, selected_tile.y * tile_size, tile_size, tile_size), Color.WHITE, false, 1.0)
		var comp = sim.get_tile_composition(selected_tile.x, selected_tile.y)
		var mana = sim.get_tile_mana(selected_tile.x, selected_tile.y)
		var info_str = "Tile (%d, %d)\nID: %d (%s)\nMana: %d" % [selected_tile.x, selected_tile.y, comp, biome_names.get(comp, "Unknown"), mana]
		var info_pos = Vector2(selected_tile.x + 1, selected_tile.y) * tile_size + Vector2(5, 0)
		if info_pos.x > sim.map_width * tile_size - 100: info_pos.x -= 150
		draw_string($CanvasLayer/Control.get_theme_default_font(), info_pos, info_str, HORIZONTAL_ALIGNMENT_LEFT, -1, 12, Color.YELLOW)

func _draw_units():
	var units = sim.get_all_units()
	for id in units:
		var pos = units[id]; var screen_pos = Vector2(pos.x, pos.y) * tile_size + Vector2(tile_size, tile_size) / 2
		var team = sim.get_unit_team(id); var color = Color.GREEN if team == 0 else Color.RED
		draw_circle(screen_pos, tile_size * 0.4, color)
		var sated_time = sim.get_unit_sated_timer(id)
		if sated_time > 0:
			var bar_w = tile_size * 0.8 * (float(sated_time) / 10.0)
			draw_rect(Rect2(screen_pos + Vector2(-tile_size*0.4, tile_size*0.3), Vector2(bar_w, 3)), Color.SADDLE_BROWN)
		var intent = sim.get_unit_intent_pos(id)
		if intent == pos:
			var diet = sim.get_unit_diet(id); var mana = sim.get_tile_mana(pos.x, pos.y); var composition = sim.get_tile_composition(pos.x, pos.y)
			if (mana | composition) & diet: draw_arc(screen_pos, tile_size * 0.5, 0, PI*2, 8, Color.LIME_GREEN, 2.0)

func _draw_unit_intents():
	var units = sim.get_all_units()
	for uid in units:
		var start = sim.get_unit_pos(uid); var end = sim.get_unit_intent_pos(uid)
		if start == end: continue
		var p1 = Vector2(start.x * tile_size + tile_size/2, start.y * tile_size + tile_size/2)
		var p2 = Vector2(end.x * tile_size + tile_size/2, end.y * tile_size + tile_size/2)
		var color = Color.CYAN if sim.get_unit_team(uid) == 0 else Color.RED
		draw_line(p1, p2, color, 2.0)
		var dir = (p2 - p1).normalized(); draw_line(p2, p2 - dir.rotated(0.5) * 5, color, 2.0); draw_line(p2, p2 - dir.rotated(-0.5) * 5, color, 2.0)

func _get_mana_color(bit: int) -> Color:
	match bit:
		1: return Color.BLUE
		2: return Color.RED
		4: return Color(0.6, 0.4, 0.2)
		8: return Color.GREEN
		16: return Color.WHITE
		32: return Color(0.2, 0.1, 0.3)
		64: return Color.GOLD
		128: return Color.CYAN
	return Color.GRAY

func _get_biome_color(comp: int) -> Color:
	if comp == 0: return Color(0.3, 0.4, 0.2)
	var color = Color(0, 0, 0); var count = 0
	if comp & 1: color += Color.BLUE; count += 1
	if comp & 2: color += Color.RED; count += 1
	if comp & 4: color += Color(0.6, 0.4, 0.2); count += 1
	if comp & 8: color += Color.GREEN; count += 1
	if comp & 16: color += Color.WHITE; count += 1
	if comp & 32: color += Color(0.2, 0.1, 0.3); count += 1
	if comp & 64: color += Color.GOLD; count += 1
	if comp & 128: color += Color.CYAN; count += 1
	if count > 0: return color / count
	return Color.BLACK
