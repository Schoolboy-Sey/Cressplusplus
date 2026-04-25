# **ENGINE TECHNICAL DESIGN DOCUMENT (TDD)**

**Architecture Type:** 16-Byte Deterministic Bitwise Grid

**Paradigm:** Data-Oriented Design (DOD) / Branchless ALU

**Game Flow:** Hybrid Turn-Based "WeGo" (Simultaneous predicted execution via step-intervals)

## **1\. MEMORY MAP (The 16-Byte Coordinate)**

Every physical coordinate on the map is represented by exactly 16 bytes. There are no object-oriented "Tile" classes.

### **Byte 1: The Static State (uint8\_t)**

* **Bits 0-7:** Tile\_Composition. Represents the physical base biome via bitflags that relate to mana composition. (e.g., Desert, Wooden Fortress, Slime Pool). Gives up to 256 unique biomes.

### **Byte 2: The Physical Geometry (uint8\_t)**

* **Bits 0-7:** Slope. Defines gravity vectors (Flat, N, E, S, W, Peak, Pit, Wall).  
  * Seems excessive  
  * Could use extra space for terrain modiifiers

### **Byte 3: Elevation data (0-255) (uint8\_t)**

* **Bits 0-7: Elevation or Z-level data.**  
  * Again, excessive. 

### **Byte 4: Pathfinding/Scent Data (uint8\_t)**

* **Bits 0-3:** Scent data for the player. Allows enemies to track towards the player. Spreads out and steps down as it spreads.   
* **Bit 4:** Hazard Flag \- shows that the tile is considered a hazard and to track around it.  
* **Bit 5:** Impassable Flag \- Usually can’t walk through. Used to tell the engine to activate weight calculations if a unit moves into it suddenly.  
* **Bit 6: RESERVED**  
* **Bit 7: RESERVED**  
* **TODO: How does this spread?**

### **Bytes 5-12: The Effect Stack (uint64\_t)**

A single 64-bit integer representing all transient effects, modifiers, and local states. Sorted strictly by priority (Bit 0 resolves first, Bit 63 resolves last).

This is connected to a lookup table that determines the tile that it transforms into if it is hit by an effect. For example Tree \+ Fire \= ruin, but volcano \+ fire \= no change. The First \[0\] part always has no change, used to block effects from moving outside the map, and for empty spaces (Air)

* **Byte 5-6 (Elements):** Base physical effects (Water, Fire, Magic).  
* **Byte 7 (Modifiers):** Adjectives (Poison, Holy) that piggyback on elements.  
* **Byte 8 (Commands):** Verbs (Erode, Build) that alter Bytes 1 or 2\.  
* **Byte 9 (Signals):** Data pulses for logic gates / traps.  
* **Bytes 10-11 (Reserved):** Unassigned expansion space.  
* **Byte 12**  
  * **Bits 0-2(Timers):** Local countdown ticks for delayed events (e.g., Spawners).  
  * **Bit 3 (Occupancy):** HAS\_ENTITY flag. Used to ping the Entity Manager without expensive RAM lookups.  
  * **Bit 4 (Items):** HAS\_ITEM flag  
  * **Bit 5-7: RESERVED**

### **Byte 13: Ambient Mana (uint8\_t)**

* **Bits 0-7:** Bitflag that represents what types of Mana are in the tile

### **Byte 14-15: The Imprint Field (uint16\_t)** See Ecosystem and Biological Loop

### **Byte 16: RESERVED/ PADDING (uint8\_t)** Keeps the tile in a 16byte chunk for easiest CPU processing, can use for later. 

* **Bits 0-7:**

---

## **2\. THE GAME LOOP (Execution Order)**

The game operates in Turn phases, with the "Action" phase divided into discrete, rapid "Steps."

**Phase 1: Planning Phase**

1. Enemy AI calculates optimal paths based on Byte 2 (Hazard/Scent) and locks in movement/action intent. These intents are telegraphed to the player visually.  
2. Player inputs their actions and movement.

**Phase 2: Execution Phase (The Step Loop)**

The engine executes the locked-in actions over a series of Steps. For every Step, the following exact order of operations occurs:

TODO: add In new steps that combine and delete effects

1. **The Instant Window:** Game pauses execution. Players may cast "Instant" speed abilities (inserting new Effect bits into the stack or altering unit paths).  
2. **Entity Movement & Combat:** \* Units attempt to move based on their locked-in paths.  
   * If two opposing units attempt to occupy the same grid space simultaneously, **Chess-style Combat** triggers immediately. Stats are compared, the loser is destroyed or repelled, and the winner claims the HAS\_ENTITY coordinate.  
3. **The Grid ALU (Effects Loop):** The branchless mathematics process all active effects on the board (Detailed in Section 3).  
4. **Damage Resolution:** If the Grid ALU flagged a tile as dangerous *and* Bit 63 (HAS\_ENTITY) is 1, the Entity Manager is pinged to apply damage to the unit standing there.  
5. **Timer Resolution:** Bits 60-62 are decremented. If a Spawner timer hits 0, the Entity Manager spawns the relevant unit (e.g., Slime) into the coordinate.

---

## **3\. THE BRANCHLESS ALU (Grid Effects Resolution)**

During Step 3 of the Execution Phase, the Chunk Manager iterates over all "Awake" 32x32 tile chunks. For every tile, it runs the hardware-accelerated Effects Loop.

**A. The Hardware Jump (CTZ)**

The engine loads the 64-bit Effect integer and uses \_\_builtin\_ctzll(effects) to find the lowest active bit (Highest Priority). It completely skips empty bits.

**B. The Transition Registry**

The engine uses the active Effect to select a 256-byte array in the L1 Cache (e.g., FIRE\_TRANSITIONS\[\]). It uses the Tile\_Composition (Byte 1\) as the index to find the target state.

**C. The ALU Multiplexer**

The CPU executes the resolution using pure logic gates.

C++  
// 1\. Array Lookup  
uint8\_t target\_tile \= FIRE\_TRANSITIONS\[Current\_Tile\_Composition\];

// 2\. Generate All-or-Nothing Mask via Two's Complement  
uint8\_t active\_mask \= \-((Active\_Effects & SPECIFIC\_EFFECT\_BIT) \>\> Bit\_Index);

// 3\. Branchless State Swap  
Current\_Tile\_Composition \= (Current\_Tile\_Composition & \~active\_mask) | (target\_tile & active\_mask);

// 4\. Clear the processed bit from the stack and loop  
Active\_Effects &= (Active\_Effects \- 1);

**D. Slope Displacement (Vector Movement)**

After chemistry resolves, the ALU checks the 3-bit Slope. It uses a predefined array of memory offsets to shift specific "Gravity-Bound" effect bits to the correct adjacent memory address for the next frame.

