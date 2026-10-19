# Native HD-2D RPG & Diorama Engine — Architectural Handoff & Bootstrap Spec

> [!IMPORTANT]
> **Purpose of This Document:** This specification is a complete context handoff for starting a new **Octopath Traveler / Lumina-inspired 2.5D (HD-2D)** engine in a fresh Antigravity session. It captures our established stack, architectural findings from benchmarking `stubborn-hug/lumina`, reusable C++/WebGPU systems already built in `C:\git\eatsworlds`, and engine naming candidates.

---

## 1. Engine Naming Suggestions

Since our ecosystem includes **Eatsbits**, **Eatscript**, and **Eatsworlds** (now dedicated to the pure 3D voxel diorama engine), here are curated naming options for the new **2.5D HD-2D RPG Engine**, grouped by thematic direction:

### A. The `Eats*` Family (Direct Brand Continuity)
| Name | Rationale & Vibe |
| :--- | :--- |
| **Eatsrealms** *(Top Pick)* | Naturally pairs with *Eatsworlds* while signaling a stronger focus on **RPG realms, towns, quests, NPCs, and monsters** rather than raw terrain blocks. |
| **Eatstales** | Evokes classic Square Enix / *Octopath Traveler* storytelling, party journeys, and living storybook dioramas. |
| **Eatslore** | Short, punchy, and immediately communicates an RPG, worldbuilding, and narrative focus. |
| **Eatschronicles** / **Eatssaga** | Classic JRPG / HD-2D homage; great if the engine is geared toward party-based or tactical RPGs. |
| **Eatscraft** / **Eatsstage** | Highlights the "theatrical stage / tilt-shift diorama" nature of 2.5D sprites standing on a lit 3D stage. |

### B. Hybrid / Visual-First Names (Referencing HD-2D, Light & Tilt-Shift)
| Name | Rationale & Vibe |
| :--- | :--- |
| **Eatslumina** / **LuminaNative** | A direct nod to the *Lumina* HD-2D inspiration, brought into the native C++/WebGPU ecosystem. |
| **Diorama2D** / **Eatsdiorama** | Captures the exact visual hook: 2D pixel art living inside a miniature tilt-shift 3D diorama. |
| **Prism2D** / **Eatsprism** | Evokes light refraction, shadow casting, and multi-layered depth (2D sprites + 3D world). |

---

## 2. Executive Summary & Core Philosophy

