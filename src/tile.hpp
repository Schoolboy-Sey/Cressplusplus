#pragma once

#include <cstdint>

/**
 * @struct Tile
 * @brief Represents a single 16-byte coordinate in the grid.
 * 
 * Memory Map:
 * Byte 1:  Composition (uint8_t)
 * Byte 2:  Slope (uint8_t)
 * Byte 3:  Elevation (uint8_t)
 * Byte 4:  Pathing/Scent (uint8_t)
 * Bytes 5-12: Effect Stack (uint64_t)
 * Byte 13: Ambient Mana (uint8_t)
 * Bytes 14-16: Padding (uint8_t)
 */
#pragma pack(push, 1)
struct Tile {
    uint8_t composition;        // Physical biome ID
    uint8_t slope;              // Gravity vectors
    uint8_t elevation;          // Z-level data
    uint8_t pathing_scent;      // Scent (bits 0-3), Flags (bits 4-7)
    uint64_t effect_stack;      // Transient effects
    uint8_t ambient_mana;       // Available spellcasting fuel
    uint8_t padding[3];         // L1 Cache alignment

    // Constants for pathing_scent
    static const uint8_t SCENT_MASK = 0x0F;
    static const uint8_t FLAG_HAZARD = 0x10;
    static const uint8_t FLAG_IMPASSABLE = 0x20;

    void set_scent(uint8_t scent) {
        pathing_scent = (pathing_scent & ~SCENT_MASK) | (scent & SCENT_MASK);
    }

    uint8_t get_scent() const {
        return pathing_scent & SCENT_MASK;
    }

    bool is_impassable() const {
        return (pathing_scent & FLAG_IMPASSABLE) != 0;
    }
};
#pragma pack(pop)
static_assert(sizeof(Tile) == 16, "Tile must be exactly 16 bytes.");