---

## **4\. PERMANENT TERRAFORMING VS. TRANSIENT EFFECTS**

* **Spells/Chaos (Transient):** Adding Fire or Water via an attack flips a bit in the **Effect Stack (Bytes 3-10)**. It is subject to destruction masks and slope displacement.  
* **Building/Upgrading (Permanent):** When a Builder class creates a structure or upgrades a tile (e.g., Lean-to \-\> Slime Pool), the engine directly overwrites the **Tile Composition (Byte 1\)**. This bypasses the Effect Stack entirely.

---

## **5\. RENDERING PIPELINE (Bitmask Shader)**

The frontend graphics engine does not calculate state. It receives the 10-byte chunk data and passes it to a single GPU Shader.

1. Draws base texture using Tile\_Composition (Byte 1).  
2. Composites Elements (Bits 0-15) via overlay textures (e.g., translucent water).  
3. Applies Color Multiplies/Dyes (Bits 16-23) via RGB math (e.g., Poison bit \= Green tint).  
4. Emits particles or UI warning decals based on Commands/Hazards.

# **GODOT RENDERING PIPELINE (TDD)**

**Architecture Type:** HD-2D / 3D Billboard **Data Source:** C++ GDExtension via Vulkan Storage Buffers **Goal:** Render a massive, 64-bit deterministic grid using 2D pixel art within a dynamic 3D camera environment.

## **1\. DATA TRANSFER & SYSTEM "GLUE"**

The game logic lives entirely in C++. Godot acts solely as the Renderer and UI framework.

* **The Bridge Node:** A custom GDExtension class called `SimulationManager` sits in the Godot Scene Tree.  
* **The Storage Buffer:** The `SimulationManager` holds the master 1D array of 16-byte tiles. It exposes this array directly to Godot's Vulkan rendering pipeline using a `RenderingServer.global_shader_parameter_add` or a `UniformSet`. This allows the Godot GPU to read the raw C++ memory directly, bypassing GDScript entirely.  
* **Execution Timing:** Godot maintains a `Timer` node for the Step phase. On timeout, it calls `SimulationManager.run_step()`. The C++ executes the Branchless ALU, updates the Storage Buffer, and the GPU draws the new frame instantly.

## **2\. THE FLOOR (Custom MultiMesh Shader)**

The ground (dirt, grass, water, bridges) is rendered as a single, massive flat grid using a `MultiMeshInstance3D` to achieve single-draw-call performance.

* **The Mesh:** The `MultiMesh` contains flat 1x1 quads (squares) laid out on the X/Z axis.  
* **The Shader:** A custom GDShader reads the Vulkan Storage Buffer based on its Instance ID (`INSTANCE_ID`).  
  * *Base Texture:* Reads Byte 1 (`Composition`) to sample the correct pixel art floor texture from a master Atlas.  
  * *Effect Compositing:* Reads Bytes 5-12 (`Effect Stack`). Applies translucent layers (e.g., Water bit \= blue overlay) and dyes (e.g., Poison bit \= green multiply).  
* **Elevation (The Z-Level Illusion):** The shader reads Byte 3 (`Absolute Elevation`). If a tile is at Elevation 2, but the tile directly "South" of it is at Elevation 1, the shader automatically draws a "Cliff Face" texture on the southern edge of the higher tile. The mesh itself remains perfectly flat on the GPU; the verticality is an optical illusion handled by the shader.

## **3\. ENTITIES & VERTICAL STRUCTURES (Sprite3D)**

To avoid complex MultiMesh UV math, units, trees, and multi-angle buildings use individual Godot nodes. To prevent Node bloat, they only exist for "Awake" chunks near the camera.

* **The Node:** `Sprite3D` configured with **Alpha Cutoff (Scissor)** to prevent transparent pixel Z-fighting in the 3D depth buffer.  
* **1-Angle Objects (Units, Trees):** `Billboard Mode` set to `Y-Billboard`. They will automatically swivel to face the camera.  
* **4-Angle Objects (Buildings):** `Billboard Mode` set to `Disabled`. A lightweight GDScript attached to the node reads the `SimulationManager`'s current Camera Angle (0, 90, 180, 270\) and updates the Sprite's `frame` property to show the correct side of the building.

## **4\. THE CAMERA SYSTEM (Cinematography)**

The camera utilizes a 3D perspective to create dynamic transitions between exploration and tactical combat.

* **Exploration Mode:** `Camera3D` uses standard Perspective projection (e.g., 70° FOV) positioned near the player. `CameraAttributesPhysical` applies Depth of Field (DoF) to blur the foreground and background, creating a miniature diorama effect.  
* **Combat Mode (The Dolly Zoom):** When combat begins, a `Tween` simultaneously:  
  1. Reduces FOV to \~5°.  
  2. Pulls the camera Z-position backward by hundreds of units to maintain subject size.  
  3. Reduces DoF Blur to 0.0. *Result:* The perspective lines mathematically crush flat, creating a razor-sharp, perfect isometric grid.  
* **Camera Rotation (The Bounce):** The camera does not smoothly orbit the map. To rotate 90 degrees:  
  1. The logical 1D grid layout coordinates are mathematically swapped in the renderer (e.g., `Logical_X` becomes `Map_Width - Y`).  
  2. The `Sprite3D` nodes run a rapid "squash and stretch" bounce animation.  
  3. At the apex of the bounce, the 4-angle sprites swap their frames. *Result:* The map appears to instantly spin, masking the paper-thin profile of 2D planes turning sideways.

# **ENTITY COMPONENT SYSTEM & COMBAT (TDD)**

**Architecture Type:** Structure of Arrays (SoA) / Pure Data-Oriented **Combat Paradigm:** Deterministic Integer Mathematics (Weight \+ Velocity) **Grid Interaction:** ECS modifies Grid via 64-bit Stack; ECS reads Grid via O(1) Lookups.

## **1\. ECS MEMORY STRUCTURE (Main RAM)**

Entities do not exist as Object-Oriented classes. They exist as parallel arrays of raw primitive data. A unit's "ID" is simply its index across these arrays.

### **The Parallel Arrays**

* `uint32_t Entity_Coordinates[MAX_UNITS]` \- Stores the exact 1D index of the unit on the Grid.  
* `uint8_t Entity_BaseWeight[MAX_UNITS]` \- The unit's innate Vigor/Mass.  
* `int8_t Entity_Velocity[MAX_UNITS]` \- The transient modifier (can be negative).  
* `uint8_t Entity_Team[MAX_UNITS]` \- Identifier for collision/combat logic (0 \= Player, 1 \= Enemy).  
* `uint8_t Entity_Flags[MAX_UNITS]` \- Bitmask for active abilities (Bit 0: Bounce, Bit 1: Push, Bit 2: Double Strike).

