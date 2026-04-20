#include "simulation_manager.hpp"
#include "godot_cpp/core/class_db.hpp"
#include <queue>
#include <cstring>
#include <immintrin.h>

using namespace godot;

static inline int ctz64(uint64_t mask) {
#ifdef _MSC_VER
    unsigned long where; if (_BitScanForward64(&where, mask)) return (int)where; return 64;
#else
    return mask == 0 ? 64 : __builtin_ctzll(mask);
#endif
}

void SimulationManager::_bind_methods() {
    ClassDB::bind_method(D_METHOD("run_step"), &SimulationManager::run_step);
    ClassDB::bind_method(D_METHOD("generate_new_world", "seed"), &SimulationManager::generate_new_world);
    ClassDB::bind_method(D_METHOD("save_state_snapshot"), &SimulationManager::save_state_snapshot);
    ClassDB::bind_method(D_METHOD("load_state_snapshot"), &SimulationManager::load_state_snapshot);
    ClassDB::bind_method(D_METHOD("process_ai_intents"), &SimulationManager::process_ai_intents);
    ClassDB::bind_method(D_METHOD("auto_update_scent"), &SimulationManager::auto_update_scent);
    ClassDB::bind_method(D_METHOD("set_tile_composition", "x", "z", "composition"), &SimulationManager::set_tile_composition);
    ClassDB::bind_method(D_METHOD("get_tile_composition", "x", "z"), &SimulationManager::get_tile_composition);
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
    ClassDB::bind_method(D_METHOD("get_map_width"), &SimulationManager::get_map_width);
    ClassDB::bind_method(D_METHOD("get_map_height"), &SimulationManager::get_map_height);

    BIND_ENUM_CONSTANT(FLAG_BOUNCE);
    BIND_ENUM_CONSTANT(FLAG_PUSH);
    BIND_ENUM_CONSTANT(FLAG_FLYING);

    ADD_PROPERTY(PropertyInfo(Variant::INT, "map_width"), "", "get_map_width");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "map_height"), "", "get_map_height");
}

SimulationManager::SimulationManager() {
    int size = map_width * map_height; grid.resize(size); propagation_buffer.assign(size, 0); is_active_map.assign(size, false); is_dirty_map.assign(size, false);
    active_tiles.reserve(size / 10); dirty_propagation_indices.reserve(size / 10);
    entity_coordinate.resize(MAX_ENTITIES); entity_intent.resize(MAX_ENTITIES); entity_flags.assign(MAX_ENTITIES, 0);
    entity_base_weight.resize(MAX_ENTITIES); entity_velocity.resize(MAX_ENTITIES); entity_team.resize(MAX_ENTITIES);
    entity_generation.assign(MAX_ENTITIES, 0); entity_active.assign(MAX_ENTITIES, false); unit_grid.assign(size, -1);
    for (int i = MAX_ENTITIES - 1; i >= 0; --i) available_entities.push_back(i);
    clear_interaction_tables();
}
SimulationManager::~SimulationManager() {}

void SimulationManager::save_state_snapshot() {
    snapshot_grid = grid; snapshot_entity_coordinate = entity_coordinate; snapshot_entity_intent = entity_intent; 
    snapshot_entity_flags = entity_flags; snapshot_entity_base_weight = entity_base_weight;
    snapshot_entity_velocity = entity_velocity; snapshot_entity_team = entity_team; snapshot_entity_generation = entity_generation; snapshot_entity_active = entity_active; snapshot_unit_grid = unit_grid;
}

void SimulationManager::load_state_snapshot() {
    if (snapshot_grid.empty()) return;
    grid = snapshot_grid; entity_coordinate = snapshot_entity_coordinate; entity_intent = snapshot_entity_intent;
    entity_flags = snapshot_entity_flags; entity_base_weight = snapshot_entity_base_weight;
    entity_velocity = snapshot_entity_velocity; entity_team = snapshot_entity_team; entity_generation = snapshot_entity_generation; entity_active = snapshot_entity_active; unit_grid = snapshot_unit_grid;
    active_tiles.clear(); std::fill(is_active_map.begin(), is_active_map.end(), false); available_entities.clear();
    for (int i = 0; i < (int)grid.size(); ++i) if (grid[i].effect_stack != 0) _mark_tile_active(i);
    for (int i = MAX_ENTITIES - 1; i >= 0; --i) if (!entity_active[i]) available_entities.push_back(i);
}

