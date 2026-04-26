#include "simulation_manager.hpp"
#include "godot_cpp/core/class_db.hpp"
#include <queue>
#include <cstring>
#include <immintrin.h>
#include <algorithm>

using namespace godot;

static inline int ctz64(uint64_t mask) {
#ifdef _MSC_VER
    unsigned long where; if (_BitScanForward64(&where, mask)) return (int)where; return 64;
#else
    return mask == 0 ? 64 : __builtin_ctzll(mask);
#endif
}

static inline int popcount8(uint8_t v) {
#ifdef _MSC_VER
    return (int)__popcnt16(v);
#else
    return __builtin_popcount(v);
#endif
}

void SimulationManager::_bind_methods() {
    ClassDB::bind_method(D_METHOD("run_step"), &SimulationManager::run_step);
    ClassDB::bind_method(D_METHOD("generate_new_world", "seed"), &SimulationManager::generate_new_world);
    ClassDB::bind_method(D_METHOD("save_state_snapshot"), &SimulationManager::save_state_snapshot);
    ClassDB::bind_method(D_METHOD("load_state_snapshot"), &SimulationManager::load_state_snapshot);
    ClassDB::bind_method(D_METHOD("process_ai_intents"), &SimulationManager::process_ai_intents);
    ClassDB::bind_method(D_METHOD("auto_update_scent"), &SimulationManager::auto_update_scent);
    ClassDB::bind_method(D_METHOD("run_scent_update", "x", "z"), &SimulationManager::run_scent_update);
    ClassDB::bind_method(D_METHOD("set_tile_composition", "x", "z", "composition"), &SimulationManager::set_tile_composition);
    ClassDB::bind_method(D_METHOD("get_tile_composition", "x", "z"), &SimulationManager::get_tile_composition);
    ClassDB::bind_method(D_METHOD("set_tile_mana", "x", "z", "mask"), &SimulationManager::set_tile_mana);
    ClassDB::bind_method(D_METHOD("get_tile_mana", "x", "z"), &SimulationManager::get_tile_mana);
    ClassDB::bind_method(D_METHOD("set_tile_effect", "x", "z", "effect_bit"), &SimulationManager::set_tile_effect);
    ClassDB::bind_method(D_METHOD("get_tile_effects", "x", "z"), &SimulationManager::get_tile_effects);
    ClassDB::bind_method(D_METHOD("set_impassable", "x", "z", "impassable"), &SimulationManager::set_impassable);
    ClassDB::bind_method(D_METHOD("is_impassable", "x", "z"), &SimulationManager::is_impassable);
    ClassDB::bind_method(D_METHOD("spawn_unit", "x", "z", "team", "weight"), &SimulationManager::spawn_unit);
    ClassDB::bind_method(D_METHOD("spawn_unit_full", "x", "z", "team", "weight", "velocity", "flags"), &SimulationManager::spawn_unit_full, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("despawn_unit", "entity_id"), &SimulationManager::despawn_unit);
    ClassDB::bind_method(D_METHOD("move_unit_intent", "entity_id", "target_x", "target_z"), &SimulationManager::move_unit_intent);
    ClassDB::bind_method(D_METHOD("get_unit_at", "x", "z"), &SimulationManager::get_unit_at);
    ClassDB::bind_method(D_METHOD("get_unit_pos", "entity_id"), &SimulationManager::get_unit_pos);
    ClassDB::bind_method(D_METHOD("get_unit_intent_pos", "entity_id"), &SimulationManager::get_unit_intent_pos);
    ClassDB::bind_method(D_METHOD("get_unit_team", "entity_id"), &SimulationManager::get_unit_team);
    ClassDB::bind_method(D_METHOD("get_unit_weight", "entity_id"), &SimulationManager::get_unit_weight);
    ClassDB::bind_method(D_METHOD("get_unit_velocity", "entity_id"), &SimulationManager::get_unit_velocity);
    ClassDB::bind_method(D_METHOD("get_unit_flags", "entity_id"), &SimulationManager::get_unit_flags);
    ClassDB::bind_method(D_METHOD("set_unit_flags", "entity_id", "flags"), &SimulationManager::set_unit_flags);
    ClassDB::bind_method(D_METHOD("get_unit_diet", "entity_id"), &SimulationManager::get_unit_diet);
    ClassDB::bind_method(D_METHOD("set_unit_diet", "entity_id", "diet"), &SimulationManager::set_unit_diet);
    ClassDB::bind_method(D_METHOD("get_unit_sated_timer", "entity_id"), &SimulationManager::get_unit_sated_timer);
    ClassDB::bind_method(D_METHOD("get_all_units"), &SimulationManager::get_all_units);
    ClassDB::bind_method(D_METHOD("get_scent", "x", "z"), &SimulationManager::get_scent);
    ClassDB::bind_method(D_METHOD("get_scent_map_string"), &SimulationManager::get_scent_map_string);
    ClassDB::bind_method(D_METHOD("get_grid_data"), &SimulationManager::get_grid_data);
    ClassDB::bind_method(D_METHOD("clear_interaction_tables"), &SimulationManager::clear_interaction_tables);
    ClassDB::bind_method(D_METHOD("add_annihilation", "bit_a", "bit_b"), &SimulationManager::add_annihilation);
    ClassDB::bind_method(D_METHOD("add_chemistry", "bit_a", "bit_b", "result_stack"), &SimulationManager::add_chemistry);
    ClassDB::bind_method(D_METHOD("add_biome_transition", "biome_id", "effect_bit", "result_biome_id"), &SimulationManager::add_biome_transition);
    ClassDB::bind_method(D_METHOD("set_flammable", "biome_id", "flammable"), &SimulationManager::set_flammable);
    ClassDB::bind_method(D_METHOD("set_biome_weight", "biome_id", "weight"), &SimulationManager::set_biome_weight);
    ClassDB::bind_method(D_METHOD("set_effect_weight", "bit_index", "weight"), &SimulationManager::set_effect_weight);
    ClassDB::bind_method(D_METHOD("set_propagation_rule", "bit_index", "check_flammable", "check_elevation"), &SimulationManager::set_propagation_rule);
    ClassDB::bind_method(D_METHOD("set_propagation_interval", "bit_index", "interval"), &SimulationManager::set_propagation_interval);
    ClassDB::bind_method(D_METHOD("add_evolution_path", "current_species", "mana_bit", "result_species"), &SimulationManager::add_evolution_path);
    ClassDB::bind_method(D_METHOD("set_species_stats", "species_id", "weight", "velocity"), &SimulationManager::set_species_stats);
    ClassDB::bind_method(D_METHOD("get_map_width"), &SimulationManager::get_map_width);
    ClassDB::bind_method(D_METHOD("get_map_height"), &SimulationManager::get_map_height);

    BIND_ENUM_CONSTANT(FLAG_BOUNCE);
    BIND_ENUM_CONSTANT(FLAG_PUSH);
    BIND_ENUM_CONSTANT(FLAG_FLYING);
    BIND_ENUM_CONSTANT(FLAG_HERBIVORE);
    BIND_ENUM_CONSTANT(FLAG_CARNIVORE);

    ADD_PROPERTY(PropertyInfo(Variant::INT, "map_width"), "", "get_map_width");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "map_height"), "", "get_map_height");
}