### **The Grid Bridge Arrays**

To allow the ECS to understand the physical world without complex logic, two globally cached arrays are kept in memory:

* `uint8_t EFFECT_WEIGHTS[64]` \- Maps the 64 Effect bits to physical Weights (e.g., `EFFECT_WEIGHTS[2] = 15` for Fire).  
* `uint8_t BIOME_WEIGHTS[256]` \- Maps the 256 Composition biomes to physical structural Weights (e.g., `BIOME_WEIGHTS[Stone_Wall] = 40`).

---

## **2\. DETERMINISTIC COMBAT RESOLUTION (The Clash)**

During the Step Execution Phase, combat is initiated automatically when two entities attempt to occupy the same Grid coordinate, or when an entity targets an occupied coordinate.

**The Equation:** `Total Force = BaseWeight + Velocity`

### **Standard Resolution Matrix**

1. **Attacker Force \> Defender Force:** Defender is destroyed, unless defender uses an ability like deflect or reflect that allows them to survive. Attacker successfully occupies the target tile.  
2. **Attacker Force \< Defender Force:** Attack fails. The Attacker's movement into the tile is canceled. Additional moves happen based on the unit, like bouncing away to a selected tile.   
3. **Attacker Force \== Defender Force (The Tie):** Mutual failure. Both units cancel their forward movement intent and trigger the "Bounce" fallback, unless they have other moves that happen here. 

### **The "Bounce" Fallback**

If an attack fails or results in a tie, AND the units have no respective moves that occur in this situation, the attacking unit's coordinate is reverted to its physical location from the *previous* step, effectively visually bouncing off the defender to create tactical space.

### **Additional moves Occur**

Moves   
---

## **3\. KINETIC COLLISIONS (The Domino Effect)**

If an entity possesses the `Push` flag, or is knocked back via an ability, it becomes a kinetic projectile.

### **Entity-to-Entity Domino**

If Unit A is pushed into a tile currently occupied by Unit B:

1. A secondary Clash is instantly calculated (`Unit A Force` vs `Unit B Force`).  
2. If A wins, B takes damage and is subsequently Pushed into the next tile along the vector.  
3. This recurses until a unit wins a defense check or an empty tile is reached.

### **Entity-to-Grid Crashing**

If Unit A is pushed into a tile where the Grid's `Impassable` flag (Byte 4, Bit 0\) is `1`:

1. The ECS performs an O(1) lookup on `BIOME_WEIGHTS[Target_Tile.Composition]`.  
2. **If Unit Force \> Biome Weight:** The ECS injects the `SHOCKWAVE` command bit into the target tile's 64-bit Effect Stack. On the next ALU loop, the tile is physically destroyed/altered, and Unit A moves into the space.  
3. **If Biome Weight \> Unit Force:** The Wall survives. Unit A's movement stops, and it suffers "Crush Damage" equal to the differential.

---

## **4\. PROJECTILE LOGIC (Step-by-Step Traversal)**

Projectiles (Arrows, Targeted Magic) exist inside the ECS arrays identically to units, simply possessing high Velocity and low BaseWeight.

### **Trajectory Mathematics (Bresenham's Line)**

Because the Grid is discrete, projectiles do not use floating-point vectors.

* When a projectile is fired, the ECS calculates the path from Origin to Target using **Bresenham's Line Algorithm**.  
* This generates a finite queue of 1D memory indices (the flight path).  
* The trajectory may appear jagged (e.g., stepping North, then North-East, then North) but guarantees absolute mathematical adherence to the discrete grid.

### **Flight Execution & Interception**

* **Step Phase:** The projectile advances 1 or more tiles along its Bresenham queue.  
* **Grid Interception:** Before moving, the ECS checks the target tile's 64-bit Effect Stack and Composition. If a physical barrier (e.g., Ice Pillar) or effect (e.g., Updraft) is present, the projectile initiates a Clash against the `BIOME_WEIGHT` or `EFFECT_WEIGHT`.  
* **Instant Speed Disruption:** Because projectiles travel step-by-step, players can pause execution between steps to physically alter the grid in front of the projectile, forcing a collision on the subsequent step.

---

# **HYBRID TOOLING PIPELINE (TDD)**

**Architecture Type:** C++ Backend Generation \+ Godot Frontend Parsing **Goal:** Provide maximum development speed by utilizing Godot’s native 2D/3D editor tools to author levels, while compiling all data down to the strict 16-byte C++ memory array for runtime execution.

## **1\. PROCEDURAL GENERATION (The Open World)**

For massive map generation (e.g., 1,000,000 tiles), all generation logic occurs exclusively in the C++ backend to prevent GDScript bottlenecking.

* **The Library:** Integrate a lightweight, fast noise library (e.g., `FastNoiseLite` C++ implementation) directly into the GDExtension.  
* **The Process:** A dedicated `WorldGenerator` C++ class loops sequentially through the 1D `Grid_Array`.  
  * It samples 2D noise using the `(X, Z)` coordinates to determine `Absolute_Elevation`.  
  * It uses cellular automata or secondary noise maps to assign base biomes to the `Composition` byte.  
* **Execution:** Triggered via a single GDScript call: `SimulationManager.generate_new_world(seed)`.

## **2\. THE TILEMAP COMPILER (Handcrafted Dungeons)**

For tight, puzzle-box dungeons (e.g., 16x16 or 32x32), level designers use Godot's native 2D `TileMapLayer` node to draw the rooms visually.

* **The Mapping Dictionary:** A GDScript dictionary maps Godot `TileData` (or Atlas Coordinates) to your C++ engine's Biome Indices (e.g., `Vector2(1, 0): 40 // Stone Wall`).  
* **The Compiler Script:** An `@tool` script attached to the Godot Scene. When the designer clicks a custom "Bake Level" button:  
  1. The script loops through every used cell in the `TileMapLayer`.  
  2. It translates the Godot visual tile into the 16-byte C++ format.  
  3. It strips away all Godot Node data and packs the raw bytes into a custom binary file (e.g., `dungeon_01.dat`).  
* **Runtime Loading:** The C++ backend loads the `.dat` file directly into its active memory array, completely bypassing the Godot Scene Tree at runtime.

## **3\. THE 3D EDITOR CHISEL (Live Overworld Editing)**

To allow designers to manually place boss arenas or POIs inside the generated 3D open world, a custom Godot Editor Plugin modifies the C++ memory array in real-time.

