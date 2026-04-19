# **ENGINE TECHNICAL DESIGN DOCUMENT (TDD)**

**Architecture Type:** 16-Byte Deterministic Bitwise Grid
**Paradigm:** Data-Oriented Design (DOD) / Branchless ALU
**Game Flow:** Hybrid Turn-Based "WeGo" (Simultaneous predicted execution via step-intervals)

---

## **1. MEMORY MAP (The 16-Byte Coordinate)**

Every physical coordinate on the map is represented by exactly 16 bytes. There are no object-oriented "Tile" classes. In every instance, branchless C++ functions must be used where it is the most efficient option.

### **Byte 1: The Static State (uint8_t)**
*   **Bits 0-7: Tile_Composition.** Represents the physical base biome via bitflags that relate to mana composition (e.g., Desert, Wooden Fortress, Slime Pool). Gives up to 256 unique biomes.

### **Byte 2: The Physical Geometry (uint8_t)**
*   **Bits 0-7: Slope.** Defines gravity vectors (Flat, N, E, S, W, Peak, Pit, Wall).
    *   *Note:* Could use extra space for terrain modifiers.

### **Byte 3: Elevation data (0-255) (uint8_t)**
*   **Bits 0-7: Absolute Elevation.** Dictates physical gravity and fluid flow.

### **Byte 4: Pathfinding/Scent Data (uint8_t)**
*   **Bits 0-3: Scent.** AI tracking data for the player. Spreads out and steps down as it propagates.
*   **Bit 4: Hazard Flag.** Shows that the tile is considered a hazard; units will track around it.
*   **Bit 5: Impassable Flag.** Used to tell the engine to activate weight/collision calculations if a unit attempts movement into it.
*   **Bits 6-7: RESERVED**

### **Bytes 5-12: The Effect Stack (uint64_t)**
A single 64-bit integer representing all transient effects, modifiers, and local states. Sorted strictly by priority (Bit 0 resolves first, Bit 63 resolves last). This is connected to a lookup table that determines the tile transformation results.

*   **Bytes 5-6 (Elements):** Base physical effects (Water, Fire, Magic).
*   **Byte 7 (Modifiers):** Adjectives (Poison, Holy) that piggyback on elements.
*   **Byte 8 (Commands):** Verbs (Erode, Build) that alter Bytes 1 or 2.
*   **Byte 9 (Signals):** Data pulses for logic gates / traps.
*   **Bytes 10-11: RESERVED**
*   **Byte 12:**
    *   **Bits 0-2 (Timers):** Local countdown ticks for delayed events (e.g., Spawners).
    *   **Bit 3 (Occupancy):** HAS_ENTITY flag. Used to ping the Entity Manager without expensive RAM lookups.
    *   **Bit 4 (Items):** HAS_ITEM flag.
    *   **Bits 5-7: RESERVED**

### **Byte 13: Ambient Mana (uint8_t)**
*   **Bits 0-7:** Bitflags representing the types of Mana available in the tile for spellcasting fuel.

### **Bytes 14-16: RESERVED / PADDING (uint8_t)**
*   Padding to maintain strict 16-byte chunk alignment for L1 Cache optimization.

---

## **2. THE GAME LOOP (Execution Order)**

The game operates in Turn phases, with the "Action" phase divided into discrete, rapid "Steps."

### **Phase 1: Planning Phase**
1.  **AI Calculation:** Enemy AI calculates optimal paths based on Scent/Hazards and locks in movement intents. These intents are telegraphed visually to the player.
2.  **Player Input:** Player inputs their movement and spell actions.

### **Phase 2: Execution Phase (The Step Loop)**
For every Step, the following exact order of operations occurs:

1.  **The Instant Window:** Execution pauses. Players may cast "Instant" speed abilities (inserting bits into the stack or altering unit paths).
2.  **Entity Movement & Combat:**
    *   Units attempt to move based on locked-in paths.
    *   Simultaneous occupation triggers **The Clash** (Chess-style Combat). Winner claims the tile; loser is repelled or destroyed.
3.  **The Grid ALU (Effects Loop):** Branchless mathematics process all active effects (Annihilation, Chemistry, Transitions).
4.  **Damage Resolution:** If a tile is flagged as dangerous and HAS_ENTITY is 1, the unit standing there takes damage.
5.  **Timer Resolution:** Countdown bits are decremented. If a spawner hits 0, the Entity Manager spawns a new unit.

---

## **3. THE BRANCHLESS ALU (Grid Effects Resolution)**

### **A. The Hardware Jump (CTZ)**
The engine loads the 64-bit Effect integer and uses hardware-accelerated bit-scanning (`ctz64`) to find the lowest active bit (Highest Priority), skipping empty bits entirely.

### **B. The Transition Registry**
The engine uses the active Effect to select a 256-byte transition array from the L1 Cache (e.g., `FIRE_TRANSITIONS[]`). It uses the `Composition` (Byte 1) as the index to find the target state.

### **C. The ALU Multiplexer**
The CPU executes resolution using pure logic gates:
```cpp
// 1. Array Lookup
uint8_t target_tile = FIRE_TRANSITIONS[Current_Tile_Composition];

// 2. Generate All-or-Nothing Mask via Two's Complement
uint8_t active_mask = -((Active_Effects & SPECIFIC_EFFECT_BIT) >> Bit_Index);

// 3. Branchless State Swap
Current_Tile_Composition = (Current_Tile_Composition & ~active_mask) | (target_tile & active_mask);

// 4. Clear the processed bit from the stack and loop
Active_Effects &= (Active_Effects - 1);
```