void SimulationManager::auto_update_scent() {
    for (auto &tile : grid) tile.set_scent(0);
    std::queue<int> wavefront_queue;
    for (int i = 0; i < MAX_ENTITIES; ++i) if (entity_active[i] && entity_team[i] == 0) { int idx = entity_coordinate[i]; grid[idx].set_scent(15); wavefront_queue.push(idx); }
    if (wavefront_queue.empty()) return;
    int current_scent = 15;
    while (!wavefront_queue.empty() && current_scent > 0) {
        int sz = (int)wavefront_queue.size();
        for (int i = 0; i < sz; ++i) {
            int idx = wavefront_queue.front(); wavefront_queue.pop();
            int tx = idx % map_width, tz = idx / map_width;
            int dx[] = {0,0,1,-1}, dz[] = {1,-1,0,0};
            for (int d=0; d<4; ++d) {
                int nx = tx+dx[d], nz = tz+dz[d];
                if (nx>=0 && nx<map_width && nz>=0 && nz<map_height) {
                    int n_idx = nz*map_width+nx; Tile &n = grid[n_idx];
                    if (!n.is_impassable() && n.get_scent() < current_scent-1) { n.set_scent(current_scent-1); wavefront_queue.push(n_idx); }
                }
            }
        }
        current_scent--;
    }
}

void SimulationManager::process_ai_intents() {
    for (int i = 0; i < MAX_ENTITIES; ++i) {
        if (!entity_active[i] || entity_team[i] != 1) continue; 
        int cur = entity_coordinate[i], cx = cur % map_width, cz = cur / map_width, best = cur, best_s = grid[cur].get_scent();
        int dx[] = {0,0,1,-1}, dz[] = {1,-1,0,0};
        for (int d=0; d<4; ++d) {
            int nx = cx+dx[d], nz = cz+dz[d];
            if (nx>=0 && nx<map_width && nz>=0 && nz<map_height) {
                int n_idx = nz*map_width+nx;
                const Tile& neighbor = grid[n_idx];
                int occupant = unit_grid[n_idx];
                
                if (!neighbor.is_impassable()) {
                    // AI targets tile if empty OR contains a unit from an opposing team
                    bool is_enemy = (occupant != -1 && entity_team[occupant] != entity_team[i]);
                    if (occupant == -1 || is_enemy) {
                        int n_scent = neighbor.get_scent();
                        if (n_scent > best_s) { best_s = n_scent; best = n_idx; }
                    }
                }
            }
        }
        entity_intent[i] = best;
    }
}

void SimulationManager::run_step() {
    step_count++; std::vector<int> current_active = active_tiles; active_tiles.clear(); std::fill(is_active_map.begin(), is_active_map.end(), false);
    _resolve_movement_and_clashes();
    for (int index : current_active) _resolve_internal_alu(grid[index]);
    _process_propagation_sparse(current_active);
    for (int index : current_active) _resolve_transitions(grid[index]);
    _inject_biome_effects(); _apply_propagation();
}