SimulationManager::SimulationManager() {
    int size = map_width * map_height; grid.resize(size); propagation_buffer.assign(size, 0); 
    is_active_map.assign(size, 0); is_dirty_map.assign(size, 0);
    active_tiles.reserve(size / 10); dirty_propagation_indices.reserve(size / 10);
    wavefront_current.reserve(size); wavefront_next.reserve(size);
    entity_coordinate.resize(MAX_ENTITIES); entity_intent.resize(MAX_ENTITIES); entity_flags.assign(MAX_ENTITIES, 0);
    entity_diet_mask.assign(MAX_ENTITIES, 0); entity_base_weight.resize(MAX_ENTITIES); entity_species.assign(MAX_ENTITIES, 0);
    entity_diet.assign(MAX_ENTITIES, 0); entity_mutation.assign(MAX_ENTITIES, 0);
    entity_tier.assign(MAX_ENTITIES, 0); entity_heading.assign(MAX_ENTITIES, 0); entity_sated_timer.assign(MAX_ENTITIES, 0);
    entity_velocity.resize(MAX_ENTITIES); entity_team.resize(MAX_ENTITIES);
    entity_active.assign(MAX_ENTITIES, 0); unit_grid.assign(size, EMPTY_TILE);
    for (int i = MAX_ENTITIES - 1; i >= 0; --i) available_entities.push_back(i);
    memset(evolution_paths, 0, sizeof(evolution_paths)); memset(species_base_weight, 0, sizeof(species_base_weight)); memset(species_base_velocity, 0, sizeof(species_base_velocity));
    clear_interaction_tables();
}
SimulationManager::~SimulationManager() {}

void SimulationManager::save_state_snapshot() {
    snapshot_grid = grid; snapshot_entity_coordinate = entity_coordinate; snapshot_entity_intent = entity_intent; 
    snapshot_entity_flags = entity_flags; snapshot_entity_base_weight = entity_base_weight;
    snapshot_entity_species = entity_species; snapshot_entity_active = entity_active; snapshot_unit_grid = unit_grid;
}

void SimulationManager::load_state_snapshot() {
    if (snapshot_grid.empty()) return;
    grid = snapshot_grid; entity_coordinate = snapshot_entity_coordinate; entity_intent = snapshot_entity_intent;
    entity_flags = snapshot_entity_flags; entity_base_weight = snapshot_entity_base_weight;
    entity_species = snapshot_entity_species; entity_active = snapshot_entity_active; unit_grid = snapshot_unit_grid;
    active_tiles.clear(); std::fill(is_active_map.begin(), is_active_map.end(), 0); available_entities.clear(); active_entity_indices.clear();
    for (int i = 0; i < (int)grid.size(); ++i) if (grid[i].effect_stack != 0) _mark_tile_active(i);
    for (int i = MAX_ENTITIES - 1; i >= 0; --i) {
        if (!entity_active[i]) available_entities.push_back(i);
        else active_entity_indices.push_back((uint16_t)i);
    }
}