* **The Plugin:** An `EditorPlugin` script adds a "Map Editor" dock to the Godot UI, containing a dropdown of Biome Types.  
* **The Raycast:** When the designer clicks the 3D viewport, Godot shoots a ray from the editor camera to the collision plane.  
* **The Coordinate Math:** The script converts the 3D `(X, 0, Z)` intersection into a discrete 1D integer index: `Index = (Z * Map_Width) + X`.  
* **The Bridge:** The script calls a GDExtension function: `SimulationManager.editor_set_tile(Index, Biome_ID)`. The C++ updates the array, and the MultiMesh shader instantly reflects the change in the editor viewport.

## **4\. BINARY SERIALIZATION (Save & Load)**

Because the map is a tightly packed, contiguous `std::vector` of 16-byte structs, saving the game state bypasses standard JSON or XML serialization.

* **Save:** The C++ engine opens a file stream (`std::ofstream`) in binary mode and dumps the entire `Grid_Array` directly to the disk in a single operation.  
* **Load:** The engine allocates the memory, opens the file (`std::ifstream`), and reads the exact byte count back into RAM.  
* **Performance:** A 16MB map saves and loads almost instantaneously, bound only by the user's SSD read/write speeds.

# **Top Down View**

### **1\. The Data: The 16-Byte Tile (The Physical Reality)**

This is the only data that physically exists in the 16MB grid memory. It holds state, but *no logic*.

* **Byte 1 (`composition`):** The base physical biome ID (0-255). **(This is the persistent memory of the world. It acts as your "timer" and "generator").**  
* **Byte 2 (`slope_geometry`):** The 3D shape for visual rendering and slope logic.  
* **Byte 3 (`absolute_elevation`):** Z-height (0-255). Dictates physical gravity and fluid flow.  
* **Byte 4 (`pathing_scent`):** AI pathfinding costs and impassable flags.  
* **Bytes 5-12 (`effect_stack`):** The 64-bit dynamic state. **(This is strictly transient. It only holds elements while they are actively clashing or moving, and is wiped at the end of the turn).**  
* **Byte 13 (`ambient_mana`):** The 8-bit mask of available spellcasting fuel.  
* **Bytes 14-16 (`padding`):** Empty space to guarantee hardware L1 Cache alignment.

### **2\. The Rules: The Constellation of Tables (The Global Memory)**

Because the Grid contains no logic, we use incredibly small, globally accessible 1D and 2D arrays to define the physics of the universe.

* **`CHEMISTRY_PAIRS[64][64]` (The Combiner):**  
  * *Purpose:* Defines what happens when two bits exist simultaneously.  
* **`ANNIHILATION_MATRIX[64]` (The Destroyer):**  
  * *Purpose:* A 1D array of bitmasks defining what a specific bit instantly deletes.  
* **`CARRY_MASK[16]` (The Transporter):**  
  * *Purpose:* Defines which Modifiers can stick to which Elements during propagation.  
* **`BIOME_WEIGHTS[256]` & `EFFECT_WEIGHTS[64]` (The Combat Bridge):**  
  * *Purpose:* Maps physical ground and magical effects to Mass for the ECS combat math.  
* **`BIOME_TRANSITIONS[256][64]` (The Reality Editor) *\[NEW\]*:**  
  * *Purpose:* Maps the current `composition` and the surviving `effect_stack` to a brand new Biome. (e.g., `[Grass][Fire] = Ash`).

### **3\. The Pipeline: The Final Execution Order (The Symphony)**

When the Chunk Manager wakes up a 32x32 area, this is the unyielding order of operations that guarantees zero race conditions and enables tactical "Wile E. Coyote" saves.

1. **The Pre-Step (Double Buffer Injection) *\[NEW\]*:** Active Biomes (e.g., Burning Wood, Poison Vent) write their respective Elemental Bits directly into the *Write Buffer* for the next frame, safely skipping the current frame's math.  
   TODO: how does this work, and is this how we have biomes that interact with the world?  
2. **The Watchpoint (Interrupts):** *Does the player have an Instant targeted here?* If yes, halt. Players can place effects into stacks near them at this point.   
3. **The ECS Execution (Combat & Gravity) *\[NEW\]*:**  
   * *Tactical Gravity:* Check the unit's elevation against the tile. If the unit is hovering over a pit (from a bridge broken last turn), they fall.  
   * *Combat:* Resolve Weight/Velocity clashes, Bounces, and Pushes, including effects on tiles. Units aren’t affected after this step, instead are stuck in “Wile E” mode, allowing chances to respond to new threats in their new situation.   
4. **The Annihilation (Destruction):** Run the `effect_stack` through the `ANNIHILATION_MATRIX`. (Counter-spells clear out elemental attacks, wind spell stops projectile).  
5. **The Chemistry (Transformation):** Run the `effect_stack` through `CHEMISTRY_PAIRS`. (Elements combine).  
6. **The Propagation (Fluid Flow):** The surviving Elements check the `CARRY_MASK` to grab their sticky Modifiers, check the `absolute_elevation` around them for gravity, and write their movement into the temporary Write Buffer.  
   * To limit issues, maybe relegate the spreading kinds of elements to a certain number, like maybe only a bytes worth of effects are considered “spreadable” like large fire spreads, small fire doesn’t.  
   * These also each have different rules, some spread by geography (water) others by biome (fire)  
     1. Using these effects on more magical substances could be very cool.   
7. **The World Mutation (Cellular Automaton) *\[NEW\]*:** \* Does the surviving stack alter the actual `composition` via the `BIOME_TRANSITIONS` table? Update the Biome. (Shockwave knocks down a building, fire burns a forest)  
   * **WIPE THE STACK.** The Tile is wiped completely clean of 64-bit effects, ready to receive the Write Buffer for the next step.  
8. **Biomes Propagate:** Every step, 3 random tiles check their neighbors for evolution rules, like ground \+ forest \= sapling replaces ground. 

# **ECOSYSTEM & BIOLOGICAL LOOP (TDD ADDENDUM)(EDIT)**

**Architecture Type:** Data-Oriented Design (DOD) / Discrete Cellular Automata **Paradigm:** Excitable Media / Binary State AI **Goal:** Create a self-sustaining, thermodynamic ecosystem that dictates AI migration and biome mutation without floating-point math, complex state machines, or interference with the kinetic combat engine.

---

## **1\. MEMORY MAP UPDATES**

To support biological loops without expanding the 16-byte chunk, we utilize the remaining padding and expand the flat ECS arrays.

### **The Grid: The Imprint Field (Bytes 14-15)**

The previously reserved padding bytes are combined into a single `uint16_t`. This acts as a localized, 16-bit Excitable Media Cellular Automaton.