void SimulationManager::_resolve_movement_and_clashes() {
    int max_cycles = 1; 
    for (int i = 0; i < MAX_ENTITIES; ++i) if (entity_active[i] && (int)entity_velocity[i] > max_cycles) max_cycles = (int)entity_velocity[i];
    
    for (int cycle = 0; cycle < max_cycles; ++cycle) {
        for (int i = 0; i < MAX_ENTITIES; ++i) {
            if (!entity_active[i]) continue;
            if (cycle > 0 && cycle >= (int)entity_velocity[i]) continue;
            uint32_t cur = entity_coordinate[i], target = entity_intent[i];
            if (cur == target) continue;
            int cx = cur % map_width, cz = cur / map_width, tx = target % map_width, tz = target / map_width;
            int dx = (tx > cx) ? 1 : (tx < cx ? -1 : 0), dz = (tz > cz) ? 1 : (tz < cz ? -1 : 0);
            uint32_t next_step = (cz + dz) * map_width + (cx + dx);
            int occ = unit_grid[next_step];
            if (occ == -1 && !grid[next_step].is_impassable()) {
                unit_grid[cur] = -1; grid[cur].effect_stack &= ~Tile::FLAG_HAS_ENTITY;
                entity_coordinate[i] = next_step; unit_grid[next_step] = i;
                grid[next_step].effect_stack |= Tile::FLAG_HAS_ENTITY;
                _mark_tile_active(next_step); _mark_tile_active(cur);
            } else {
                int a_f = (int)entity_base_weight[i] + (int)entity_velocity[i];
                if (occ != -1 && entity_team[occ] != entity_team[i]) {
                    int d_f = (int)entity_base_weight[occ] + (int)entity_velocity[occ];
                    if (a_f > d_f) {
                        _despawn_entity_internal(occ); unit_grid[cur] = -1; grid[cur].effect_stack &= ~Tile::FLAG_HAS_ENTITY;
                        entity_coordinate[i] = next_step; unit_grid[next_step] = i; grid[next_step].effect_stack |= Tile::FLAG_HAS_ENTITY;
                        _mark_tile_active(next_step); _mark_tile_active(cur);
                    } else if (entity_flags[i] & FLAG_PUSH) {
                        // Push Logic: Defender moved in same direction as attack
                        int pdx = dx, pdz = dz;
                        int ptx = (next_step % map_width) + pdx, ptz = (next_step / map_width) + pdz;
                        if (ptx >= 0 && ptx < map_width && ptz >= 0 && ptz < map_height) {
                            uint32_t p_idx = ptz * map_width + ptx;
                            int b_w = (int)biome_weights[grid[p_idx].composition];
                            if (grid[p_idx].is_impassable()) b_w = 255;

                            if (a_f + b_w > d_f) {
                                // Impact Kill: Smashed into heavy biome or obstacle
                                _despawn_entity_internal(occ);
                                unit_grid[cur] = -1; grid[cur].effect_stack &= ~Tile::FLAG_HAS_ENTITY;
                                entity_coordinate[i] = next_step; unit_grid[next_step] = i;
                                grid[next_step].effect_stack |= Tile::FLAG_HAS_ENTITY;
                                _mark_tile_active(next_step); _mark_tile_active(cur);
                            } else if (unit_grid[p_idx] == -1 && !grid[p_idx].is_impassable()) {
                                unit_grid[next_step] = -1; grid[next_step].effect_stack &= ~Tile::FLAG_HAS_ENTITY;
                                entity_coordinate[occ] = p_idx; unit_grid[p_idx] = occ;
                                entity_intent[occ] = p_idx; 
                                grid[p_idx].effect_stack |= Tile::FLAG_HAS_ENTITY;
                                _mark_tile_active(p_idx); _mark_tile_active(next_step);
                            }
                        }
                        entity_intent[i] = cur;
                    } else entity_intent[i] = cur;
                } else entity_intent[i] = cur;
            }
        }
    }
}

int SimulationManager::spawn_unit(int x, int z, int team, int weight) {
    return spawn_unit_full(x, z, team, weight, (team == 0) ? 3 : 2);
}

int SimulationManager::spawn_unit_full(int x, int z, int team, int weight, int velocity, int flags) {
    if (available_entities.empty() || x < 0 || x >= map_width || z < 0 || z >= map_height) return -1;
    int idx = z * map_width + x; if (unit_grid[idx] != -1) return -1;
    int id = available_entities.back(); available_entities.pop_back();
    entity_active[id] = true; entity_coordinate[id] = idx; entity_intent[id] = idx;
    entity_team[id] = (uint8_t)team; entity_base_weight[id] = (uint8_t)weight; 
    entity_velocity[id] = (int8_t)velocity; entity_flags[id] = (uint32_t)flags;
    entity_generation[id]++;
    unit_grid[idx] = id; grid[idx].effect_stack |= Tile::FLAG_HAS_ENTITY; _mark_tile_active(idx); return id;
}

void SimulationManager::despawn_unit(int id) { if (id >= 0 && id < MAX_ENTITIES && entity_active[id]) _despawn_entity_internal(id); }
void SimulationManager::_despawn_entity_internal(int id) {
    int idx = entity_coordinate[id]; unit_grid[idx] = -1; grid[idx].effect_stack &= ~Tile::FLAG_HAS_ENTITY;
    entity_active[id] = false; available_entities.push_back(id); _mark_tile_active(idx);
}
void SimulationManager::move_unit_intent(int id, int tx, int tz) { if (id >= 0 && id < MAX_ENTITIES && entity_active[id] && tx >= 0 && tx < map_width && tz >= 0 && tz < map_height) entity_intent[id] = tz * map_width + tx; }

