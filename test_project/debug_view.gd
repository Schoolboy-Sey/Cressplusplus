extends Node2D

var sim: SimulationManager
var tile_size: int = 20
var show_scent: bool = true
var show_biome: bool = true
var show_impassable: bool = true

var selected_tile: Vector2i = Vector2i(-1, -1)
var player_pos: Vector2i = Vector2i(5, 5)
var is_painting: bool = false

var definitions = {}
var effect_names = {}
var biome_names = {}

@onready var ui_panel = $CanvasLayer/Control/Panel
@onready var comp_input = $CanvasLayer/Control/Panel/VBoxContainer/CompInput
@onready var biome_name_label = $CanvasLayer/Control/Panel/VBoxContainer/BiomeName
@onready var bit_container = $CanvasLayer/Control/Panel/VBoxContainer/BitContainer
@onready var impassable_check = $CanvasLayer/Control/Panel/VBoxContainer/ImpassableCheck
@onready var effect_dropdown = $CanvasLayer/Control/Panel/VBoxContainer/EffectDropdown

func _ready():
	sim = SimulationManager.new()
	add_child(sim)
	_load_definitions()
	_setup_bit_checkboxes()
	sim.generate_new_world(42)
	# starting with the smell not displayed. 
	# sim.run_scent_update(player_pos.x, player_pos.y)
	_refresh_map_list()
	queue_redraw()

func _setup_bit_checkboxes():
	var names = ["W (1)", "F (2)", "E (4)", "Pl (8)", "Pu (16)", "R (32)", "Mi (64)", "Ma (128)"]
	for i in range(8):
		var cb = CheckBox.new()
		cb.text = names[i]
		cb.toggled.connect(_on_bit_toggled.bind(1 << i))
		bit_container.add_child(cb)

func _on_bit_toggled(toggled: bool, bit: int):
	if selected_tile.x != -1:
		var current = sim.get_tile_composition(selected_tile.x, selected_tile.y)
		if toggled:
			current |= bit
		else:
			current &= ~bit
		sim.set_tile_composition(selected_tile.x, selected_tile.y, current)
		comp_input.text = str(current)
		queue_redraw()

func _load_definitions():
	var file = FileAccess.open("res://definitions.json", FileAccess.READ)
	if file:
		var json = JSON.parse_string(file.get_as_text())
		if json:
			definitions = json
			sim.clear_interaction_tables()
			print("DebugView: Loading %d biomes, %d effects..." % [definitions.biomes.size(), definitions.effects.size()])
			
			# Map Biomes
			for b in definitions.biomes:
				biome_names[int(b.id)] = b.name
				if b.has("flammable") and b.flammable:
					sim.set_flammable(int(b.id), true)
					print("  Biome %d [%s] marked Flammable" % [int(b.id), b.name])
				
			# Map Effects
			for e in definitions.effects:
				var bit = int(e.bit)
				effect_names[bit] = e.name
				effect_dropdown.add_item(e.name, bit)
				
				# Apply spread interval if defined
				if e.has("interval"):
					var idx = _get_bit_index(bit)
					sim.set_propagation_interval(idx, int(e.interval))
					print("  Effect [%s] interval set to %d" % [e.name, int(e.interval)])
				
			var fire_bit = _get_effect_bit("Fire")
			if fire_bit != 0:
				var idx = _get_bit_index(fire_bit)
				sim.set_propagation_rule(idx, true, false)
				print("  Fire Rule: Bit %d is active" % idx)
				
			# Map Interactions
			for i in definitions.interactions:
				if i.type == "annihilation":
					var bit_a = _get_effect_bit(i.a)
					var bit_b = _get_effect_bit(i.b)
					if bit_a != 0 and bit_b != 0:
						sim.add_annihilation(_get_bit_index(bit_a), _get_bit_index(bit_b))
				elif i.type == "chemistry":
					var bit_a = _get_effect_bit(i.a)
					var bit_b = _get_effect_bit(i.b)
					var res_bit = _get_effect_bit(i.result)
					if bit_a != 0 and bit_b != 0 and res_bit != 0:
						sim.add_chemistry(_get_bit_index(bit_a), _get_bit_index(bit_b), res_bit)
			
			# Map Transitions
			var trans_count = 0
			for t in definitions.transitions:
				var b_id = _get_biome_id(t.biome)
				var e_bit = _get_effect_bit(t.effect)
				var res_b = _get_biome_id(t.result)
				# Important: only add if we found the IDs
				if e_bit != 0:
					sim.add_biome_transition(b_id, _get_bit_index(e_bit), res_b)
					trans_count += 1
			print("DebugView: Loaded %d valid transitions." % trans_count)