void SimulationManager::auto_update_scent() {
    for (auto &tile : grid) tile.set_scent(0);
    wavefront_current.clear();
    for (uint16_t i : active_entity_indices) if (entity_team[i] == 0) { int idx = entity_coordinate[i]; grid[idx].set_scent(15); wavefront_current.push_back(idx); }
    if (wavefront_current.empty()) return;
    int current_scent = 15;
    while (!wavefront_current.empty() && current_scent > 0) {
        wavefront_next.clear();
        for (int idx : wavefront_current) {
            int tx = idx % map_width, tz = idx / map_width;
            int dx[] = {0,0,1,-1}, dz[] = {1,-1,0,0};
            for (int d=0; d<4; ++d) {
                int nx = tx+dx[d], nz = tz+dz[d];
                if (nx>=0 && nx<map_width && nz>=0 && nz<map_height) {
                    int n_idx = nz*map_width+nx; Tile &n = grid[n_idx];
                    if (!n.is_impassable() && n.get_scent() < current_scent-1) { n.set_scent(current_scent-1); wavefront_next.push_back(n_idx); }
                }
            }
        }
        wavefront_current = wavefront_next;
        current_scent--;
    }
}

int SimulationManager::get_unit_diet(int id) const { return (id >= 0 && id < MAX_ENTITIES && entity_active[id]) ? entity_diet[id] : 0; }
void SimulationManager::set_unit_diet(int id, int diet) { 
    if (id >= 0 && id < MAX_ENTITIES && entity_active[id]) {
        entity_diet[id] = (uint8_t)diet;
        uint16_t mask = 0; for(int b=0; b<8; ++b) { if(diet & (1<<b)) mask |= (3 << (b*2)); }
        entity_diet_mask[id] = mask;
    }
}
int SimulationManager::get_unit_sated_timer(int id) const { return (id >= 0 && id < MAX_ENTITIES && entity_active[id]) ? (int)entity_sated_timer[id] : 0; }

void SimulationManager::process_ai_intents() {
    for (uint16_t i : active_entity_indices) {
        if (entity_team[i] != 1) continue; 
        int cur = entity_coordinate[i];
        if (entity_sated_timer[i] > 0) { entity_intent[i] = cur; continue; }

        uint32_t flags = entity_flags[i];
        uint8_t diet = entity_diet[i];
        uint16_t dm = entity_diet_mask[i];

        // --- STAY IF ON FOOD ---
        uint8_t food_here = grid[cur].ambient_mana | grid[cur].composition;
        if (food_here & diet) { entity_intent[i] = cur; continue; }

        uint16_t imprint = grid[cur].imprint_field & dm;
        int best = cur;
        bool found_wave = false;

        // --- O(1) PHASE ALIGNMENT NAVIGATION ---
        uint16_t target_state = 0;
        uint16_t pulse_mask = dm & 0xAAAA;
        uint16_t trail_mask = dm & 0x5555;

        if (imprint & pulse_mask) target_state = trail_mask;      // On Pulse -> look for Trail
        else if (imprint & trail_mask) target_state = 0;           // On Trail -> look for Empty
        else target_state = pulse_mask;                            // On Empty -> look for Pulse

        int cx = cur % map_width, cz = cur / map_width;
        int dx[] = {0,0,1,-1}, dz[] = {1,-1,0,0};
        for (int d = 0; d < 4; ++d) {
            int nx = cx + dx[d], nz = cz + dz[d];
            if (nx >= 0 && nx < map_width && nz >= 0 && nz < map_height) {
                int n_idx = nz * map_width + nx;
                uint16_t n_imprint = grid[n_idx].imprint_field & dm;
                
                // Prioritize Source neighbor (Source has both bits set for at least one diet type)
                if ((n_imprint & pulse_mask) && (n_imprint & trail_mask)) {
                    best = n_idx; found_wave = true; break;
                }
                
                if (n_imprint == target_state) { 
                    best = n_idx; found_wave = true; 
                    // Don't break yet, keep looking for a Source if possible
                }
            }
        }

        if (!found_wave) {
            if (flags & FLAG_CARNIVORE) {
                int best_s = -1;
                for (int d = 0; d < 4; ++d) {
                    int nx = cx + dx[d], nz = cz + dz[d];
                    if (nx >= 0 && nx < map_width && nz >= 0 && nz < map_height) {
                        int n_idx = nz * map_width + nx;
                        int occupant = get_unit_at(nx, nz);
                        if (occupant != -1 && entity_team[occupant] != entity_team[i]) { best = n_idx; break; }
                        int n_scent = grid[n_idx].get_scent();
                        if (n_scent > best_s && !grid[n_idx].is_impassable() && unit_grid[n_idx] == EMPTY_TILE) { best_s = n_scent; best = n_idx; }
                    }
                }
            } else {
                int best_s = grid[cur].get_scent();
                for (int d = 0; d < 4; ++d) {
                    int nx = cx + dx[d], nz = cz + dz[d];
                    if (nx >= 0 && nx < map_width && nz >= 0 && nz < map_height) {
                        int n_idx = nz * map_width + nx;
                        if (!grid[n_idx].is_impassable() && unit_grid[n_idx] == EMPTY_TILE) {
                            int n_scent = grid[n_idx].get_scent();
                            if (n_scent > best_s) { best_s = n_scent; best = n_idx; }
                        }
                    }
                }
            }
        }
        entity_intent[i] = best;
    }
}