int SimulationManager::get_unit_at(int x, int z) const { return (x >= 0 && x < map_width && z >= 0 && z < map_height) ? unit_grid[z * map_width + x] : -1; }
Vector2i SimulationManager::get_unit_pos(int id) const { return (id >= 0 && id < MAX_ENTITIES && entity_active[id]) ? Vector2i(entity_coordinate[id] % map_width, entity_coordinate[id] / map_width) : Vector2i(-1, -1); }
Vector2i SimulationManager::get_unit_intent_pos(int id) const { return (id >= 0 && id < MAX_ENTITIES && entity_active[id]) ? Vector2i(entity_intent[id] % map_width, entity_intent[id] / map_width) : Vector2i(-1, -1); }
int SimulationManager::get_unit_team(int id) const { return (id >= 0 && id < MAX_ENTITIES && entity_active[id]) ? entity_team[id] : -1; }
int SimulationManager::get_unit_weight(int id) const { return (id >= 0 && id < MAX_ENTITIES && entity_active[id]) ? entity_base_weight[id] : 0; }
int SimulationManager::get_unit_velocity(int id) const { return (id >= 0 && id < MAX_ENTITIES && entity_active[id]) ? entity_velocity[id] : 0; }
int SimulationManager::get_unit_flags(int id) const { return (id >= 0 && id < MAX_ENTITIES && entity_active[id]) ? entity_flags[id] : 0; }
void SimulationManager::set_unit_flags(int id, int f) { if (id >= 0 && id < MAX_ENTITIES && entity_active[id]) entity_flags[id] = (uint32_t)f; }
Dictionary SimulationManager::get_all_units() const { Dictionary d; for (int i = 0; i < MAX_ENTITIES; ++i) if (entity_active[i]) d[i] = get_unit_pos(i); return d; }

void SimulationManager::set_biome_weight(int id, int w) { if (id >= 0 && id < 256) biome_weights[id] = (uint8_t)w; }
void SimulationManager::set_effect_weight(int b, int w) { if (b >= 0 && b < 64) effect_weights[b] = (int8_t)w; }

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

void SimulationManager::_inject_biome_effects() {}

void SimulationManager::_process_propagation_sparse(const std::vector<int>& active_indices) {
    for (int idx : active_indices) {
        uint64_t st = grid[idx].effect_stack; if (st == 0) continue;
        int x = idx % map_width, z = idx / map_width; uint8_t el = grid[idx].elevation, cd = grid[idx].get_countdown(); uint64_t it = st;
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
                            uint8_t res_e = ((grid[n_i].elevation <= el ? 0xFF : 0x00) & e_m) | ~e_m;
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
        propagation_buffer[idx] = 0; is_dirty_map[idx] = false;
    }
    dirty_propagation_indices.clear();
}

void SimulationManager::set_flammable(int id, bool f) { if (id >= 0 && id < 256) flammability_lut[id] = f ? 0xFF : 0x00; }
void SimulationManager::set_propagation_rule(int i, bool f, bool e) { if (i >= 0 && i < 56) { propagation_rules[i].active = true; propagation_rules[i].check_flammable = f; propagation_rules[i].check_elevation = e; propagation_rules[i].bit = 1ULL << i; } }
void SimulationManager::set_propagation_interval(int i, int iv) { if (i >= 0 && i < 56) propagation_rules[i].spread_interval = iv > 0 ? iv : 1; }
void SimulationManager::_mark_tile_active(int idx) { if (idx >= 0 && idx < (int)is_active_map.size() && !is_active_map[idx]) { is_active_map[idx] = true; active_tiles.push_back(idx); } }
void SimulationManager::_push_to_buffer(int idx, uint64_t m) { if (idx >= 0 && idx < (int)propagation_buffer.size()) { propagation_buffer[idx] |= m; if (!is_dirty_map[idx]) { is_dirty_map[idx] = true; dirty_propagation_indices.push_back(idx); } } }