### Why This Sibling Engine Exists
In [`C:\git\eatsworlds`](file:///c:/git/eatsworlds), we built a working **C++20 + Google Dawn (WebGPU) + `miniaudio` + Eatscript** voxel diorama engine inspired by *Town to City*. While the greedy-meshed voxel architecture is ultra-fast and gorgeous for static architecture and terrain, **representing NPCs, animals, monsters, and expressive RPG animations purely out of voxels introduces massive technical debt** (requiring micro-voxel grids or complex multi-part skeletal voxel rigging).

Meanwhile, the proven **HD-2D (*Octopath Traveler*)** approach—exemplified in the open-source web project **Lumina** (`https://gitlab.com/stubborn-hug/lumina/`)—solves this effortlessly by pairing:
1. **3D Multi-Height Tile Terrain & Low-Poly Architectural Props** wrapped in **procedurally generated pixel-art textures & normal maps**.
2. **2.5D Lit, Shadow-Casting Pixel-Art Billboards (`Sprite3D`)** for players, NPCs, animals, monsters, and swaying flora.
3. **Cinematic 3D Lighting & Post-Processing** (2048² directional sun shadows, dynamic point lights for lanterns/torches/spells, Tilt-Shift Bokeh Depth of Field, Bloom, and God Rays).

### Our Core Stack Advantage ("Native First, Web & Mobile Ready")
Lumina proves that a **100% procedural HD-2D engine** (zero external textures, models, or audio files) can look breathtaking. However, because Lumina is built on **Three.js (WebGL2) + JavaScript**, it has **no native path** and suffers severe performance bottlenecks on larger levels.

Our engine uses the battle-tested **Eatsbits / Eatsworlds stack**:
* **Core Language:** Modern **C++20** (zero GC pauses, SIMD-friendly procedural generation).
* **Graphics API:** **Google Dawn (WebGPU)** in C++ for native Desktop/Mobile, with a direct 1:1 compilation path to **Emscripten / WebAssembly + Browser WebGPU** for web targets.
* **Audio Engine:** **`miniaudio`** with custom procedural synthesis and foley built on top.
* **Scripting:** **Eatscript** (our 1:1 Python-syntax core scripting interface) driving procedural world generation, quests, buildings, and entity behaviors.

---

## 3. Benchmarks: Why Native C++/WebGPU Crushes Web-Only HD-2D

From analyzing Lumina’s architecture (`docs/architecture/OVERVIEW.md` and `docs/architecture/PERFORMANCE.md` at `https://gitlab.com/stubborn-hug/lumina/`), here are the exact bottlenecks in Lumina's Three.js implementation and how our C++/Dawn architecture solves them:

| Subsystem | Lumina (Three.js / WebGL2 / JS) | Our Native C++20 / Dawn WebGPU HD-2D Engine |
| :--- | :--- | :--- |
| **Scene Triangle Count** | **0.73M – 1.21M tris** on a 128×128 tile map (*Starfall Vale*, 874 props, 68 NPCs) | **< 80K tris** (instanced tile skirts + merged chunk hull buffers + 2-triangle instanced billboards) |
| **Draw Calls / Frame** | **150 – 283 draw calls** across **57–63 shader programs** | **4 – 8 draw calls total** across **3–4 unified WGSL pipelines** |
| **Cold Startup / Procgen Time** | **2.5 – 4.4 seconds freeze** (HTML5 `<canvas>` 2D pixel loops + WebGL shader compile) | **< 30 milliseconds** (C++20 CPU/WGSL compute atlas generation into a single `Texture2DArray`) |
| **Memory & GC Churn** | **~161 KB/frame JS GC allocation** (45% from Three.js uniform uploads) + 151 separate WebGL textures | **0 bytes/frame GC churn**; single `wgpu::Buffer` uniform upload + 1 `Texture2DArray` material atlas |
| **Frame Time Budget** | 5.3–8.7 ms GPU + 3.0–5.1 ms JS CPU on Desktop (struggles on mobile) | **< 1.0 ms GPU + < 0.3 ms CPU** (locked 60–120 FPS on Desktop, Web/WASM, and Mobile) |

---

## 4. Reusable Foundation from `C:\git\eatsworlds`

> [!TIP]
> **Do not reinvent the wheel!** The following modules in [`C:\git\eatsworlds`](file:///c:/git/eatsworlds) are already built, verified, and compiling cleanly with CMake + Dawn + `miniaudio`. Copy and adapt them directly when bootstrapping the new repository:

1. **CMake & Build Infrastructure:**
   * [`C:\git\eatsworlds\CMakeLists.txt`](file:///c:/git/eatsworlds/CMakeLists.txt) and [`C:\git\eatsworlds\build.ps1`](file:///c:/git/eatsworlds/build.ps1) — Pre-configured C++20 setup linking Dawn (`webgpu_dawn`), GLFW/Win32 windowing, and `miniaudio`.
2. **Workspace Testing Rule (MUST COPY):**
   * [`C:\git\eatsworlds\.agents\rules\testing.md`](file:///c:/git/eatsworlds/.agents/rules/testing.md) — **Critical User Preference:** *Do NOT run build or test commands automatically after making code edits. Only compile or run tests when explicitly requested by the user.*
3. **WebGPU Shadow Map & Tilt-Shift Post-Processing Pipeline:**
   * [`C:\git\eatsworlds\include\eatsworlds\diorama_renderer.hpp`](file:///c:/git/eatsworlds/include/eatsworlds/diorama_renderer.hpp) & [`C:\git\eatsworlds\src\render\diorama_renderer.cpp`](file:///c:/git/eatsworlds/src/render/diorama_renderer.cpp)
   * Already implements:
     * **Pass 1:** `2048×2048` `Depth32Float` Directional Sun Shadow Pass with hardware PCF (`textureSampleCompare`).
     * **Pass 2:** Golden-Hour Hemisphere + Sun + Point Light PBR-lite shading into an `RGBA16Float` HDR offscreen target.
     * **Pass 3:** **24-Tap Golden-Angle Spiral Tilt-Shift Bokeh Depth of Field** + ACES Filmic Tonemapping + Vignette + headless PNG snapshot export (`--snapshot`).
4. **Procedural Audio Foley (`miniaudio`):**
   * [`C:\git\eatsworlds\include\eatsworlds\audio_foley.hpp`](file:///c:/git/eatsworlds/include/eatsworlds/audio_foley.hpp) & [`C:\git\eatsworlds\src\audio\audio_foley.cpp`](file:///c:/git/eatsworlds/src/audio/audio_foley.cpp) — Real-time procedural foley & ambient synthesizer.
5. **Eatscript Interpreter Core:**
   * [`C:\git\eatsworlds\include\eatsworlds\script_bindings.hpp`](file:///c:/git/eatsworlds/include/eatsworlds/script_bindings.hpp) & [`C:\git\eatsworlds\src\voxel\script_bindings.cpp`](file:///c:/git/eatsworlds/src/voxel/script_bindings.cpp) — Clean line-by-line Pythonic parser with variables, arithmetic expressions, and built-in RNG ready to bind to HD-2D tile, prop, and entity APIs.

---

## 5. Target Architecture for the New HD-2D Engine

```mermaid
flowchart LR
    subgraph Eatscript["Eatscript (.eats Scene / RPG Script)"]
        S1["terrain_plateau() / stairs() / river()"]
        S2["prop_house() / bridge() / lantern()"]
        S3["spawn_npc() / spawn_monster() / foliage()"]
    end

    subgraph ProcGen["C++20 Procedural Asset Core (< 20ms Startup)"]
        P1["PixelCanvas C++\n(Procedural Albedo + Normal + Emissive)"]
        P2["SpriteGenerator C++\n(Procedural NPC, Monster & Tree Sheets)"]
        P3["Single WebGPU Texture2DArray Atlas\n(Nearest-Neighbor + Pixel AA)"]
        P1 --> P3
        P2 --> P3
    end

    subgraph World["HD-2D World Geometry & Entities"]
        W1["TileMap Mesh Builder\n(Multi-Height Cliffs, Slopes, Water)"]
        W2["PropFactory Mesh Builder\n(Gabled Roofs, Timber Houses, Bridges)"]
        W3["Sprite3D Instance Buffer\n(Lit & Shadow-Casting 2.5D Billboards)"]
    end

    subgraph WebGPU["Dawn WebGPU Render Pipeline (4-6 Draw Calls)"]
        R1["Pass 1: 2048² Shadow Map\n(Opaque World + Alpha-Tested 2.5D Sprites)"]
        R2["Pass 2: HD-2D Lit Pass (RGBA16Float)\n(Normal-Mapped Pixel Art + Clustered Point Lights)"]
        R3["Pass 3: Tilt-Shift Bokeh DoF + Bloom + ACES"]
        R1 --> R2 --> R3
    end

    Eatscript --> World
    ProcGen --> WebGPU
    World --> WebGPU
```

### Pillar 1: `PixelCanvas` in Native C++20 (Procedural Pixel Art + Normal Maps)
Inspired by Lumina's `PixelCanvas.js` and `Textures.js`, but running at native C++ speed:
* A lightweight C++20 `PixelCanvas` class (`std::vector<uint32_t>` RGBA8) with deterministic seeded noise (Perlin/Worley/FBM), palette quantization, dithering, brick/cobble/shingle pattern generators, and automatic **Sobel height-to-normal-map generation**.
* Generates all world material tiles (`32×32` or `64×64`: cobblestone, cliff rock, grass tiers, wood planks, plaster, terracotta shingles, glowing windows) and packs them into a single `wgpu::Texture` (`2DArray`, layers 0..N for Albedo+Roughness, layers N..2N for Normal+Emissive).
* **Crisp Pixel Art Sampling in WGSL:** Uses hardware bilinear filtering with a sub-pixel UV derivative curve (`fwidth(uv)`) so pixel art stays razor-sharp at any camera angle without shimmering when panning.

### Pillar 2: Multi-Height 3D Tile Terrain & Water (`TileMap`)
* A 2D grid of tiles where each tile stores `(elevation, terrain_type, cliff_type, water_flag, slope_dir)`.
* Automatically generates **3D stepped cliff walls**, **diagonal/cardinal stairs & ramps**, **shoreline foam edges**, and **translucent animated water planes** with procedural caustics and sky/terrain reflections.
* Merged per-chunk into compact vertex/index buffers so the entire terrain draws in **1 draw call**.

### Pillar 3: Procedural 3D Architectural Props (`PropFactory`)
* Unlike pure voxels, buildings can use **true angled/gabled roofs, overhangs, timber beams, archways, windmills, and suspension bridges** built from clean low-poly procedural primitives (`box`, `prism_roof`, `cylinder`, `arch`) UV-mapped to the procedural pixel-art material atlas.
* Emissive window panes automatically register warm point lights in the scene’s `LightPool` at dusk/night.

### Pillar 4: 2.5D Lit & Shadow-Casting Billboards (`Sprite3D`)
The signature *Octopath Traveler* look:
* **Entity Sprites:** Players, villagers, animals, and monsters are procedurally synthesized into multi-frame pixel-art sprite sheets (idle, walk, attack) with an accompanying bevel/normal map.
* **3D Grounded Billboards:** Rendered via a single instanced WebGPU storage buffer (`16–32 bytes per sprite instance`). Each billboard locks to the Y-axis (or camera tilt plane) so it stands upright on the 3D terrain.
* **Alpha-Tested 3D Shadows:** In **Pass 1 (Shadow Pass)**, the sprite shader samples the sprite alpha (`if (alpha < 0.5) discard;`), casting crisp pixel-art silhouettes across 3D stairs, walls, and terrain.
* **Normal-Mapped 3D Lighting:** In **Pass 2 (Lit Pass)**, each sprite pixel evaluates directional sunlight + nearby point lights (torches, lanterns, spells) + optional rim-lighting so characters feel physically embedded in the 3D diorama.

### Pillar 5: Dynamic Point-Light Pool + HD-2D Post-Processing
* **Uniform/Storage Point Light Array:** Up to 64 dynamic point lights (warm tavern windows, street lanterns, campfires, magic projectiles) evaluated per-pixel in WGSL with zero shader recompilation.
* **Tilt-Shift Bokeh DoF + Bloom:** Reuses the 24-tap Golden-Angle Bokeh filter from `Eatsworlds`, enhanced with a soft HDR bloom threshold so lanterns, windows, and water highlights glow against the miniature blurred foreground/background.

---

## 6. Starter Prompt for the New Discussion

When you open your new Antigravity chat, you can paste the following prompt directly:

```markdown
We are starting a new Native-First 2.5D (HD-2D) RPG & Diorama Engine inspired by *Octopath Traveler* and the open-source *Lumina* project (`https://gitlab.com/stubborn-hug/lumina/`), built on our C++20 + Google Dawn (WebGPU) + `miniaudio` + Eatscript stack.

Please read the full architectural specification and handoff document at @[c:\Users\root\Desktop\eats-hd2d-engine-spec.md] (also in @[c:\git\eatsworlds\docs\hd2d_engine_handoff.md]).

Also inspect our existing working voxel engine at `C:\git\eatsworlds` (specifically `CMakeLists.txt`, `src/render/diorama_renderer.cpp`, `src/audio/audio_foley.cpp`, and `.agents/rules/testing.md`) so we can reuse the Dawn WebGPU setup, 2048² shadow pass, Tilt-Shift Bokeh DoF pipeline, `miniaudio` foley, and Eatscript core.

Let's finalize the project name/folder (`C:\git\<name>`) and scaffold Phase 1: the C++20 Procedural PixelCanvas & Normal Atlas, Multi-Height 3D Tile Terrain + Gabled Village Props, and 2.5D Shadow-Casting Pixel-Art Billboards (`Sprite3D`).
```