* **Bits 0-15:** Divided into eight 2-bit pairs. Each pair corresponds exactly to the 8 bitflags in `Ambient_Mana` (Byte 13).  
* This creates a distinct, mathematically propagating "scent wave" for every type of mana in the game.

### **The ECS: Diet and Binary States**

Three additions to the parallel entity arrays to govern biological needs:

* `uint8_t Entity_Diet[MAX_UNITS]` \- A bitmask aligning with `Ambient_Mana`. Defines what a unit eats (e.g., `00000001` \= Plant Eater, `00000100` \= Fire Eater).  
* `uint8_t Entity_Timers[MAX_UNITS]` \- A simple countdown array for biological refractory periods.  
* `Entity_Flags[MAX_UNITS]` (Update) \- Addition of `constexpr uint32_t FLAG_SATED = 1 << 3;`.

---

## **2\. THE IMPRINT FIELD (Excitable Media CA)**

To allow herbivores to track specific mana types over long distances without infinitely flooding the map, the Imprint Field executes a 4-state CA wave.

### **The 4 States (Per 2-Bit Pair)**

* `11` (3): **Source** (The physical object emitting the signal, e.g., a Tree).  
* `10` (2): **Pulse** (The active wavefront).  
* `01` (1): **Trail** (The cooling refractory state).  
* `00` (0): **Empty** (Neutral grid).

**Propagation Rules (The Bitwise OR & Degrade)** To avoid the SIMD bottleneck of calculating "Exactly One Neighbor", the Imprint Field uses a hyper-fast, cascading `OR` degradation rule.

* `11` (3): **Source** \* `10` (2): **Pulse** \* `01` (1): **Trail** \* `00` (0): **Empty** **The Branchless Loop:**  
1. **The Merge:** The CPU shifts the states of the N, S, E, W neighbors and bitwise `OR`s them together to see if an active wave exists nearby.  
2. **The Degrade:** \* If `00 (Empty)` AND the neighbor `OR` check contains a Pulse/Source \-\> Becomes `10 (Pulse)`.  
   * If `10 (Pulse)` \-\> Unconditionally degrades to `01 (Trail)`.  
   * If `01 (Trail)` \-\> Unconditionally degrades to `00 (Empty)`.  
   * If `11 (Source)` \-\> Unconditionally remains `11 (Source)`.

Because `Pulse` leaves a `Trail` that cannot be immediately overwritten, the wave is forced to expand outward as a hollow ring. This eliminates neighbor-counting, reducing the AVX2 math to under 5 clock cycles per 16 tiles.

---

## **3\. THE BIOLOGICAL LOOP (Feeding & Regrowth)**

The ecosystem is driven by the direct consumption and mutation of the Grid's base states (`Composition` and `Ambient_Mana`).

### **A. Herbivore Feeding (The Consumption Action)**

When an herbivore steps onto a tile, it attempts to feed via a bitwise `AND` operation: `if (Grid[pos].Ambient_Mana & Entity_Diet[id])`

1. The matching `Ambient_Mana` bit is cleared from the tile.  
2. The tile's `Composition` (Byte 1\) is forcefully overwritten to `Barren` (or an equivalent depleted state).  
3. The herbivore is fully sated.

### **B. World Mutation (The Regrowth Action, WIP)**

During **Step 8**, the Grid heals itself naturally.

1. Random tiles are selected on the map. If there is a plant mana pulse on the grid, a plant can grow there.

---

## **4\. DATA-ORIENTED AI & Scent Tracking**

AI logic utilizes bit-shifts and boolean flags during the Planning Phase, ensuring ultra-fast intent calculation without complex state machines.

### **A. Herbivore Pathing (Riding the Wave)**

Herbivores do not calculate paths; they "bob upstream" against the Imprint Field wave.

1. The AI reads the 2-bit state of its 4 orthogonal neighbors corresponding to its `Entity_Diet` index.  
2. **Priority 1:** If a neighbor is `Source (11)`, step toward it to eat.  
3. **Priority 2:** If the herbivore is standing on or near a `Pulse (10)`, it looks for a `Trail (01)`. It steps onto the `Trail`, knowing mathematically the Pulse occupied that space one step prior, leading directly toward the Source.  
4. **Priority 3:** Wait for the next wave to pass.

If they are not hungry, this behavior changes. They instead “surf the wave” and orbit the food or return to their biome. 

### **B. Carnivore Pathing & Satiation (Binary AI Inversion)**

Carnivore aggression is dictated entirely by a binary flag, dictating how it reads Byte 4 (`pathing_scent`).

**The Kill Event:** If a Carnivore wins a Clash against a prey entity: `Entity_Flags[predator_id] |= FLAG_SATED;` `Entity_Timers[predator_id] = PRESET_DURATION;`

**The Planning Phase:**

* `if (!(Entity_Flags[id] & FLAG_SATED))` **(Hungry):** The carnivore reads Byte 4 (Prey/Player Scent) and paths toward the highest concentration.  
* `if (Entity_Flags[id] & FLAG_SATED)` **(Sated):** The carnivore ignores Byte 4 entirely. It searches for `Empty (00)` tiles or a designated "Den" imprint to sleep off the meal, visually turning away from the player.

**Timer Resolution:** At the end of the Execution Phase, the engine sweeps the `Entity_Timers` array. If a timer hits 0, `Entity_Flags[id] &= ~FLAG_SATED;`, and the carnivore begins hunting again on the next turn.

# **SHADOW BUFFER CA PIPELINE**

The global architecture remains a strict 16-byte `std::vector<Tile>`. To process the 16-bit Cellular Automata, the engine provisions a static, thread-local memory buffer sized exactly for a 32x32 chunk (1,024 tiles), plus a 1-tile border (padding) to handle neighbor lookups without bounding checks.

C++  
// 34x34 to include a 1-tile border for N/S/E/W lookups  
// alignas(32) guarantees perfect AVX2 memory alignment  
alignas(32) thread\_local uint16\_t Shadow\_Buffer\[1156\];   
alignas(32) thread\_local uint16\_t Result\_Buffer\[1156\];

### **SHADOW BUFFER CA PIPELINE (Updated for SIMD Alignment)**

**Architecture Type: Temporal SoA / L1 Cache Shadow Buffer Goal: Maximize Cellular Automata throughput by restructuring AoS memory into AVX2-aligned SoA memory inside the CPU's L1 cache, avoiding hardware segfaults via Stride Padding.**

**A. The AVX2 Memory Stride To utilize `_mm256_load_si256` (Aligned Loads), memory rows must be strictly divisible by 32 bytes (16 `uint16_t` tiles). A 32x32 chunk with a 1-tile border requires 34 tiles per row, which breaks alignment and causes a hardware crash.**

