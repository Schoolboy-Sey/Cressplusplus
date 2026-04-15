extends Node2D

var sim: SimulationManager
var tile_size: int = 20
var show_scent: bool = true
var show_biome: bool = true
var show_impassable: bool = true

var selected_tile: Vector2i = Vector2i(-1, -1)
var player_pos: Vector2i = Vector2i(5, 5)

var definitions = {}
var effect_names = {}
var biome_names = {}

@onready var ui_panel = $CanvasLayer/Control/Panel
@onready var comp_input = $CanvasLayer/Control/Panel/VBoxContainer/CompInput
@onready var impassable_check = $CanvasLayer/Control/Panel/VBoxContainer/ImpassableCheck
@onready var effect_dropdown = $CanvasLayer/Control/Panel/VBoxContainer/EffectDropdown

func _ready():
	sim = SimulationManager.new()
	add_child(sim)
	_load_definitions()
	sim.generate_new_world(42)
	sim.run_scent_update(player_pos.x, player_pos.y)
	queue_redraw()

func _load_definitions():
	var file = FileAccess.open("res://definitions.json", FileAccess.READ)
	if file:
		var json = JSON.parse_string(file.get_as_text())
		if json:
			definitions = json
			sim.clear_interaction_tables()
			
			# Map Biomes
			for b in definitions.biomes:
				biome_names[b.id] = b.name
				
			# Map Effects
			for e in definitions.effects:
				effect_names[e.bit] = e.name
				effect_dropdown.add_item(e.name, e.bit)
				
			# Map Interactions
			for i in definitions.interactions:
				if i.type == "annihilation":
					var bit_a = _get_effect_bit(i.a)
					var bit_b = _get_effect_bit(i.b)
					sim.add_annihilation(_get_bit_index(bit_a), _get_bit_index(bit_b))
				elif i.type == "chemistry":
					var bit_a = _get_effect_bit(i.a)
					var bit_b = _get_effect_bit(i.b)
					var res_bit = _get_effect_bit(i.result)
					sim.add_chemistry(_get_bit_index(bit_a), _get_bit_index(bit_b), res_bit)
			
			# Map Transitions
			for t in definitions.transitions:
				var b_id = _get_biome_id(t.biome)
				var e_bit = _get_effect_bit(t.effect)
				var res_b = _get_biome_id(t.result)
				sim.add_biome_transition(b_id, _get_bit_index(e_bit), res_b)

func _get_effect_bit(name: String) -> int:
	for e in definitions.effects:
		if e.name == name: return e.bit
	return 0

func _get_bit_index(bit: int) -> int:
	for i in range(64):
		if bit == (1 << i): return i
	return 0

func _get_biome_id(name: String) -> int:
	for b in definitions.biomes:
		if b.name == name: return b.id
	return 0

func _input(event):
	if event is InputEventMouseButton and event.pressed:
		var grid_pos = Vector2i(event.position / tile_size)
		if grid_pos.x >= 0 and grid_pos.x < sim.map_width and grid_pos.y >= 0 and grid_pos.y < sim.map_height:
			if event.button_index == MOUSE_BUTTON_LEFT:
				selected_tile = grid_pos
				_update_ui()
			elif event.button_index == MOUSE_BUTTON_RIGHT:
				if Input.is_key_pressed(KEY_SHIFT):
					var effect_bit = effect_dropdown.get_selected_id()
					sim.set_tile_effect(grid_pos.x, grid_pos.y, effect_bit)
				else:
					player_pos = grid_pos
					sim.run_scent_update(player_pos.x, player_pos.y)
				queue_redraw()

func _update_ui():
	if selected_tile.x != -1:
		ui_panel.show()
		comp_input.text = str(sim.get_tile_composition(selected_tile.x, selected_tile.y))
		impassable_check.button_pressed = sim.is_impassable(selected_tile.x, selected_tile.y)
	else:
		ui_panel.hide()

func _on_run_step_pressed():
	sim.run_step()
	sim.run_scent_update(player_pos.x, player_pos.y)
	queue_redraw()

func _on_comp_input_text_changed(new_text):
	if selected_tile.x != -1:
		sim.set_tile_composition(selected_tile.x, selected_tile.y, int(new_text))
		queue_redraw()

func _on_impassable_check_toggled(button_pressed):
	if selected_tile.x != -1:
		sim.set_impassable(selected_tile.x, selected_tile.y, button_pressed)
		sim.run_scent_update(player_pos.x, player_pos.y)
		queue_redraw()

func _on_toggle_scent_toggled(button_pressed):
	show_scent = button_pressed
	queue_redraw()

func _on_toggle_biome_toggled(button_pressed):
	show_biome = button_pressed
	queue_redraw()

func _on_toggle_impassable_toggled(button_pressed):
	show_impassable = button_pressed
	queue_redraw()

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
				
			# Draw Effects
			var effects = sim.get_tile_effects(x, z)
			if effects != 0:
				draw_rect(rect.grow(-4), Color.ORANGE, false, 1.0)
				
			# Draw Scent
			if show_scent:
				var scent = sim.get_scent(x, z)
				if scent > 0:
					var scent_color = Color(0.8, 0, 0.8, 0.3) # Faint purple mist
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