void SimulationManager::run_scent_update(int x, int z) { update_scent(x, z); }
void SimulationManager::update_scent(int x, int z) {
    for (auto &tile : grid) tile.set_scent(0);
    if (x < 0 || x >= map_width || z < 0 || z >= map_height) return;
    wavefront_current.clear();
    int idx = z * map_width + x; grid[idx].set_scent(15); wavefront_current.push_back(idx);
    int current_scent = 15;
    while (!wavefront_current.empty() && current_scent > 0) {
        wavefront_next.clear();
        for (int c_idx : wavefront_current) {
            int tx = c_idx % map_width, tz = c_idx / map_width;
            int dx[] = {0,0,1,-1}, dz[] = {1,-1,0,0};
            for (int d=0; d<4; ++d) {
                int nx = tx+dx[d], nz = tz+dz[d];
                if (nx>=0 && nx<map_width && nz>=0 && nz<map_height) {
                    int n_idx = nz*map_width+nx; Tile &n = grid[n_idx];
                    if (!n.is_impassable() && n.get_scent() < current_scent-1) {
                        n.set_scent(current_scent-1); wavefront_next.push_back(n_idx);
                    }
                }
            }
        }
        wavefront_current = wavefront_next;
        current_scent--;
    }
}

void SimulationManager::run_step() {
    step_count++;
    std::vector<int> current_active = active_tiles;
    active_tiles.clear(); std::fill(is_active_map.begin(), is_active_map.end(), 0);

    // --- STEP 1: Double Buffer Injection ---
    if (step_count % 10 == 0) for (int i = 0; i < (int)grid.size(); ++i) grid[i].ambient_mana |= grid[i].composition;

    // --- STEP 3: ECS & Kinetic Clashes ---
    _resolve_movement_and_clashes();

    // --- STEP 4: Shadow Buffer CA ---
    _process_imprint_waves_simd();

    // --- STEP 5: Branchless ALU ---
    for (int index : current_active) _resolve_internal_alu(grid[index]);

    // --- STEP 6: Biological Reaction & Evolution ---
    for (uint16_t i : active_entity_indices) {
        if (entity_sated_timer[i] > 0) { entity_sated_timer[i]--; continue; }
        int cur = entity_coordinate[i]; uint8_t diet = entity_diet[i];
        if (diet == 0) continue;
        
        uint8_t food_source = grid[cur].ambient_mana | grid[cur].composition;
        if (food_source & diet) {
            int m_idx = ctz64(food_source & diet);
            if (grid[cur].ambient_mana & (1 << m_idx)) grid[cur].ambient_mana &= ~(1 << m_idx);
            else { grid[cur].composition &= ~(1 << m_idx); _mark_tile_active(cur); }
            
            entity_sated_timer[i] = 10;
            uint8_t curr_spec = entity_species[i];
            uint8_t next_spec = evolution_paths[curr_spec][m_idx];

            // Branchless Mutation/Evolution
            uint8_t evo_mask = -static_cast<uint8_t>(next_spec != 0);
            uint8_t mut_mask = ~evo_mask;

            entity_species[i] = (next_spec & evo_mask) | (curr_spec & mut_mask);
            uint8_t mut_stack = entity_mutation[i] | (1 << m_idx);
            entity_mutation[i] = mut_stack & mut_mask;

            uint8_t old_t = entity_tier[i], new_t = popcount8(entity_mutation[i]);
            entity_tier[i] = new_t;

            uint8_t t_diff = new_t - old_t;
            uint8_t ew = species_base_weight[next_spec], mw = entity_base_weight[i] + (20 * t_diff);
            int8_t  ev = species_base_velocity[next_spec], mv = entity_velocity[i] - t_diff;

            entity_base_weight[i] = (ew & evo_mask) | (mw & mut_mask);
            entity_velocity[i]    = (ev & evo_mask) | (mv & mut_mask);
        }
    }

    // --- STEP 7: World Mutation ---
    _process_propagation_sparse(current_active);
    for (int index : current_active) _resolve_transitions(grid[index]);
    _apply_propagation();
}