**To fix this, the Shadow Buffer is horizontally padded to 48 tiles wide. The SIMD processor will read the ghost cells and safely ignore them.**

**C++**

**// 48x34 grid (Width padded to nearest multiple of 16 for AVX2 alignment)**

**// alignas(32) guarantees the starting address is perfectly aligned.**

**alignas(32) thread\_local uint16\_t Shadow\_Buffer\[1632\];** 

**alignas(32) thread\_local uint16\_t Result\_Buffer\[1632\];**

**B. The Shadow Pipeline**

1. **The Extraction Pass (AoS \-\> SoA): A scalar loop reads the `Imprint_Field` (Bytes 14-15) from the main Grid and packs them into the `Shadow_Buffer`. The remaining 14 padding tiles per row are left as `0`.**  
2. **The SIMD Pass: The CPU fetches 16 tiles simultaneously. Because the row width is exactly 48, the SIMD cursor never misaligns when wrapping to the next row.**  
3. **The Injection Pass: The scalar loop writes the calculated `Result_Buffer` (ignoring the padding) back into the 14th and 15th bytes of the master Grid.**

## **1\. ECS MEMORY MAP ADDITIONS**

To support dynamic evolution without object-oriented overhead, a single byte array is added to the Entity Component System.

* `uint8_t Entity_Mutation[MAX_UNITS];`  
  * Stores *only* the acquired, foreign mana bits an entity consumes outside of its native `Entity_Diet`.  
  * Initialized to `0` upon spawning.

## **2\. THE EVOLUTION TRIGGERS (Bitwise Inheritance)**

Evolution is triggered during the Execution Phase (Step 3: The Clash / Step 7: Feeding). It relies on identifying foreign mana and merging it into the entity's genetic makeup.

### **A. Herbivore Evolution (The XOR Delta)**

When an herbivore feeds on a tile, it compares the tile's `Ambient_Mana` against its own `Entity_Diet`.

