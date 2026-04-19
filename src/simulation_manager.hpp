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
    std::vector<uint64_t> propagation_buffer;
    std::vector<int> active_tiles; // Indices of tiles with effects
    std::vector<bool> is_active_map; // Bitmask for fast index existence check
    int map_width = 32;
    int map_height = 32;

    void _mark_tile_active(int index);

    // LUTs for Branchless ALU and Physics
    uint64_t annihilation_matrix[64];
    uint64_t chemistry_pairs[64][64];
    uint8_t flammability_lut[256];
    uint8_t biome_transitions[256][64]; // Maps [BiomeID][EffectBitIndex] -> NewBiomeID

    // Propagation Rules
    struct PropagationRule {
        bool active = false;
        bool check_flammability = false;
        bool check_elevation = false;
        uint64_t bit = 0;
    };
    PropagationRule propagation_rules[64];

    void update_scent(int player_x, int player_z);
    void _resolve_internal_alu(Tile &tile);
    void _resolve_transitions(Tile &tile);
    void _process_propagation_sparse(const std::vector<int>& active_indices);
    void _apply_propagation();
    void _inject_biome_effects();

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
    void set_flammable(int biome_id, bool flammable);
    void set_propagation_rule(int bit_index, bool check_flammable, bool check_elevation);
    
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
