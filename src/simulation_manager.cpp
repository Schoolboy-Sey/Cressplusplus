#include "simulation_manager.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include <queue>
#include <cstring> // For memset and memcpy
#include <immintrin.h> // For intrinsics

using namespace godot;

#ifdef _MSC_VER
#include <intrin.h>
static inline int __builtin_ctzll(unsigned __int64 mask) {
    unsigned long where;
    if (_BitScanForward64(&where, mask)) {
        return (int)where;
    }
    return 64;
}
#endif

void SimulationManager::_bind_methods() {
    ClassDB::bind_method(D_METHOD("run_step"), &SimulationManager::run_step);
    ClassDB::bind_method(D_METHOD("generate_new_world", "seed"), &SimulationManager::generate_new_world);
    
    ClassDB::bind_method(D_METHOD("set_tile_composition", "x", "z", "composition"), &SimulationManager::set_tile_composition);
    ClassDB::bind_method(D_METHOD("get_tile_composition", "x", "z"), &SimulationManager::get_tile_composition);
    
    ClassDB::bind_method(D_METHOD("set_tile_effect", "x", "z", "effect_bit"), &SimulationManager::set_tile_effect);
    ClassDB::bind_method(D_METHOD("get_tile_effects", "x", "z"), &SimulationManager::get_tile_effects);

    ClassDB::bind_method(D_METHOD("set_impassable", "x", "z", "impassable"), &SimulationManager::set_impassable);
    ClassDB::bind_method(D_METHOD("is_impassable", "x", "z"), &SimulationManager::is_impassable);
    
    ClassDB::bind_method(D_METHOD("get_scent", "x", "z"), &SimulationManager::get_scent);
    ClassDB::bind_method(D_METHOD("run_scent_update", "player_x", "player_z"), &SimulationManager::run_scent_update);
    ClassDB::bind_method(D_METHOD("get_grid_data"), &SimulationManager::get_grid_data);
    
    ClassDB::bind_method(D_METHOD("clear_interaction_tables"), &SimulationManager::clear_interaction_tables);
    ClassDB::bind_method(D_METHOD("add_annihilation", "bit_a", "bit_b"), &SimulationManager::add_annihilation);
    ClassDB::bind_method(D_METHOD("add_chemistry", "bit_a", "bit_b", "result_stack"), &SimulationManager::add_chemistry);
    ClassDB::bind_method(D_METHOD("add_biome_transition", "biome_id", "effect_bit", "result_biome_id"), &SimulationManager::add_biome_transition);

    ClassDB::bind_method(D_METHOD("get_map_width"), &SimulationManager::get_map_width);
    ClassDB::bind_method(D_METHOD("get_map_height"), &SimulationManager::get_map_height);

    ADD_PROPERTY(PropertyInfo(Variant::INT, "map_width"), "", "get_map_width");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "map_height"), "", "get_map_height");
}

SimulationManager::SimulationManager() {
    grid.resize(map_width * map_height);
    clear_interaction_tables();
}

SimulationManager::~SimulationManager() {}

void SimulationManager::run_step() {
    for (auto &tile : grid) {
        if (tile.effect_stack != 0) {
            process_tile_alu(tile);
        }
    }
}

void SimulationManager::process_tile_alu(Tile &tile) {
    uint64_t remaining_stack = tile.effect_stack;
    uint64_t iterator_mask = tile.effect_stack;

    // Phase 1: Annihilation
    while (iterator_mask != 0) {
        int bit = __builtin_ctzll(iterator_mask);
        uint64_t bit_mask = 1ULL << bit;

        if (remaining_stack & bit_mask) {
            uint64_t targets = annihilation_matrix[bit];
            uint64_t actual_hits = remaining_stack & targets;

            if (actual_hits != 0) {
                remaining_stack &= ~actual_hits;
                remaining_stack &= ~bit_mask;
            }
        }
        iterator_mask &= ~(1ULL << bit);
    }

    // Phase 2: Chemistry
    iterator_mask = remaining_stack;
    while (iterator_mask != 0) {
        int bit_a = __builtin_ctzll(iterator_mask);
        uint64_t mask_a = 1ULL << bit_a;

        if (remaining_stack & mask_a) {
            uint64_t inner_mask = remaining_stack & ~mask_a;
            while (inner_mask != 0) {
                int bit_b = __builtin_ctzll(inner_mask);
                uint64_t mask_b = 1ULL << bit_b;

                uint64_t result = chemistry_pairs[bit_a][bit_b];
                if (result != 0) {
                    remaining_stack &= ~mask_a;
                    remaining_stack &= ~mask_b;
                    remaining_stack |= result;
                    break;
                }
                inner_mask &= ~mask_b;
            }
        }
        iterator_mask &= ~mask_a;
    }

    // Phase 3: Biome Transitions
    iterator_mask = remaining_stack;
    while (iterator_mask != 0) {
        int bit = __builtin_ctzll(iterator_mask);
        uint8_t next_biome = biome_transitions[tile.composition][bit];
        if (next_biome != tile.composition) {
            tile.composition = next_biome;
        }
        iterator_mask &= ~(1ULL << bit);
    }

    // WIPE THE STACK
    tile.effect_stack = 0;
}

