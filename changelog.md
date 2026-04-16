# Changelog

All notable changes to the Cress project will be documented in this file.

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

### Fixed
- Resolved `static_assert` failure where `Tile` struct was exceeding 16 bytes due to default compiler padding.
- Added MSVC compatibility for trailing zero counting intrinsics.
- Fixed `debug_view.gd` parser error by using a more reliable font retrieval method.
- **Fixed JSON Dictionary Lookups**: Added explicit `int()` casts for biome IDs and effect bits in `debug_view.gd` to prevent float-key mismatches from Godot's JSON parser.
- **Improved UI Selection**: Updated `debug_view.gd` to draw the white selection square last, ensuring it's always visible on top of all layers.
- **UI Descriptions**: Added a dynamic description field to the Biome editor in the "Definition Editor" addon.
