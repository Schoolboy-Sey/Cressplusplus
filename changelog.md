# Changelog

All notable changes to the Cress project will be documented in this file.

## [2026-04-19] - Biological Engine & AVX2 "8-Step Symphony"
### Added
- **AVX2 SIMD Biological CA**: Implemented a high-speed scent wave propagation system using 256-bit SIMD intrinsics.
    - **Exactly One Neighbor Rule**: Implemented bitwise logic to ensure waves fracture and self-annihilate upon collision, preventing "solid wavefronts."
    - **Wave-Based Recharge**: Scent pulses now physically transport mana; passing pulses recharge `ambient_mana` on tiles matching the biome identity.
    - **4-State CA**: Implemented the full Source -> Pulse -> Trail -> Empty state machine within the SIMD block.
- **Branchless Evolution & Mutation (Step 6)**: Implemented the "Identity Theft" biological reaction.
    - **Evolution Matrix**: Added `evolution_paths[256][8]` LUT to handle species transformations without `if/else` logic.
    - **Bitwise Masking**: Used "All-or-Nothing" masks to swap species identities and physics stats in a single CPU operation during feeding.
    - **Mutation Tracking**: Consumed mana bits are added to the `entity_mutation` stack, automatically tiering units up via hardware `popcount`.
- **Phase Alignment AI ($O(1)$ Navigation)**:
    - **Intersection Tracking**: Herbivores now use a 16-bit `diet_mask` to detect wave intersections in a single memory load.
    - **Ride vs. Surf**: Units now correctly navigate "Upstream" toward food sources when hungry and "Surf" away when sated.
- **Hardware-Optimized execution (The 8-Step Symphony)**:
    - **Pipeline Re-ordering**: Refactored `run_step()` to follow the strict TDD pipeline (Replenishment -> Movement -> Biological Wave -> Chemistry -> Feeding -> World Mutation).
    - **Occupancy Grid v2**: Switched to `uint16_t` with an `EMPTY_TILE` (65535) sentinel for faster memory access.
    - **Sparse Looping**: Replaced $O(N)$ full-array scans with a dense `active_entity_indices` list to bypass empty ECS slots.
- **Improved Debug Tools**:
    - **Tile Inspector**: Added a real-time popup to view Tile ID, Composition, and Ambient Mana levels.
    - **Unit Status Overlays**: Added visual indicators for "Sated" (stomach bar) and "Eating" (green arc animation).
    - **Named Waves**: The wave selector now uses mana names (Fire, Water, etc.) instead of IDs for better clarity.

### Fixed
- **Memory Traps**: Eliminated `std::vector<bool>` bit-shifting traps by converting status maps to `uint8_t`.
- **L1 Cache Alignment**: Added `alignas(32)` and 48-tile horizontal padding to shadow buffers to guarantee perfect SIMD memory streaming.
- **Zero Allocation**: Moved wavefront queues to pre-allocated class-level buffers to prevent OS memory requests during the physics loop.
- **Scent Persistence**: Resolved a bug where scent pulses were too transient for AI tracking by implementing Compass Heading memory in the ECS.

## [2026-04-15] - Initial GDExtension Architecture & Smellnet
### Added
- **Tile Structure (`src/tile.hpp`)**: Implemented the core 16-byte deterministic bitwise grid unit as specified in the TDD.
    - Used `#pragma pack(push, 1)` to ensure strict 16-byte hardware alignment.
    - Included fields for `composition`, `slope`, `elevation`, `pathing_scent`, `effect_stack`, and `ambient_mana`.
- **SimulationManager (`src/simulation_manager.cpp/hpp`)**: Created the primary GDExtension bridge between Godot and the C++ simulation.
    - **Branchless ALU**: Implemented high-speed effect processing using `__builtin_ctzll` (with MSVC `_BitScanForward64` fallback).
        - **Phase 1: Annihilation**: Mutual destruction of conflicting bits (e.g., Water + Fire).
        - **Phase 2: Chemistry**: Combination of bits into new effects (e.g., Water + Magic = Steam).
        - **Phase 3: Biome Transitions**: Surviving effects can permanently alter the `composition` byte.
        - **Phase 4: Stack Wipe**: All transient effects are cleared after processing to prepare for the next step.
    - **Interaction Tables**: Added methods to populate ALU LUTs from external data (`add_annihilation`, `add_chemistry`, `add_biome_transition`).
    - **Smellnet**: BFS scent propagation algorithm.
    - **Utility**: Added property accessors and grid data exposure for Godot.
- **2D Debug Visualization Upgrades (`test_project/debug_view.tscn/gd`)**:
    - **Mana Bitflag Checkboxes**: Replaced biome dropdown with individual checkboxes for each Mana bit (W, F, E, Pl, Pu, R, Mi, Ma), allowing for direct composition of complex biomes.
    - **Visual Effects**: Active effects are now rendered as small, color-coded balls that orbit the center of the tile, providing a clear view of multiple simultaneous effects.
    - **Save/Load System**: Implemented a JSON-based save/load system (`user://grid_save.json`) to persist grid configurations, including biomes, impassable flags, and active effects.
    - **UI Enhancements**: Expanded the edit panel and improved the layout for better accessibility.
    - **JSON Definitions**: Now loads biome, effect, and interaction rules from `test_project/definitions.json`.
    - **Effect Interaction Testing**:
        - **Shift + RMB**: Place the currently selected effect from the dropdown onto a tile.
        - **Run Step Button**: Executes the simulation step (ALU processing).