void SimulationManager::_process_imprint_waves_simd() {
    memset(shadow_buffer, 0, sizeof(shadow_buffer));
    for (int z = 0; z < map_height; ++z) {
        for (int x = 0; x < map_width; ++x) {
            int g_idx = z * map_width + x, s_idx = (z + 1) * SHADOW_WIDTH + (x + 1);
            uint8_t src_mask = grid[g_idx].composition | grid[g_idx].ambient_mana;
            uint16_t src_bits = 0;
            for (int b = 0; b < 8; ++b) if (src_mask & (1 << b)) src_bits |= (3 << (b * 2));
            shadow_buffer[s_idx] = grid[g_idx].imprint_field | src_bits;
        }
    }
    __m256i m_h = _mm256_set1_epi16(0xAAAA), m_l = _mm256_set1_epi16(0x5555);
    for (int z = 1; z <= map_height; ++z) {
        for (int xc = 0; xc < 3; ++xc) {
            int i = z * SHADOW_WIDTH + (xc * 16);
            __m256i cur = _mm256_load_si256((__m256i*)&shadow_buffer[i]);
            __m256i u = _mm256_loadu_si256((__m256i*)&shadow_buffer[i - SHADOW_WIDTH]);
            __m256i d = _mm256_loadu_si256((__m256i*)&shadow_buffer[i + SHADOW_WIDTH]);
            __m256i l = _mm256_loadu_si256((__m256i*)&shadow_buffer[i - 1]);
            __m256i r = _mm256_loadu_si256((__m256i*)&shadow_buffer[i + 1]);
            __m256i uh = _mm256_and_si256(u, m_h), dh = _mm256_and_si256(d, m_h), lh = _mm256_and_si256(l, m_h), rh = _mm256_and_si256(r, m_h);
            __m256i any_h = _mm256_or_si256(_mm256_or_si256(uh, dh), _mm256_or_si256(lh, rh));
            __m256i p1 = _mm256_or_si256(_mm256_and_si256(uh, dh), _mm256_and_si256(lh, rh));
            __m256i p2 = _mm256_and_si256(_mm256_or_si256(uh, dh), _mm256_or_si256(lh, rh));
            __m256i multi = _mm256_or_si256(p1, p2), ex1 = _mm256_andnot_si256(multi, any_h);
            __m256i h = _mm256_and_si256(cur, m_h), low = _mm256_and_si256(cur, m_l), lah = _mm256_slli_epi16(low, 1), src = _mm256_and_si256(h, lah);
            __m256i nh = _mm256_andnot_si256(_mm256_or_si256(h, lah), ex1);
            nh = _mm256_or_si256(nh, src);
            __m256i nl = _mm256_srli_epi16(_mm256_andnot_si256(lah, h), 1);
            nl = _mm256_or_si256(nl, _mm256_srli_epi16(src, 1));
            _mm256_store_si256((__m256i*)&result_buffer[i], _mm256_or_si256(nh, nl));
        }
    }
    for (int z = 0; z < map_height; ++z) {
        for (int x = 0; x < map_width; ++x) {
            int g_idx = z * map_width + x, r_idx = (z + 1) * SHADOW_WIDTH + (x + 1);
            uint16_t res = result_buffer[r_idx]; grid[g_idx].imprint_field = res;
            uint16_t h_b = res & 0xAAAA; uint8_t pm = 0;
            for (int b=0; b<8; ++b) if (h_b & (2 << (b*2))) pm |= (1 << b);
            grid[g_idx].ambient_mana |= (grid[g_idx].composition & pm);
        }
    }
}

void SimulationManager::_resolve_movement_and_clashes() {
    int max_c = 1; for (uint16_t i : active_entity_indices) if ((int)entity_velocity[i] > max_c) max_c = (int)entity_velocity[i];
    for (int cycle = 0; cycle < max_c; ++cycle) {
        for (uint16_t i : active_entity_indices) {
            if (cycle > 0 && cycle >= (int)entity_velocity[i]) continue;
            uint32_t cur = entity_coordinate[i], target = entity_intent[i]; if (cur == target) continue;
            int cx = cur % map_width, cz = cur / map_width, tx = target % map_width, tz = target / map_width;
            int dx = (tx > cx) ? 1 : (tx < cx ? -1 : 0), dz = (tz > cz) ? 1 : (tz < cz ? -1 : 0);
            uint32_t next = (cz + dz) * map_width + (cx + dx); uint16_t occ = unit_grid[next];
            if (occ == EMPTY_TILE && !grid[next].is_impassable()) {
                unit_grid[cur] = EMPTY_TILE; grid[cur].effect_stack &= ~Tile::FLAG_HAS_ENTITY;
                entity_coordinate[i] = next; unit_grid[next] = (uint16_t)i; grid[next].effect_stack |= Tile::FLAG_HAS_ENTITY;
                _mark_tile_active(next); _mark_tile_active(cur);
            } else if (occ != EMPTY_TILE) {
                int af = (int)entity_base_weight[i] + (int)entity_velocity[i];
                if (entity_team[occ] != entity_team[i]) {
                    int df = (int)entity_base_weight[occ] + (int)entity_velocity[occ];
                    if (af > df) {
                        _despawn_entity_internal(occ); unit_grid[cur] = EMPTY_TILE; grid[cur].effect_stack &= ~Tile::FLAG_HAS_ENTITY;
                        entity_coordinate[i] = next; unit_grid[next] = (uint16_t)i; grid[next].effect_stack |= Tile::FLAG_HAS_ENTITY;
                        _mark_tile_active(next); _mark_tile_active(cur);
                    } else if (entity_flags[i] & FLAG_PUSH) {
                        int pdx = dx, pdz = dz, ptx = (next % map_width) + pdx, ptz = (next / map_width) + pdz;
                        if (ptx >= 0 && ptx < map_width && ptz >= 0 && ptz < map_height) {
                            uint32_t p_idx = ptz * map_width + ptx; int bw = grid[p_idx].is_impassable() ? 255 : (int)biome_weights[grid[p_idx].composition];
                            if (af + bw > df) {
                                _despawn_entity_internal(occ); unit_grid[cur] = EMPTY_TILE; grid[cur].effect_stack &= ~Tile::FLAG_HAS_ENTITY;
                                entity_coordinate[i] = next; unit_grid[next] = (uint16_t)i; grid[next].effect_stack |= Tile::FLAG_HAS_ENTITY;
                                _mark_tile_active(next); _mark_tile_active(cur);
                            } else if (unit_grid[p_idx] == EMPTY_TILE && !grid[p_idx].is_impassable()) {
                                unit_grid[next] = EMPTY_TILE; grid[next].effect_stack &= ~Tile::FLAG_HAS_ENTITY;
                                entity_coordinate[occ] = p_idx; unit_grid[p_idx] = (uint16_t)occ; entity_intent[occ] = p_idx;
                                grid[p_idx].effect_stack |= Tile::FLAG_HAS_ENTITY; _mark_tile_active(p_idx); _mark_tile_active(next);
                            }
                        }
                        entity_intent[i] = cur;
                    } else entity_intent[i] = cur;
                } else entity_intent[i] = cur;
            } else entity_intent[i] = cur;
        }
    }
}

