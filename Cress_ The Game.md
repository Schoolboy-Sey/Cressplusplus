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

### **Byte 14-16: RESERVED/ PADDING (uint8\_t)** Keeps the tile in a 16byte chunk for easiest CPU processing, can use for later. 

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

