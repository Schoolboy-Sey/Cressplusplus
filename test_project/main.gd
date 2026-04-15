extends Node2D

func _ready() -> void:
	test_scent()
	
func test_scent()->void:
	var sim: SimulationManager = SimulationManager.new()
	sim.generate_new_world(42)
	
	# Player at (5, 5)
	sim.run_scent_update(5, 5)
	print("Scent at (5, 5): ", sim.get_scent(5, 5))
	print("Scent at (6, 5): ", sim.get_scent(6, 5))
	print("Scent at (10, 5): ", sim.get_scent(10, 5))
	
	# Player near wall at x=10, z=10..19
	sim.run_scent_update(5, 15)
	print("Scent at (5, 15): ", sim.get_scent(5, 15))
	print("Scent at (9, 15): ", sim.get_scent(9, 15))
	print("Scent at (10, 15) [Wall]: ", sim.get_scent(10, 15))
	print("Scent at (11, 15) [Behind Wall]: ", sim.get_scent(11, 15))