void SimulationManager::generate_new_world(int seed) {
    grid.assign(map_width * map_height, Tile{0, 0, 0, 0, 0, 0, 0, 0});
    for (auto &tile : grid) tile.set_countdown(7);
    active_tiles.clear(); is_active_map.assign(map_width * map_height, 0);
    dirty_propagation_indices.clear(); is_dirty_map.assign(map_width * map_height, 0);
    propagation_buffer.assign(map_width * map_height, 0); step_count = 0;
    available_entities.clear(); active_entity_indices.clear();
    for (int i = MAX_ENTITIES - 1; i >= 0; --i) available_entities.push_back(i);
    entity_active.assign(MAX_ENTITIES, 0); unit_grid.assign(map_width * map_height, EMPTY_TILE);
}

int SimulationManager::spawn_unit_full(int x, int z, int team, int weight, int velocity, int flags) {
    if (available_entities.empty() || x < 0 || x >= map_width || z < 0 || z >= map_height) return -1;
    int idx = z * map_width + x; if (unit_grid[idx] != EMPTY_TILE) return -1;
    int id = available_entities.back(); available_entities.pop_back();
    entity_active[id] = 1; entity_coordinate[id] = idx; entity_intent[id] = idx;
    entity_team[id] = (uint8_t)team; entity_base_weight[id] = (uint8_t)weight; 
    entity_velocity[id] = (int8_t)velocity; entity_flags[id] = (uint32_t)flags;
    active_entity_indices.push_back((uint16_t)id);
    unit_grid[idx] = (uint16_t)id; grid[idx].effect_stack |= Tile::FLAG_HAS_ENTITY; _mark_tile_active(idx); return id;
}

void SimulationManager::despawn_unit(int id) { if (id >= 0 && id < MAX_ENTITIES && entity_active[id]) _despawn_entity_internal(id); }
void SimulationManager::_despawn_entity_internal(int id) {
    int idx = entity_coordinate[id]; unit_grid[idx] = EMPTY_TILE; grid[idx].effect_stack &= ~Tile::FLAG_HAS_ENTITY;
    entity_active[id] = 0; available_entities.push_back(id);
    active_entity_indices.erase(std::remove(active_entity_indices.begin(), active_entity_indices.end(), (uint16_t)id), active_entity_indices.end());
    _mark_tile_active(idx);
}
void SimulationManager::move_unit_intent(int id, int tx, int tz) { if (id >= 0 && id < MAX_ENTITIES && entity_active[id] && tx >= 0 && tx < map_width && tz >= 0 && tz < map_height) entity_intent[id] = tz * map_width + tx; }

int SimulationManager::get_unit_at(int x, int z) const { if (x < 0 || x >= map_width || z < 0 || z >= map_height) return -1; uint16_t id = unit_grid[z * map_width + x]; return id == EMPTY_TILE ? -1 : (int)id; }
Vector2i SimulationManager::get_unit_pos(int id) const { return (id >= 0 && id < MAX_ENTITIES && entity_active[id]) ? Vector2i(entity_coordinate[id] % map_width, entity_coordinate[id] / map_width) : Vector2i(-1, -1); }
Vector2i SimulationManager::get_unit_intent_pos(int id) const { return (id >= 0 && id < MAX_ENTITIES && entity_active[id]) ? Vector2i(entity_intent[id] % map_width, entity_intent[id] / map_width) : Vector2i(-1, -1); }
int SimulationManager::get_unit_team(int id) const { return (id >= 0 && id < MAX_ENTITIES && entity_active[id]) ? entity_team[id] : -1; }
int SimulationManager::get_unit_weight(int id) const { return (id >= 0 && id < MAX_ENTITIES && entity_active[id]) ? entity_base_weight[id] : 0; }
int SimulationManager::get_unit_velocity(int id) const { return (id >= 0 && id < MAX_ENTITIES && entity_active[id]) ? entity_velocity[id] : 0; }
int SimulationManager::get_unit_flags(int id) const { return (id >= 0 && id < MAX_ENTITIES && entity_active[id]) ? entity_flags[id] : 0; }
void SimulationManager::set_unit_flags(int id, int f) { if (id >= 0 && id < MAX_ENTITIES && entity_active[id]) entity_flags[id] = (uint32_t)f; }
Dictionary SimulationManager::get_all_units() const { Dictionary d; for (uint16_t i : active_entity_indices) d[i] = get_unit_pos(i); return d; }

