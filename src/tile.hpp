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
    uint8_t composition;        // Byte 1: Physical biome ID
    uint8_t slope_geometry;     // Byte 2: Gravity vectors / shape
    uint8_t absolute_elevation; // Byte 3: Z-level data
    uint8_t pathing_scent;      // Byte 4: Scent (bits 0-3), Flags (bits 4-7)
    uint64_t effect_stack;      // Bytes 5-12: Transient effects
    uint8_t ambient_mana;       // Byte 13: Available spellcasting fuel
    uint16_t imprint_field;     // Bytes 14-15: Scent Wave CA
    uint8_t reserved_padding;   // Byte 16: L1 Cache alignment

    // Constants for pathing_scent
    static const uint8_t SCENT_MASK = 0x0F;
    static const uint8_t FLAG_HAZARD = 0x10;
    static const uint8_t FLAG_IMPASSABLE = 0x20;

    // Constants for Byte 12 (effect_stack top byte)
    static const uint64_t FLAG_HAS_ENTITY = 1ULL << 59; // Byte 12, Bit 3
    static const uint64_t FLAG_HAS_ITEM = 1ULL << 60;   // Byte 12, Bit 4

    void set_scent(uint8_t scent) {
        pathing_scent = (pathing_scent & ~SCENT_MASK) | (scent & SCENT_MASK);
    }

    uint8_t get_scent() const {
        return pathing_scent & SCENT_MASK;
    }

    bool is_impassable() const {
        return (pathing_scent & FLAG_IMPASSABLE) != 0;
    }

    // Byte 12 Timer Helpers (Bits 56-58 of effect_stack)
    uint8_t get_countdown() const {
        return static_cast<uint8_t>((effect_stack >> 56) & 0x7);
    }

    void set_countdown(uint8_t val) {
        effect_stack = (effect_stack & ~(0x7ULL << 56)) | (static_cast<uint64_t>(val & 0x7) << 56);
    }
};
#pragma pack(pop)
static_assert(sizeof(Tile) == 16, "Tile must be exactly 16 bytes.");