void SimulationManager::generate_new_world(int seed) {
    grid.assign(map_width * map_height, Tile{0, 0, 0, 0, 0, 0, {0, 0, 0}});
    for (int i = 0; i < 10; ++i) {
        set_impassable(10, 10 + i, true);
    }
}

void SimulationManager::set_tile_composition(int x, int z, uint8_t composition) {
    if (x >= 0 && x < map_width && z >= 0 && z < map_height) {
        grid[z * map_width + x].composition = composition;
    }
}

uint8_t SimulationManager::get_tile_composition(int x, int z) const {
    if (x >= 0 && x < map_width && z >= 0 && z < map_height) {
        return grid[z * map_width + x].composition;
    }
    return 0;
}

void SimulationManager::set_tile_effect(int x, int z, uint64_t effect_bit) {
    if (x >= 0 && x < map_width && z >= 0 && z < map_height) {
        grid[z * map_width + x].effect_stack |= effect_bit;
    }
}

uint64_t SimulationManager::get_tile_effects(int x, int z) const {
    if (x >= 0 && x < map_width && z >= 0 && z < map_height) {
        return grid[z * map_width + x].effect_stack;
    }
    return 0;
}

void SimulationManager::set_impassable(int x, int z, bool impassable) {
    if (x >= 0 && x < map_width && z >= 0 && z < map_height) {
        int index = z * map_width + x;
        if (impassable) {
            grid[index].pathing_scent |= Tile::FLAG_IMPASSABLE;
        } else {
            grid[index].pathing_scent &= ~Tile::FLAG_IMPASSABLE;
        }
    }
}

bool SimulationManager::is_impassable(int x, int z) const {
    if (x >= 0 && x < map_width && z >= 0 && z < map_height) {
        return grid[z * map_width + x].is_impassable();
    }
    return false;
}

void SimulationManager::clear_interaction_tables() {
    memset(annihilation_matrix, 0, sizeof(annihilation_matrix));
    memset(chemistry_pairs, 0, sizeof(chemistry_pairs));
    for (int b = 0; b < 256; ++b) {
        for (int e = 0; e < 64; ++e) {
            biome_transitions[b][e] = (uint8_t)b;
        }
    }
}

void SimulationManager::add_annihilation(int bit_a, int bit_b) {
    if (bit_a < 64 && bit_b < 64) {
        annihilation_matrix[bit_a] |= (1ULL << bit_b);
        annihilation_matrix[bit_b] |= (1ULL << bit_a);
    }
}

void SimulationManager::add_chemistry(int bit_a, int bit_b, uint64_t result_stack) {
    if (bit_a < 64 && bit_b < 64) {
        chemistry_pairs[bit_a][bit_b] = result_stack;
        chemistry_pairs[bit_b][bit_a] = result_stack;
    }
}

void SimulationManager::add_biome_transition(uint8_t biome_id, int effect_bit, uint8_t result_biome_id) {
    if (effect_bit < 64) {
        biome_transitions[biome_id][effect_bit] = result_biome_id;
    }
}

int SimulationManager::get_scent(int x, int z) const {
    if (x >= 0 && x < map_width && z >= 0 && z < map_height) {
        return grid[z * map_width + x].get_scent();
    }
    return 0;
}

void SimulationManager::run_scent_update(int player_x, int player_z) {
    update_scent(player_x, player_z);
}

void SimulationManager::update_scent(int player_x, int player_z) {
    for (auto &tile : grid) {
        tile.set_scent(0);
    }
    if (player_x < 0 || player_x >= map_width || player_z < 0 || player_z >= map_height) return;
    std::queue<int> wavefront_queue;
    int player_index = player_z * map_width + player_x;
    grid[player_index].set_scent(15);
    wavefront_queue.push(player_index);
    int current_scent = 15;
    while (!wavefront_queue.empty() && current_scent > 0) {
        int current_frontier_size = wavefront_queue.size();
        for (int i = 0; i < current_frontier_size; ++i) {
            int tile_index = wavefront_queue.front();
            wavefront_queue.pop();
            int tx = tile_index % map_width;
            int tz = tile_index / map_width;
            int dx[] = {0, 0, 1, -1};
            int dz[] = {1, -1, 0, 0};
            for (int d = 0; d < 4; ++d) {
                int nx = tx + dx[d];
                int nz = tz + dz[d];
                if (nx >= 0 && nx < map_width && nz >= 0 && nz < map_height) {
                    int neighbor_index = nz * map_width + nx;
                    Tile &neighbor = grid[neighbor_index];
                    if (!neighbor.is_impassable()) {
                        if (neighbor.get_scent() < current_scent - 1) {
                            neighbor.set_scent(current_scent - 1);
                            wavefront_queue.push(neighbor_index);
                        }
                    }
                }
            }
        }
        current_scent--;
    }
}

PackedByteArray SimulationManager::get_grid_data() const {
    PackedByteArray data;
    data.resize(grid.size() * sizeof(Tile));
    uint8_t *ptr = data.ptrw();
    memcpy(ptr, grid.data(), grid.size() * sizeof(Tile));
    return data;
}