- **GDExtension Registration**: Registered `SimulationManager` in `register_types.cpp`.
- **Class Documentation (`doc_classes/SimulationManager.xml`)**: Added comprehensive XML documentation for the `SimulationManager` class, including detailed descriptions of all methods and properties for Godot Editor integration.
- **Scent Verification Tooling**: 
    - Added `get_scent_map_string()` to `SimulationManager` for ASCII-based debugging of the smellnet wavefront.
    - Updated `test_project/main.gd` with rigorous U-trap verification tests to confirm BFS behavior.
- **Godot Addon: "Definition Editor"**:
    - Created a Main Screen plugin to edit `definitions.json` directly within the Godot editor.
    - Supports Biome configuration (ID, Name, Description) using bitwise mana checkboxes.
    - Includes an Effect Manager to name all 64 bits of the effect stack by byte.
    - Features dedicated panels for managing Annihilation, Chemistry, and Biome Transition rules.
    - **Search & Filter**: Added real-time search bars and browser lists for biomes, interactions, and transitions.
- **In-Game Debugging Upgrades**:
    - **Paint Mode**: Implemented click-and-drag "painting" for compositions, effects (Shift+Drag), and impassable flags (Ctrl+Drag).
    - **Live Sync**: Added a "Reload Definitions" button to the debug UI to apply simulation rule changes without restarting.

- **High-Performance Propagation Engine**:
    - **Phase 6: Propagation**: Implemented a double-buffered spread system using a `propagation_buffer`.
    - **Deterministic Decay**: Fire and other elements now spread exactly one tile per simulation step.
    - **Propagation LUTs**: Added `flammability_lut` and `PropagationRule` structures to configure complex spread logic (Elevation, Flammability, etc.) in C++.
- **Branchless ALU Refactor**:
    - Optimized core simulation passes using **Two's Complement Masking** to eliminate conditional jumps.
    - **Blind Transitions**: Biome changes now use direct LUT assignments, avoiding "if-changed" logic.
    - **Unrolled Neighbor Checks**: Manually unrolled 4-way BFS and propagation loops for maximum instruction pipelining.
- **Enhanced Map Management**:
    - **Named Saves**: Added ability to save/load maps with custom names to `user://maps/`.
    - **Map Browser**: Integrated a dropdown UI to swap between different testing scenarios instantly.

- **Workflow & Debug UI Improvements**:
    - **Run Turn**: Added a "Run Turn" button that executes 10 simulation steps sequentially to observe long-term deterministic results.
    - **Map Name Auto-fill**: Loading a map now automatically populates the "Map Name" field with the loaded file's name for faster iteration and overwriting.
- **Sparse Propagation & Sparse Updates**:
    - **Performance Optimization**: Replaced full-grid scans with an `active_tiles` list and `dirty_propagation_indices`. The simulation now only processes tiles that contain active effects or are affected by spread.
    - **Scaling**: Targeted to handle up to 1,000,000 tiles by skipping hundreds of thousands of "static/empty" tiles per frame.
- **"Slow Burn" Pacing & Persistence**:
    - **Heat Thresholds**: Implemented smolder countdowns (7 steps) using Byte 12 timer bits. Effects must "build up" on a tile before jumping to neighbors.
    - **Spread Intervals**: Added `set_propagation_interval` to configure effect-specific speed (e.g., Fire spreads every 4 steps, Water spreads every 1).
    - **Fuel-Aware Persistence**: Effects now strictly extinguish if their host biome transforms into a non-flammable state (e.g., Forest -> Dirt consumes the fire fuel).
- **Pooled ECS & Deterministic Combat**:
    - **Structure of Arrays (SoA)**: Entities are managed in high-speed parallel arrays with a fixed-memory budget of 4,096 units.
    - **The Clash**: Implemented deterministic combat resolution using `Weight + Velocity`.
    - **Movement Sub-Steps**: High-velocity units now correctly take multiple sequential steps per turn.
    - **Automated Scent**: The Smellnet AI tracking system now automatically source-links to the first Player unit (Team 0).
- **Tooling: "Play Mode" & State Snapshots**:
    - **Snapshot Engine**: Added C++ binary backup/restore for the entire world state.
    - **Undo/Revert**: Players can enter Play Mode to test scenarios and instantly revert the map to its design state upon exiting.
    - **Intent HUD**: Added Cyan (Player) and Red (Enemy) movement arrows to telegraph planned paths during the turn phase.

### Fixed
- Resolved `static_assert` failure where `Tile` struct was exceeding 16 bytes due to default compiler padding.
- Added MSVC compatibility for trailing zero counting intrinsics.
- Fixed `debug_view.gd` parser error by using a more reliable font retrieval method.
- **Fixed JSON Dictionary Lookups**: Added explicit `int()` casts for biome IDs and effect bits in `debug_view.gd` to prevent float-key mismatches from Godot's JSON parser.
- **Improved UI Selection**: Updated `debug_view.gd` to draw the white selection square last, ensuring it's always visible on top of all layers.
- **UI Descriptions**: Added a dynamic description field to the Biome editor in the "Definition Editor" addon.
- **Paint Safety**: Fixed a bug where painting could be initiated by clicking outside the grid boundaries.
- **Propagation Timing**: Refactored `run_step` to strictly separate the "Current Turn ALU" from the "Next Turn Spread," resolving a bug where fire would spread and disappear in a single step.