void SimulationManager::set_biome_weight(int id, int w) { if (id >= 0 && id < 256) biome_weights[id] = (uint8_t)w; }
void SimulationManager::set_effect_weight(int b, int w) { if (b >= 0 && b < 64) effect_weights[b] = (int8_t)w; }
void SimulationManager::set_propagation_rule(int i, bool f, bool e) { if (i >= 0 && i < 56) { propagation_rules[i].active = true; propagation_rules[i].check_flammable = f; propagation_rules[i].check_elevation = e; propagation_rules[i].bit = 1ULL << i; } }
void SimulationManager::set_propagation_interval(int i, int iv) { if (i >= 0 && i < 56) propagation_rules[i].spread_interval = iv > 0 ? iv : 1; }
void SimulationManager::add_evolution_path(int cs, int mb, int rs) { if (cs < 256 && mb < 8) evolution_paths[cs][mb] = (uint8_t)rs; }
void SimulationManager::set_species_stats(int sid, int w, int v) { if (sid < 256) { species_base_weight[sid] = (uint8_t)w; species_base_velocity[sid] = (int8_t)v; } }
void SimulationManager::_mark_tile_active(int idx) { if (idx >= 0 && idx < (int)is_active_map.size() && !is_active_map[idx]) { is_active_map[idx] = 1; active_tiles.push_back(idx); } }
void SimulationManager::_push_to_buffer(int idx, uint64_t m) { if (idx >= 0 && idx < (int)propagation_buffer.size()) { propagation_buffer[idx] |= m; if (!is_dirty_map[idx]) { is_dirty_map[idx] = 1; dirty_propagation_indices.push_back(idx); } } }

int SimulationManager::get_scent(int x, int z) const { return (x >= 0 && x < map_width && z >= 0 && z < map_height) ? grid[z * map_width + x].get_scent() : 0; }
PackedByteArray SimulationManager::get_grid_data() const { PackedByteArray d; d.resize((int)grid.size() * sizeof(Tile)); memcpy(d.ptrw(), grid.data(), grid.size() * sizeof(Tile)); return d; }
String SimulationManager::get_scent_map_string() const {
    String res = ""; for (int z=0; z<map_height; ++z) { for (int x=0; x<map_width; ++x) { if (is_impassable(x,z)) res+=" # "; else { int s = get_scent(x,z); if (s==0) res+=" . "; else { char buf[4]; snprintf(buf,sizeof(buf),"%2d ",s); res+=buf; } } } res+="\n"; } return res;
}

void SimulationManager::clear_interaction_tables() {
    memset(annihilation_matrix, 0, sizeof(annihilation_matrix)); memset(chemistry_pairs, 0, sizeof(chemistry_pairs)); memset(flammability_lut, 0, sizeof(flammability_lut));
    for (int i=0; i<64; ++i) propagation_rules[i] = PropagationRule();
    for (int b=0; b<256; ++b) for (int e=0; e<64; ++e) biome_transitions[b][e] = (uint8_t)b;
}
void SimulationManager::set_tile_composition(int x, int z, int c) { if (x >= 0 && x < map_width && z >= 0 && z < map_height) grid[z * map_width + x].composition = (uint8_t)c; }
int SimulationManager::get_tile_composition(int x, int z) const { return (x >= 0 && x < map_width && z >= 0 && z < map_height) ? (int)grid[z * map_width + x].composition : 0; }
void SimulationManager::set_tile_mana(int x, int z, int m) { if (x >= 0 && x < map_width && z >= 0 && z < map_height) grid[z * map_width + x].ambient_mana = (uint8_t)m; }
int SimulationManager::get_tile_mana(int x, int z) const { return (x >= 0 && x < map_width && z >= 0 && z < map_height) ? (int)grid[z * map_width + x].ambient_mana : 0; }
void SimulationManager::set_tile_effect(int x, int z, uint64_t e) { if (x >= 0 && x < map_width && z >= 0 && z < map_height) { int idx = z * map_width + x; grid[idx].effect_stack |= e; if (e & 0x00FFFFFFFFFFFFFFULL) grid[idx].set_countdown(7); _mark_tile_active(idx); } }
uint64_t SimulationManager::get_tile_effects(int x, int z) const { return (x >= 0 && x < map_width && z >= 0 && z < map_height) ? grid[z * map_width + x].effect_stack : (uint64_t)0; }
void SimulationManager::set_impassable(int x, int z, bool i) { if (x >= 0 && x < map_width && z >= 0 && z < map_height) { int idx = z * map_width + x; if (i) grid[idx].pathing_scent |= Tile::FLAG_HAZARD | Tile::FLAG_IMPASSABLE; else grid[idx].pathing_scent &= ~(Tile::FLAG_HAZARD | Tile::FLAG_IMPASSABLE); } }
bool SimulationManager::is_impassable(int x, int z) const { return (x >= 0 && x < map_width && z >= 0 && z < map_height) ? grid[z * map_width + x].is_impassable() : false; }
void SimulationManager::add_annihilation(int bit_a, int bit_b) { if (bit_a < 64 && bit_b < 64) { annihilation_matrix[bit_a] |= (1ULL << bit_b); annihilation_matrix[bit_b] |= (1ULL << bit_a); } }
void SimulationManager::add_chemistry(int bit_a, int bit_b, uint64_t result_stack) { if (bit_a < 64 && bit_b < 64) { chemistry_pairs[bit_a][bit_b] = result_stack; chemistry_pairs[bit_b][bit_a] = result_stack; } }
void SimulationManager::add_biome_transition(int biome_id, int effect_bit, int result_biome_id) { if (effect_bit < 64 && biome_id < 256) biome_transitions[biome_id][effect_bit] = (uint8_t)result_biome_id; }
void SimulationManager::set_flammable(int biome_id, bool flammable) { if (biome_id >= 0 && biome_id < 256) flammability_lut[biome_id] = flammable ? 0xFF : 0x00; }