func _get_effect_bit(effect_name: String) -> int:
	for e in definitions.effects:
		if e.name.strip_edges().to_lower() == effect_name.strip_edges().to_lower():
			return int(e.bit)
	return 0

func _get_bit_index(bit: int) -> int:
	for i in range(64):
		if bit == (1 << i): return i
	return 0

func _get_biome_id(biome_name: String) -> int:
	for b in definitions.biomes:
		if b.name.strip_edges().to_lower() == biome_name.strip_edges().to_lower():
			return int(b.id)
	return 0

func _input(event):
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_LEFT:
			if event.pressed:
				var grid_pos = Vector2i(event.position / tile_size)
				if _is_valid_pos(grid_pos):
					is_painting = true
					selected_tile = grid_pos
					_update_ui()
					queue_redraw()
				else:
					is_painting = false
			else:
				is_painting = false
		elif event.button_index == MOUSE_BUTTON_RIGHT and event.pressed:
			var grid_pos = Vector2i(event.position / tile_size)
			if _is_valid_pos(grid_pos):
				if Input.is_key_pressed(KEY_SHIFT):
					var effect_bit = effect_dropdown.get_selected_id()
					sim.set_tile_effect(grid_pos.x, grid_pos.y, effect_bit)
				else:
					player_pos = grid_pos
					sim.run_scent_update(player_pos.x, player_pos.y)
				queue_redraw()
				
	elif event is InputEventMouseMotion and is_painting:
		var grid_pos = Vector2i(event.position / tile_size)
		if _is_valid_pos(grid_pos) and grid_pos != selected_tile:
			# PAINT logic
			if Input.is_key_pressed(KEY_CTRL): # Paint Impassable
				sim.set_impassable(grid_pos.x, grid_pos.y, impassable_check.button_pressed)
			elif Input.is_key_pressed(KEY_SHIFT): # Paint Effect
				var effect_bit = effect_dropdown.get_selected_id()
				sim.set_tile_effect(grid_pos.x, grid_pos.y, effect_bit)
			else: # Paint Composition
				var current_comp = int(comp_input.text)
				sim.set_tile_composition(grid_pos.x, grid_pos.y, current_comp)
			
			queue_redraw()

func _is_valid_pos(pos: Vector2i) -> bool:
	return pos.x >= 0 and pos.x < sim.map_width and pos.y >= 0 and pos.y < sim.map_height

func _update_ui():
	if selected_tile.x != -1:
		ui_panel.show()
		var comp = sim.get_tile_composition(selected_tile.x, selected_tile.y)
		comp_input.text = str(comp)
		
		# Update Biome Name
		if biome_names.has(comp):
			biome_name_label.text = biome_names[comp]
		else:
			biome_name_label.text = "Unknown Biome (%d)" % comp
			
		impassable_check.button_pressed = sim.is_impassable(selected_tile.x, selected_tile.y)
		
		var cbs = bit_container.get_children()
		for i in range(8):
			if i < cbs.size():
				cbs[i].set_pressed_no_signal((comp & (1 << i)) != 0)
	else:
		ui_panel.hide()

func _on_run_step_pressed():
	sim.run_step()
	sim.run_scent_update(player_pos.x, player_pos.y)
	queue_redraw()

func _on_run_turn_pressed():
	for i in range(10):
		sim.run_step()
	sim.run_scent_update(player_pos.x, player_pos.y)
	queue_redraw()

func _on_comp_input_text_changed(new_text):
	if selected_tile.x != -1:
		sim.set_tile_composition(selected_tile.x, selected_tile.y, int(new_text))
		_update_ui() # Sync checkboxes
		queue_redraw()

