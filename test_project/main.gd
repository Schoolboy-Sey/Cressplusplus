extends Node2D

func _ready() -> void:
	test_scent()
	
func test_scent()->void:
	var sim: SimulationManager = SimulationManager.new()
	sim.generate_new_world(42)
	
	print("--- Test 1: Open Field Scent ---")
	sim.run_scent_update(5, 5)
	print("Scent at (5, 5): ", sim.get_scent(5, 5))
	print("Scent at (6, 5): ", sim.get_scent(6, 5))
	print("Scent at (10, 5): ", sim.get_scent(10, 5))
	
	print("\n--- Test 2: Wall Obstacle Scent ---")
	# Wall at x=10, z=10..19
	sim.run_scent_update(5, 15)
	print("Scent at (5, 15): ", sim.get_scent(5, 15))
	print("Scent at (9, 15): ", sim.get_scent(9, 15))
	print("Scent at (10, 15) [Wall]: ", sim.get_scent(10, 15))
	print("Scent at (11, 15) [Behind Wall]: ", sim.get_scent(11, 15))
	
	print("\n--- Test 3: U-Shaped Trap ---")
	# Create a U-trap around (20, 20) opening to the right
	# Top wall
	for x in range(15, 21): sim.set_impassable(x, 15, true)
	# Bottom wall
	for x in range(15, 21): sim.set_impassable(x, 25, true)
	# Left wall
	for z in range(15, 26): sim.set_impassable(15, z, true)
	
	# Player inside the trap
	sim.run_scent_update(18, 20)
	print("Scent at (18, 20) [Inside]: ", sim.get_scent(18, 20))
	print("Scent at (14, 20) [Behind Left Wall]: ", sim.get_scent(14, 20))
	print("Scent at (22, 20) [Outside Opening]: ", sim.get_scent(22, 20))
	
	print("\nFull Scent Map (Zoomed 15x15 around trap):")
	var map_str = ""
	for z in range(13, 28):
		var line = ""
		for x in range(13, 28):
			if sim.is_impassable(x, z):
				line += " # "
			else:
				var s = sim.get_scent(x, z)
				if s == 0: line += " . "
				else: line += "%2d " % s
		map_str += line + "\n"
	print(map_str)