int SimulationManager::spawn_unit(int x, int z, int team, int weight) { return spawn_unit_full(x, z, team, weight, (team == 0) ? 3 : 2); }

void SimulationManager::_resolve_internal_alu(Tile &tile) {
    uint64_t rem = tile.effect_stack, it = tile.effect_stack;
    while (it != 0) {
        int b = ctz64(it); uint64_t bm = 1ULL << b;
        if (rem & bm) {
            uint64_t t = annihilation_matrix[b], ah = rem & t;
            uint64_t hm = -static_cast<int64_t>(ah != 0);
            rem &= ~(ah & hm); rem &= ~(bm & hm);
        }
        it &= ~(1ULL << b);
    }
    it = rem;
    while (it != 0) {
        int ba = ctz64(it); uint64_t mask_a = 1ULL << ba;
        if (rem & mask_a) {
            uint64_t inner_mask = rem & ~mask_a;
            while (inner_mask != 0) {
                int bb = ctz64(inner_mask); uint64_t mask_b = 1ULL << bb; uint64_t r = chemistry_pairs[ba][bb];
                if (r != 0) { rem &= ~mask_a; rem &= ~mask_b; rem |= r; break; }
                inner_mask &= ~mask_b;
            }
        }
        it &= ~mask_a;
    }
    if (rem & 0x00FFFFFFFFFFFFFFULL) {
        uint8_t count = tile.get_countdown(); if (count > 0) { count--; tile.set_countdown(count); rem = (rem & 0x00FFFFFFFFFFFFFFULL) | (tile.effect_stack & 0xFF00000000000000ULL); }
    }
    tile.effect_stack = rem;
}

void SimulationManager::_resolve_transitions(Tile &tile) {
    uint8_t countdown = tile.get_countdown(); uint64_t it = tile.effect_stack & 0x00FFFFFFFFFFFFFFULL;
    while (it != 0) { int b = ctz64(it); tile.composition = (countdown == 0) ? biome_transitions[tile.composition][b] : tile.composition; it &= ~(1ULL << b); }
    tile.effect_stack &= (0xFFULL << 56); if (countdown == 0) tile.set_countdown(7);
}

void SimulationManager::_process_propagation_sparse(const std::vector<int>& active_indices) {
    for (int idx : active_indices) {
        uint64_t st = grid[idx].effect_stack; if (st == 0) continue;
        int x = idx % map_width, z = idx / map_width; uint8_t el = grid[idx].absolute_elevation, cd = grid[idx].get_countdown(); uint64_t it = st;
        while (it != 0) {
            int b_i = ctz64(it); uint64_t b_m = 1ULL << b_i; if (b_i >= 56) { it &= ~b_m; continue; }
            const PropagationRule &r = propagation_rules[b_i];
            if (r.active) {
                uint8_t cp = grid[idx].composition;
                if ((flammability_lut[cp] != 0) && (cd > 0 || biome_transitions[cp][b_i] == cp)) _push_to_buffer(idx, b_m);
                if ((step_count % r.spread_interval == 0) && (cd <= 3)) {
                    uint8_t f_m = r.check_flammable ? 0xFF : 0x00, e_m = r.check_elevation ? 0xFF : 0x00;
                    auto sp = [&](int nx, int nz) {
                        if (nx>=0 && nx<map_width && nz>=0 && nz<map_height) {
                            int n_i = nz*map_width+nx;
                            uint8_t res_f = (flammability_lut[grid[n_i].composition] & f_m) | ~f_m;
                            uint8_t res_e = ((grid[n_i].absolute_elevation <= el ? 0xFF : 0x00) & e_m) | ~e_m;
                            if ((res_f & res_e) != 0) _push_to_buffer(n_i, b_m);
                        }
                    };
                    sp(x+1,z); sp(x-1,z); sp(x,z+1); sp(x,z-1);
                }
            }
            it &= ~b_m;
        }
    }
}

void SimulationManager::_apply_propagation() {
    for (int idx : dirty_propagation_indices) {
        uint64_t pb = propagation_buffer[idx]; if (pb != 0) { grid[idx].effect_stack |= pb; _mark_tile_active(idx); }
        propagation_buffer[idx] = 0; is_dirty_map[idx] = 0;
    }
    dirty_propagation_indices.clear();
}
