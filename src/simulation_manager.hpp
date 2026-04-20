#pragma once

#include "godot_cpp/classes/node.hpp"
#include "godot_cpp/variant/packed_byte_array.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/vector2i.hpp"
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
    std::vector<int> active_tiles;
    std::vector<bool> is_active_map;
    std::vector<int> dirty_propagation_indices;
    std::vector<bool> is_dirty_map;
    
    static const int MAX_ENTITIES = 4096;
    std::vector<uint32_t> entity_coordinate;
    std::vector<uint32_t> entity_intent;
    std::vector<uint32_t> entity_flags;
    std::vector<uint8_t>  entity_base_weight;
    std::vector<int8_t>   entity_velocity;
    std::vector<uint8_t>  entity_team;
    std::vector<uint16_t> entity_generation;
    std::vector<bool>     entity_active;
    
    std::vector<int>      available_entities;
    std::vector<int>      unit_grid;

    std::vector<Tile>     snapshot_grid;
    std::vector<uint32_t> snapshot_entity_coordinate;
    std::vector<uint32_t> snapshot_entity_intent;
    std::vector<uint32_t> snapshot_entity_flags;
    std::vector<uint8_t>  snapshot_entity_base_weight;
    std::vector<int8_t>   snapshot_entity_velocity;
    std::vector<uint8_t>  snapshot_entity_team;
    std::vector<uint16_t> snapshot_entity_generation;
    std::vector<bool>     snapshot_entity_active;
    std::vector<int>      snapshot_unit_grid;

    int map_width = 32;
    int map_height = 32;
    uint32_t step_count = 0;

    void _mark_tile_active(int index);
    void _push_to_buffer(int index, uint64_t mask);

    uint64_t annihilation_matrix[64];
    uint64_t chemistry_pairs[64][64];
    uint8_t flammability_lut[256];
    uint8_t biome_transitions[256][64];
    uint8_t biome_weights[256];
    int8_t  effect_weights[64];

    struct PropagationRule {
        bool active = false;
        bool check_flammable = false;
        bool check_elevation = false;
        int spread_interval = 1;
        uint64_t bit = 0;
    };
    PropagationRule propagation_rules[64];

    void _resolve_internal_alu(Tile &tile);
    void _resolve_transitions(Tile &tile);
    void _process_propagation_sparse(const std::vector<int>& active_indices);
    void _apply_propagation();
    void _inject_biome_effects();
    
    void _resolve_movement_and_clashes();
    void _despawn_entity_internal(int entity_id);

public:
    enum EntityFlags {
        FLAG_BOUNCE = 1 << 0,
        FLAG_PUSH   = 1 << 1,
        FLAG_FLYING = 1 << 2,
    };

    SimulationManager();
    ~SimulationManager();

    void run_step();
    void generate_new_world(int seed);
    
    void save_state_snapshot();
    void load_state_snapshot();
    void process_ai_intents();
    void auto_update_scent();

    void set_tile_composition(int x, int z, int composition);
    int get_tile_composition(int x, int z) const;
    void set_tile_effect(int x, int z, uint64_t effect_bit);
    uint64_t get_tile_effects(int x, int z) const;
    void set_impassable(int x, int z, bool impassable);
    bool is_impassable(int x, int z) const;
    
    int spawn_unit(int x, int z, int team, int weight);
    int spawn_unit_full(int x, int z, int team, int weight, int velocity, int flags = 0);
    void despawn_unit(int entity_id);
    void move_unit_intent(int entity_id, int target_x, int target_z);
    
    int get_unit_at(int x, int z) const;
    Vector2i get_unit_pos(int entity_id) const;
    Vector2i get_unit_intent_pos(int entity_id) const;
    int get_unit_team(int entity_id) const;
    int get_unit_weight(int entity_id) const;
    int get_unit_velocity(int entity_id) const;
    int get_unit_flags(int entity_id) const;
    void set_unit_flags(int entity_id, int flags);
    Dictionary get_all_units() const;

    void clear_interaction_tables();
    void add_annihilation(int bit_a, int bit_b);
    void add_chemistry(int bit_a, int bit_b, uint64_t result_stack);
    void add_biome_transition(int biome_id, int effect_bit, int result_biome_id);
    void set_flammable(int biome_id, bool flammable);
    void set_biome_weight(int biome_id, int weight);
    void set_effect_weight(int bit_index, int weight);
    void set_propagation_rule(int bit_index, bool check_flammable, bool check_elevation);
    void set_propagation_interval(int bit_index, int interval);

    
    int get_map_width() const { return map_width; }
    int get_map_height() const { return map_height; }
    
    int get_scent(int x, int z) const;
    String get_scent_map_string() const;
    PackedByteArray get_grid_data() const;
};

}

VARIANT_ENUM_CAST(godot::SimulationManager::EntityFlags);
