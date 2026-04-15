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
- **2D Debug Visualization (`test_project/debug_view.tscn/gd`)**:
    - **JSON Definitions**: Now loads biome, effect, and interaction rules from `test_project/definitions.json`.
    - **Effect Interaction Testing**:
        - **Shift + RMB**: Place the currently selected effect from the dropdown onto a tile.
        - **Run Step Button**: Executes the simulation step (ALU processing).
    - **Visuals**: Added orange outlines for tiles with active effects.
- **GDExtension Registration**: Registered `SimulationManager` in `register_types.cpp`.
- **Class Documentation (`doc_classes/SimulationManager.xml`)**: Added comprehensive XML documentation for the `SimulationManager` class, including detailed descriptions of all methods and properties for Godot Editor integration.

### Fixed
- Resolved `static_assert` failure where `Tile` struct was exceeding 16 bytes due to default compiler padding.
- Added MSVC compatibility for trailing zero counting intrinsics.
- Fixed `debug_view.gd` parser error by using a more reliable font retrieval method.
