#pragma once

#include "godot_cpp/classes/node.hpp"
#include "godot_cpp/variant/packed_byte_array.hpp"
#include "tile.hpp"
#include <vector>

namespace godot {

class SimulationManager : public Node {
    GDCLASS(SimulationManager, Node)

protected:
    static void _bind_methods();

private:
    std::vector<Tile> grid;
    int map_width = 32;
    int map_height = 32;

    // LUTs for Branchless ALU
    uint64_t annihilation_matrix[64];
    uint64_t chemistry_pairs[64][64];
    uint8_t biome_transitions[256][64]; // Maps [BiomeID][EffectBitIndex] -> NewBiomeID

    void update_scent(int player_x, int player_z);
    void process_tile_alu(Tile &tile);

public:
    SimulationManager();
    ~SimulationManager();

    void run_step();
    void generate_new_world(int seed);
    
    void set_tile_composition(int x, int z, uint8_t composition);
    uint8_t get_tile_composition(int x, int z) const;
    
    void set_tile_effect(int x, int z, uint64_t effect_bit);
    uint64_t get_tile_effects(int x, int z) const;
    
    void set_impassable(int x, int z, bool impassable);
    bool is_impassable(int x, int z) const;
    
    // Configuration methods for human-readable definitions
    void clear_interaction_tables();
    void add_annihilation(int bit_a, int bit_b);
    void add_chemistry(int bit_a, int bit_b, uint64_t result_stack);
    void add_biome_transition(uint8_t biome_id, int effect_bit, uint8_t result_biome_id);
    
    int get_map_width() const { return map_width; }
    int get_map_height() const { return map_height; }
    
    // For smellnet verification
    int get_scent(int x, int z) const;
    void run_scent_update(int player_x, int player_z);
    String get_scent_map_string() const;
    
    // Expose the grid data to Godot as a PackedByteArray
    PackedByteArray get_grid_data() const;
};

} // namespace godot