func _on_impassable_check_toggled(button_pressed):
	if selected_tile.x != -1:
		sim.set_impassable(selected_tile.x, selected_tile.y, button_pressed)
		sim.run_scent_update(player_pos.x, player_pos.y)
		queue_redraw()

func _refresh_map_list():
	%MapDropdown.clear()
	var dir = DirAccess.open("user://")
	if not dir.dir_exists("maps"):
		dir.make_dir("maps")
	
	dir = DirAccess.open("user://maps")
	if dir:
		dir.list_dir_begin()
		var file_name = dir.get_next()
		while file_name != "":
			if not dir.current_is_dir() and file_name.ends_with(".json"):
				%MapDropdown.add_item(file_name)
			file_name = dir.get_next()

func _on_save_map_as_pressed():
	var map_name = %MapNameInput.text
	if map_name == "":
		print("Error: No map name provided")
		return
	
	if not map_name.ends_with(".json"):
		map_name += ".json"
		
	var data = {
		"width": sim.map_width,
		"height": sim.map_height,
		"player": {"x": player_pos.x, "y": player_pos.y},
		"tiles": []
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
	
	var file = FileAccess.open("user://maps/" + map_name, FileAccess.WRITE)
	file.store_string(JSON.stringify(data))
	print("Saved to user://maps/" + map_name)
	_refresh_map_list()

func _on_load_map_pressed():
	var idx = %MapDropdown.selected
	if idx == -1:
		print("Error: No map selected")
		return
		
	var map_name = %MapDropdown.get_item_text(idx)
	%MapNameInput.text = map_name.replace(".json", "") # Set text for easy saving
	var file = FileAccess.open("user://maps/" + map_name, FileAccess.READ)
	if not file:
		print("Error: Could not open map " + map_name)
		return
		
	var data = JSON.parse_string(file.get_as_text())
	
	sim.generate_new_world(0) # Clear
	player_pos = Vector2i(data.player.x, data.player.y)
	
	for t in data.tiles:
		sim.set_tile_composition(t.x, t.z, t.comp)
		sim.set_impassable(t.x, t.z, t.imp)
		sim.set_tile_effect(t.x, t.z, int(t.eff))
	
	sim.run_scent_update(player_pos.x, player_pos.y)
	queue_redraw()

# func _on_save_pressed():
# 	var data = {
# 		"width": sim.map_width,
# 		"height": sim.map_height,
# 		"player": {"x": player_pos.x, "y": player_pos.y},
# 		"tiles": []
# 	}
# 	for z in range(sim.map_height):
# 		for x in range(sim.map_width):
# 			var tile = {
# 				"x": x, "z": z,
# 				"comp": sim.get_tile_composition(x, z),
# 				"imp": sim.is_impassable(x, z),
# 				"eff": sim.get_tile_effects(x, z)
# 			}
# 			if tile.comp != 0 or tile.imp or tile.eff != 0:
# 				data.tiles.append(tile)
# 	
# 	var file = FileAccess.open("user://grid_save.json", FileAccess.WRITE)
# 	file.store_string(JSON.stringify(data))
# 	print("Saved to user://grid_save.json")

# func _on_load_pressed():
# 	if not FileAccess.file_exists("user://grid_save.json"): return
# 	var file = FileAccess.open("user://grid_save.json", FileAccess.READ)
# 	var data = JSON.parse_string(file.get_as_text())
# 	
# 	sim.generate_new_world(0) # Clear
# 	player_pos = Vector2i(data.player.x, data.player.y)
# 	
# 	for t in data.tiles:
# 		sim.set_tile_composition(t.x, t.z, t.comp)
# 		sim.set_impassable(t.x, t.z, t.imp)
# 		sim.set_tile_effect(t.x, t.z, int(t.eff))
# 	
# 	sim.run_scent_update(player_pos.x, player_pos.y)
# 	queue_redraw()

func _on_toggle_scent_toggled(button_pressed):
	show_scent = button_pressed
	queue_redraw()

func _on_toggle_biome_toggled(button_pressed):
	show_biome = button_pressed
	queue_redraw()

func _on_toggle_impassable_toggled(button_pressed):
	show_impassable = button_pressed
	queue_redraw()

func _on_load_definitions_pressed():
	effect_dropdown.clear()
	biome_names.clear()
	_load_definitions()
	_update_ui()
	print("Definitions reloaded.")

func _draw():
	if not sim: return
	
	var width = sim.map_width
	var height = sim.map_height
	
	for z in range(height):
		for x in range(width):
			var rect = Rect2(x * tile_size, z * tile_size, tile_size, tile_size)
			
			# Draw Biome
			if show_biome:
				var comp = sim.get_tile_composition(x, z)
				var color = _get_biome_color(comp)
				draw_rect(rect, color)
			else:
				draw_rect(rect, Color.DARK_GRAY, false)
				
			# Draw Impassable
			if show_impassable and sim.is_impassable(x, z):
				draw_rect(rect.grow(-2), Color.BLACK, false, 2.0)
				
			# Draw Effects as small balls
			var effects = sim.get_tile_effects(x, z)
			if effects != 0:
				var center = rect.get_center()
				var angle = 0.0
				var count = 0
				for i in range(16): # Check first 16 bits for visualization
					if effects & (1 << i):
						count += 1
				
				for i in range(16):
					if effects & (1 << i):
						var ball_pos = center
						if count > 1:
							var radius = 5.0
							ball_pos += Vector2(cos(angle), sin(angle)) * radius
							angle += (2.0 * PI) / count
						
						var ball_color = _get_mana_color(1 << i)
						draw_circle(ball_pos, 3, ball_color)
				
			# Draw Scent
			if show_scent:
				var scent = sim.get_scent(x, z)
				if scent > 0:
					var scent_color = Color(0.8, 0, 0.8, 0.2)
					draw_rect(rect, scent_color)
					draw_string($CanvasLayer/Control.get_theme_default_font(), rect.position + Vector2(2, 14), str(scent), HORIZONTAL_ALIGNMENT_LEFT, -1, 10)

			# Draw Grid Lines
			draw_rect(rect, Color(0.2, 0.2, 0.2, 0.5), false)

	# Draw Player
	var player_rect = Rect2(player_pos.x * tile_size, player_pos.y * tile_size, tile_size, tile_size)
	draw_rect(player_rect, Color.YELLOW, false, 2.0)
	
	# Draw Selection
	if selected_tile.x != -1:
		var sel_rect = Rect2(selected_tile.x * tile_size, selected_tile.y * tile_size, tile_size, tile_size)
		draw_rect(sel_rect, Color.WHITE, false, 1.0)

func _get_mana_color(bit: int) -> Color:
	match bit:
		1: return Color.BLUE    # W
		2: return Color.RED     # F
		4: return Color(0.6, 0.4, 0.2) # E
		8: return Color.GREEN   # Pl
		16: return Color.WHITE   # Pu
		32: return Color(0.2, 0.1, 0.3) # R
		64: return Color.GOLD    # Mi
		128: return Color.CYAN   # Ma
	return Color.GRAY

func _get_biome_color(comp: int) -> Color:
	if comp == 0: return Color(0.3, 0.4, 0.2) # Plains (Grass Green)
	
	var color = Color(0, 0, 0)
	var count = 0
	
	# Bit 0: Water (W=1) - Blue
	if comp & 1:
		color += Color.BLUE
		count += 1
	# Bit 1: Fire (F=2) - Red
	if comp & 2:
		color += Color.RED
		count += 1
	# Bit 2: Earth (E=4) - Brown
	if comp & 4:
		color += Color(0.6, 0.4, 0.2)
		count += 1
	# Bit 3: Plant (Pl=8) - Green
	if comp & 8:
		color += Color.GREEN
		count += 1
	# Bit 4: Purity (Pu=16) - White
	if comp & 16:
		color += Color.WHITE
		count += 1
	# Bit 5: Ruin (R=32) - Dark Gray/Black
	if comp & 32:
		color += Color(0.2, 0.1, 0.3)
		count += 1
	# Bit 6: Mind (Mi=64) - Pink/Gold
	if comp & 64:
		color += Color.GOLD
		count += 1
	# Bit 7: Magic (Ma=128) - Cyan/Teal
	if comp & 128:
		color += Color.CYAN
		count += 1
		
	if count > 0:
		return color / count
	return Color.BLACK