int SimulationManager::get_scent(int x, int z) const { return (x >= 0 && x < map_width && z >= 0 && z < map_height) ? grid[z * map_width + x].get_scent() : 0; }
PackedByteArray SimulationManager::get_grid_data() const { PackedByteArray d; d.resize((int)grid.size() * sizeof(Tile)); memcpy(d.ptrw(), grid.data(), grid.size() * sizeof(Tile)); return d; }
String SimulationManager::get_scent_map_string() const {
    String res = ""; for (int z=0; z<map_height; ++z) { for (int x=0; x<map_width; ++x) { if (is_impassable(x,z)) res+=" # "; else { int s = get_scent(x,z); if (s==0) res+=" . "; else { char buf[4]; snprintf(buf,sizeof(buf),"%2d ",s); res+=buf; } } } res+="\n"; } return res;
}
void SimulationManager::generate_new_world(int seed) {
    grid.assign(map_width * map_height, Tile{0, 0, 0, 0, 0, 0, {0, 0, 0}});
    for (auto &tile : grid) tile.set_countdown(7);
    active_tiles.clear(); is_active_map.assign(map_width * map_height, false);
    dirty_propagation_indices.clear(); is_dirty_map.assign(map_width * map_height, false);
    propagation_buffer.assign(map_width * map_height, 0); step_count = 0;
    available_entities.clear(); for (int i = MAX_ENTITIES - 1; i >= 0; --i) available_entities.push_back(i);
    entity_active.assign(MAX_ENTITIES, false); unit_grid.assign(map_width * map_height, -1);
}
void SimulationManager::clear_interaction_tables() {
    memset(annihilation_matrix, 0, sizeof(annihilation_matrix)); memset(chemistry_pairs, 0, sizeof(chemistry_pairs)); memset(flammability_lut, 0, sizeof(flammability_lut));
    for (int i=0; i<64; ++i) propagation_rules[i] = PropagationRule();
    for (int b=0; b<256; ++b) for (int e=0; e<64; ++e) biome_transitions[b][e] = (uint8_t)b;
}
void SimulationManager::set_tile_composition(int x, int z, int c) { if (x >= 0 && x < map_width && z >= 0 && z < map_height) grid[z * map_width + x].composition = (uint8_t)c; }
int SimulationManager::get_tile_composition(int x, int z) const { return (x >= 0 && x < map_width && z >= 0 && z < map_height) ? (int)grid[z * map_width + x].composition : 0; }
void SimulationManager::set_tile_effect(int x, int z, uint64_t e) { if (x >= 0 && x < map_width && z >= 0 && z < map_height) { int idx = z * map_width + x; grid[idx].effect_stack |= e; if (e & 0x00FFFFFFFFFFFFFFULL) grid[idx].set_countdown(7); _mark_tile_active(idx); } }
uint64_t SimulationManager::get_tile_effects(int x, int z) const { return (x >= 0 && x < map_width && z >= 0 && z < map_height) ? grid[z * map_width + x].effect_stack : (uint64_t)0; }
void SimulationManager::set_impassable(int x, int z, bool i) { if (x >= 0 && x < map_width && z >= 0 && z < map_height) { int idx = z * map_width + x; if (i) grid[idx].pathing_scent |= Tile::FLAG_HAZARD | Tile::FLAG_IMPASSABLE; else grid[idx].pathing_scent &= ~(Tile::FLAG_HAZARD | Tile::FLAG_IMPASSABLE); } }
bool SimulationManager::is_impassable(int x, int z) const { return (x >= 0 && x < map_width && z >= 0 && z < map_height) ? grid[z * map_width + x].is_impassable() : false; }
void SimulationManager::add_annihilation(int bit_a, int bit_b) { if (bit_a < 64 && bit_b < 64) { annihilation_matrix[bit_a] |= (1ULL << bit_b); annihilation_matrix[bit_b] |= (1ULL << bit_a); } }
void SimulationManager::add_chemistry(int bit_a, int bit_b, uint64_t result_stack) { if (bit_a < 64 && bit_b < 64) { chemistry_pairs[bit_a][bit_b] = result_stack; chemistry_pairs[bit_b][bit_a] = result_stack; } }
void SimulationManager::add_biome_transition(int biome_id, int effect_bit, int result_biome_id) { if (effect_bit < 64 && biome_id < 256) biome_transitions[biome_id][effect_bit] = (uint8_t)result_biome_id; }