1. **The Delta Calculation:** `uint8_t foreign_mana = (Grid[pos].Ambient_Mana ^ Entity_Diet[id]) & Grid[pos].Ambient_Mana;` *(Ensures we only grab bits the tile has that the entity doesn't).*  
2. **The Inheritance:** If the tile contained foreign mana, the herbivore absorbs it: `Entity_Mutation[id] |= foreign_mana;`

### **B. Carnivore Evolution (The OR Merge)**

Because a carnivore's diet is the act of hunting, it inherits the complexity of whatever it kills.

1. **The Kill:** A Wolf successfully Clashes with and kills an herbivore.  
2. **The Inheritance:** The Wolf absorbs the prey's total accumulated identity. `uint8_t prey_identity = Entity_Diet[prey_id] | Entity_Mutation[prey_id];` `uint8_t foreign_meat = (prey_identity ^ Entity_Diet[wolf_id]) & prey_identity;` `Entity_Mutation[wolf_id] |= foreign_meat;`

## **3\. THE 3-TIER EVOLUTION SYSTEM (Foreign Popcount)**

An entity's evolutionary tier is dictated strictly by the number of *extra* mana bits it has acquired. This is calculated branchlessly using the compiler intrinsic `__builtin_popcount()`.

**The Complexity Calculation:** `uint8_t extra_mana = __builtin_popcount(Entity_Mutation[id]);`

**The Tiers:**

* **Tier 1 (Base):** `extra_mana == 0` (The entity has only consumed its native diet).  
* **Tier 2 (Evolved):** `extra_mana >= 1` (The entity has absorbed 1 or 2 foreign mana types).  
* **Tier 3 (Apex):** `extra_mana >= 3` (The entity has absorbed 3 or more foreign mana types).

Recalculating an entity's evolutionary tier using `__builtin_popcount` during combat or movement loops wastes CPU cycles on static data.

**ECS Memory Additions:**

* `uint8_t Entity_Mutation[MAX_UNITS]` \- Stores the acquired foreign mana bits.  
* `uint8_t Entity_Tier[MAX_UNITS]` \- Caches the popcount integer (0, 1, 2, 3+).

**The Evolution Trigger:** When a unit successfully feeds or kills, the engine recalculates the Tier *once*.

C++

// Inside the kill\_entity / feeding logic:

Entity\_Mutation\[predator\_id\] |= foreign\_mana;

// Recalculate and Cache

Entity\_Tier\[predator\_id\] \= \_\_builtin\_popcount(Entity\_Mutation\[predator\_id\]);

// Immediately update the physical stat arrays based on the new Tier

Entity\_BaseWeight\[predator\_id\] \= calculate\_new\_weight(Entity\_Tier\[predator\_id\]);

Entity\_ActionPoints\[predator\_id\] \= calculate\_new\_speed(Entity\_Tier\[predator\_id\]);

During the 60 FPS combat Clash, the engine simply reads the pre-scaled `Entity_BaseWeight`, keeping the physics resolution branchless and devoid of complex math.

## **6\. ENTITY EVOLUTION & METABOLISM (LUT-Based Branching)**

This section overrules previous sections where applicable. 

**Architecture Type:** $O(1)$ Sparse Matrix & Branchless Two's Complement Masking

**Goal:** Support infinite biological permutations without causing an art-pipeline bottleneck. Entities can consume any mana type. If a specific 3D model/branch exists for that interaction, they change species. If it does not, they mutate their underlying stats natively.

### **A. Memory Map & Global Registries**

Evolution merges the Grid's "Composition" philosophy with the Entity Component System.

**1\. ECS Array Additions:**

* std::vector\<uint8\_t\> entity\_species: The base identifier (0-255). Directly maps to a specific Godot 3D mesh (e.g., 10 \= Wolf, 45 \= Hellhound).  
* std::vector\<uint8\_t\> entity\_mutation: A bitmask storing all consumed foreign mana bits.  
* std::vector\<uint8\_t\> entity\_tier: Cached \_\_builtin\_popcount(entity\_mutation).

**2\. The Global DNA Lookup Tables (Loaded at Startup):**

* uint8\_t SPECIES\_BASE\_WEIGHT\[256\]  
* int8\_t SPECIES\_BASE\_VELOCITY\[256\]  
* uint8\_t EVOLUTION\_PATHS\[256\]\[8\]: The 2-Kilobyte Sparse Matrix. \[Current\_Species\]\[Consumed\_Mana\_Bit\] returns the New\_Species\_ID. If no authored path exists, it returns 0.

### **B. The Dual-Path Paradigm**

When an entity consumes a tile's ambient\_mana, the engine processes two realities simultaneously and uses bitwise masks to apply the correct outcome based on the EVOLUTION\_PATHS lookup.

* **Reality A (True Evolution):** The LUT returns a valid ID. The entity becomes a brand new species. Its entity\_mutation stack is wiped to 0, its entity\_tier drops to 0, and it inherits the base physical stats of the new species from the global LUTs.  
* **Reality B (Metabolic Mutation):** The LUT returns 0. The entity retains its current species, but the consumed mana bit is injected into its entity\_mutation stack. Its entity\_tier increases, applying a generic scaling formula (e.g., \+20 Weight, \-1 Velocity) to its *current* stats.

### **C. The Branchless Execution Pipeline**

The trigger\_feeding function must execute without if/else statements to preserve the CPU pipeline.

1. **The $O(1)$ Fetch:** Query EVOLUTION\_PATHS using the entity's current species and the consumed mana bit.  
2. **Mask Generation:** Generate two All-or-Nothing masks based on whether the fetched ID is \> 0.  
   * evo\_mask (0xFF if true, 0x00 if false).  
   * mut\_mask (the bitwise NOT of the evo\_mask).  
3. **Resolve Identity & Stack:** Apply the masks to overwrite entity\_species. Use the mut\_mask against the updated mutation stack; this naturally wipes the stack to 0 if an evolution occurred, or preserves the new mana bit if it was a generic mutation.  
4. **Resolve Tier:** Run \_\_builtin\_popcount() on the newly resolved mutation stack. Because of Step 3, this mathematically cascades perfectly (returning 0 for fresh evolutions, or the correct new tier for mutations).  
5. **Resolve Physics:** Calculate the physical stats for Reality A (LUT values) and Reality B (current stats \+ tier modifiers). Use the masks to commit the correct stats to the ECS.

### **D. The Godot Rendering Bridge**

This architecture completely decouples visual logic from physical logic.

* Godot reads the entity\_species byte to load the core .gltf model (e.g., if 10, load wolf.gltf).  
* Godot reads the entity\_mutation byte to apply dynamic shaders or particle effects on top of that base model (e.g., if the FIRE\_BIT is active in the mutation stack, attach a fire particle emitter to the wolf's spine).  
* This allows the player to visually identify a creature's capabilities even if it hasn't formally evolved into a new handcrafted species.

Here is the flawless, 100% branchless C++ implementation:

C++

inline void trigger\_feeding(uint16\_t entity\_id, int tile\_index, uint8\_t mana\_bit\_index) {

    uint8\_t current\_species \= entity\_species\[entity\_id\];

    

    // O(1) Array Lookup. Returns 0 if no path exists.

    uint8\_t new\_species \= EVOLUTION\_PATHS\[current\_species\]\[mana\_bit\_index\];

    // 1\. GENERATE THE ALL-OR-NOTHING MASKS

    // If new\_species \> 0:  evo\_mask \= 0xFF (All 1s), mut\_mask \= 0x00 (All 0s)

    // If new\_species \== 0: evo\_mask \= 0x00 (All 0s), mut\_mask \= 0xFF (All 1s)

    uint8\_t evo\_mask \= \-static\_cast\<uint8\_t\>(new\_species \!= 0);

    uint8\_t mut\_mask \= \~evo\_mask;

    // 2\. RESOLVE SPECIES

    entity\_species\[entity\_id\] \= (new\_species & evo\_mask) | (current\_species & mut\_mask);

    // 3\. RESOLVE MUTATION STACK

    // If evolving, mut\_mask is 0x00, so the mutation stack is instantly wiped to 0\.

    // If mutating, mut\_mask is 0xFF, so the new mana bit is safely added.

    uint8\_t mutated\_stack \= entity\_mutation\[entity\_id\] | (1 \<\< mana\_bit\_index);

    entity\_mutation\[entity\_id\] \= mutated\_stack & mut\_mask;

    // 4\. RESOLVE TIER (The Popcount Cascade)

    // Because we just wiped the mutation stack to 0 if evolving, 

    // the popcount will naturally return 0\! No mask needed\!

    uint8\_t old\_tier \= entity\_tier\[entity\_id\];

    uint8\_t new\_tier \= \_\_builtin\_popcount(entity\_mutation\[entity\_id\]);

    entity\_tier\[entity\_id\] \= new\_tier;

    // 5\. RESOLVE PHYSICAL STATS

    // Did the tier go up? (Returns 1 if yes, 0 if no)

    uint8\_t tier\_diff \= new\_tier \- old\_tier; 

    // Calculate Reality A: True Evolution

    // (Note: If new\_species is 0, it safely reads index 0 of the global array)

    uint8\_t evo\_weight \= SPECIES\_BASE\_WEIGHT\[new\_species\];

    int8\_t  evo\_vel    \= SPECIES\_BASE\_VELOCITY\[new\_species\];

    // Calculate Reality B: Metabolic Mutation

    uint8\_t mut\_weight \= entity\_base\_weight\[entity\_id\] \+ (20 \* tier\_diff);

    int8\_t  mut\_vel    \= entity\_velocity\[entity\_id\] \- tier\_diff;

    // Branchless Commit: The masks instantly delete the false reality.

    entity\_base\_weight\[entity\_id\] \= (evo\_weight & evo\_mask) | (mut\_weight & mut\_mask);

    entity\_velocity\[entity\_id\]    \= (evo\_vel & evo\_mask)    | (mut\_vel & mut\_mask);

}

## **4\. PHYSICAL BALANCING & METABOLISM**

To prevent Apex predators from infinitely sweeping the map and to give the non-evolving Player a tactical advantage, an entity's physical stats and metabolism scale automatically with its tier.

### **A. Inverse Kinematics (Weight vs. Speed)**

Whenever an entity's `extra_mana` tier increases, its physics profile updates:

* `BaseWeight` increases dramatically, making it nearly impossible to defeat in a direct kinetic Clash.  
* `Action_Points` (Speed) strictly decrease. A Tier 1 unit may move multiple tiles per turn, while an Apex beast becomes a lumbering juggernaut. Its low speed allows the player to kite it, outmaneuver it, or trap it using 3D terrain modification.

### **B. The Metabolic Burn (Satiation Scaling)**

A heavier, mutated unit requires vastly more ecological energy to sustain its mass.

* The `Entity_Timers` array tracks starvation.  
* When calculating the timer decay at the end of a turn, the base decay is multiplied by `(1 + extra_mana)`.  
* An Apex beast will burn through its Satiation timer significantly faster. If it wipes out its local biome, its slow speed prevents it from reaching a new biome in time, causing it to naturally starve and restoring balance.

## **5\. THE 3D GODOT RENDERING BRIDGE**

The C++ backend processes everything as a flat 2D data grid. It passes the 10-byte Entity Struct (including `BaseWeight`, `Entity_Diet`, and `Entity_Mutation`) over the Vulkan bridge to the Godot frontend. Godot translates this data into a rich 3D environment.

1. **3D Mesh Swapping:** A lightweight Godot script attached to the entity's visual node reads the `extra_mana` popcount.  
   * If `0`, instantiate `base_mesh.gltf`.  
   * If `1` or `2`, instantiate `dire_mesh.gltf`.  
   * If `3+`, instantiate `apex_mesh.gltf` and increase the 3D scale transform.  
2. **Dynamic Spatial Materials:** A Godot Spatial Shader reads the `Entity_Mutation` byte. It applies additive visual effects to the 3D mesh based on the active bits.  
   * If the `Magic` bit is active, it enables a purple emission map on the material.  
   * If `Fire` is active, it spawns a 3D GPU Particle system (flames) attached to the creature's bones.  
   * The 3D visuals dynamically stack to match the exact bitwise inheritance the engine calculated.

# **(ADDENDUM: SCALED UNIT INTERACTIONS)**

**Architecture Type:** Data-Oriented Spatial Partitioning / $O(1)$ Occupancy Grid

**Execution Phase:** Step 3 (The Clash) / Step 5 (Movement)

**Goal:** Resolve kinetic clashes and movement for thousands of entities simultaneously across a global map without iterating through entity-to-entity loops, maintaining microsecond execution times.

## **1\. THE $O(N^2)$ BOTTLENECK AVOIDANCE**

In a standard Object-Oriented engine, checking for collisions requires looping through the active entity list and comparing spatial coordinates. If 10,000 units move, the CPU performs up to 100,000,000 coordinate checks per step. This crashes the framerate.

To solve this, Cressplusplus utilizes **Spatial Partitioning** where the Grid itself acts as the definitive matchmaking authority. Entities never check other entities; they only ask the Grid who is standing on a specific tile.

## **2\. MEMORY MAP: THE OCCUPANCY LAYER**

To keep the primary 16-byte Tile struct pristine for Vulkan and the Cellular Automata, entity collision is handled by a parallel, flat 1D array that perfectly mirrors the map's geometry.

* uint16\_t Occupancy\_Grid\[MAX\_TILES\];  
  * This array acts as a registry.  
  * The value stored at any index is the Entity\_ID of the unit currently occupying that physical space.  
  * A value of 0 indicates the tile is empty (Entity IDs begin at 1).

*Because Occupancy\_Grid is a contiguous block of uint16\_t, checking a tile's occupancy is a single, $O(1)$ direct memory fetch that executes instantly.*

## **3\. THE EXECUTION PIPELINE (Branchless Matchmaking)**

During the Execution Phase, entities attempt to execute the movement vectors they locked in during the Planning Phase. The engine processes these intents using the Occupancy Grid.

### **Phase A: The Read (Intent Check)**

When Unit A attempts to move to a target tile:

1. The engine performs a direct lookup: uint16\_t target\_id \= Occupancy\_Grid\[target\_pos\];  
2. If target\_id \== 0: The tile is empty. The movement succeeds immediately.  
3. If target\_id \!= 0: A physical Clash is initiated between Unit A and target\_id.

### **Phase B: The Clash Resolution (Kinetic Math)**

Because the engine now has both IDs, it completely bypasses spatial searching and jumps directly into the flat ECS arrays to compare stats.

C++

// 1\. Pull BaseWeights from the ECS

uint8\_t weight\_attacker \= Entity\_BaseWeight\[attacker\_id\];

uint8\_t weight\_defender \= Entity\_BaseWeight\[defender\_id\];

// 2\. Resolve Math

if (weight\_attacker \> weight\_defender) {

    // Attacker wins.

    kill\_entity(defender\_id);

    move\_entity(attacker\_id, target\_pos);

} else {

    // Defender wins or Tie. Check for FLAG\_BOUNCE.

    if (Entity\_Flags\[attacker\_id\] & FLAG\_BOUNCE) {

        trigger\_bounce(attacker\_id); 

    } else {

        kill\_entity(attacker\_id);

    }

}

### **Phase C: The Write (Updating the Grid)**

When a unit successfully moves or is killed, the Occupancy Grid must be updated to maintain the integrity of the spatial registry.

1. **Movement:** The unit writes its ID to the new tile and clears its old tile.  
   Occupancy\_Grid\[new\_pos\] \= entity\_id;  
   Occupancy\_Grid\[old\_pos\] \= 0;  
2. **Death:** The kill\_entity() function automatically clears the Occupancy Grid at the deceased unit's location, freeing the tile for the next unit.

### **SCALED UNIT INTERACTIONS (Updated for Sync Safety)**

To prevent the parallel `Occupancy_Grid` and the 16-byte master `Grid` from falling out of sync, the ECS operates through a strictly enforced inline wrapper.

**The Unified Movement Wrapper:**

C++

inline void move\_entity(uint16\_t entity\_id, uint32\_t old\_pos, uint32\_t new\_pos) {

    // 1\. Update the O(1) Matchmaking Grid (For Clashes/Pathing)

    Occupancy\_Grid\[old\_pos\] \= 0;

    Occupancy\_Grid\[new\_pos\] \= entity\_id;

    // 2\. Update the 16-Byte Hardware Grid (For Grid ALU Damage/Traps)

    Grid\[old\_pos\].effect\_stack &= \~FLAG\_HAS\_ENTITY; 

    Grid\[new\_pos\].effect\_stack |= FLAG\_HAS\_ENTITY;  

    

    // 3\. Update the ECS Spatial Array (For Rendering/Logic)

    Entity\_Coordinates\[entity\_id\] \= new\_pos;

}

If an entity is destroyed during a Clash, the `kill_entity()` function runs the exact same three-part clearing process.

## **4\. CHUNK-BASED ECS TRACKING (For Godot Rendering)**

While the math is handled globally, the Vulkan renderer cannot draw 50,000 entities across a 1,000,000-tile map without culling them. The C++ backend must feed Godot only the entities that exist within the active camera bounds.

Instead of iterating through MAX\_UNITS to find out who is on camera, the engine uses Localized Chunk Registries.

* **The Registry:** Every 32x32 Chunk Manager maintains a lightweight, dynamic array of Entity\_IDs representing only the units currently standing inside its borders.  
* **The Hand-off:** When a unit's movement crosses a chunk boundary, the move\_entity() function removes the ID from the old Chunk's registry and appends it to the new Chunk's registry.  
* **The Render Call:** When Godot asks the engine, "What do I render?", the C++ bridge simply passes the registries of the visible chunks. Godot ignores the rest of the world, allowing the global simulation to run at full speed in the background without incurring any rendering overhead.