### **D. Slope Displacement (Vector Movement)**
After chemistry resolves, the ALU checks the Slope byte. It uses memory offsets to shift "Gravity-Bound" effect bits to adjacent tiles for the next frame.

---

## **4. ENTITY COMPONENT SYSTEM & COMBAT**

**Architecture:** Structure of Arrays (SoA) / Pure Data-Oriented
**Combat Paradigm:** Deterministic Integer Mathematics (Weight + Velocity)

### **1. ECS Memory Structure**
Entities exist as parallel arrays of raw primitive data. A unit's "ID" is its index across these arrays.

*   `uint32_t Entity_Coordinates[MAX_UNITS]` - 1D grid index.
*   `uint8_t Entity_BaseWeight[MAX_UNITS]` - Innate vigor/mass.
*   `int8_t Entity_Velocity[MAX_UNITS]` - Transient speed modifier.
*   `uint8_t Entity_Team[MAX_UNITS]` - Identifier (0=Player, 1=Enemy).
*   `uint8_t Entity_Flags[MAX_UNITS]` - Bitmask (Bit 0: Bounce, Bit 1: Push).

### **2. Deterministic Combat Resolution (The Clash)**
Triggered when two entities attempt to occupy the same coordinate.
**The Equation:** `Total Force = BaseWeight + Velocity`

#### **Standard Resolution Matrix**
1.  **Attacker Force > Defender Force:** Defender is destroyed (unless they possess *Deflect*). Attacker occupies the tile.
2.  **Attacker Force < Defender Force:** Attack fails. Movement is canceled; attacker may "Bounce" away.
3.  **Attacker Force == Defender Force (The Tie):** Mutual failure. Both units cancel movement and trigger the "Bounce" fallback.

#### **The "Bounce" Fallback**
If an attack fails, the attacking unit's coordinate is reverted to its physical location from the *previous* step, visually bouncing off the defender to create tactical space.

### **3. Kinetic Collisions (The Domino Effect)**
If a unit has the `Push` flag or is knocked back, it becomes a projectile.

*   **Entity-to-Entity:** If Unit A is pushed into Unit B, a secondary Clash is calculated. If A wins, B takes damage and is pushed into the next tile. This recurses until a defender wins or an empty tile is reached.
*   **Entity-to-Grid:** If Unit A is pushed into an `Impassable` tile:
    *   **Unit Force > Biome Weight:** Inject `SHOCKWAVE` command into tile; tile is destroyed and Unit A moves in.
    *   **Biome Weight > Unit Force:** Unit A stops and suffers "Crush Damage."

---

## **5. RENDERING PIPELINE (Bitmask Shader)**

**Source:** C++ GDExtension via Vulkan Storage Buffers
**Goal:** HD-2D / 3D Billboard rendering of a deterministic grid.

1.  **Bridge Node:** `SimulationManager` sits in the scene tree and holds the master 1D tile array.
2.  **Storage Buffer:** The array is exposed directly to Vulkan, bypassing GDScript entirely.
3.  **The Floor (MultiMesh):** Ground is rendered as massive flat grid of quads.
    *   *Base Texture:* Shader reads `Composition` to sample the Atlas.
    *   *Effect Compositing:* Shader reads `Effect Stack` for translucent overlays and dyes.
    *   *Elevation Illusion:* Shader draws "Cliff Faces" based on the Elevation differential between neighbors.
4.  **Entities (Sprite3D):** Units and structures use billboard nodes near the camera to avoid MultiMesh UV complexity.

---

## **6. HYBRID TOOLING PIPELINE**

1.  **Procedural Generation:** C++ sampling of `FastNoiseLite` to generate elevations and biomes for 1M+ tiles.
2.  **TileMap Compiler:** Level designers author dungeons in Godot's `TileMapLayer`; an `@tool` script compiles them into 16-byte binary `.dat` files for runtime loading.
3.  **3D Editor Chisel:** Editor Plugin using raycasting to modify the C++ memory array in real-time.
4.  **Binary Serialization:** Direct `std::ofstream` dump of the `Grid_Array` to disk for near-instant save/load.

---

## **Top Down View (The Symphony)**

1.  **Pre-Step (Injection):** Active Biomes write Elemental Bits to the Write Buffer.
2.  **Watchpoint (Interrupts):** Players may cast "Instant" spells.
3.  **ECS Execution (Combat & Gravity):**
    *   Check elevation; units fall if over pits.
    *   Resolve clashes and "Bounce" fallbacks. Units enter "Wile E. Coyote" mode.
4.  **Annihilation:** `Effect Stack` processed through `ANNIHILATION_MATRIX`.
5.  **Chemistry:** `Effect Stack` processed through `CHEMISTRY_PAIRS`.
6.  **Propagation:** Surviving elements move based on `CARRY_MASK` and gravity rules into the Write Buffer.
7.  **Mutation & Wipe:** `BIOME_TRANSITIONS` update biomes; the `Effect Stack` is wiped clean.
8.  **Biome Propagation:** Random tiles check neighbors for evolution rules (e.g., Forest spreading to Dirt).
