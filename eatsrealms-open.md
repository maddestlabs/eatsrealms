# Eatsrealms Open: Massive Open World Architecture

This document outlines the architectural strategy for extending **Eatsrealms** from isolated diorama parcels into a **continuous, massive open world** while preserving the signature **HD-2D aesthetic** (tilt-shift miniature depth-of-field, pixel-art normal-mapped materials, dynamic point lights, and stepped voxel-diorama terrain).

---

## 1. Vision & Core Philosophy

Traditional HD-2D titles (such as *Octopath Traveler* and *Triangle Strategy*) rely on small, isolated diorama ribbons enclosed by static backdrop matte cards or skybox paintings. 

**Eatsrealms Open** differentiates itself by bringing that same handcrafted diorama aesthetic into a **fully explorable, seamless open world spanning kilometers**:
* Every distant mountain, plateau, river valley, and forest ridge is **real 3D geometry** that can be traversed.
* Distant terrain preserves the **blocky, stepped voxel-cliff aesthetic** of the diorama format rather than smoothing out into generic low-poly terrain.
* **Optical Masking via Tilt-Shift Bokeh:** Because the camera's tilt-shift lens ([`computeCoC()`](file:///c:/git/eatsrealms/src/render/hd2d_renderer.cpp#L878-L897) in [`fs_post()`](file:///c:/git/eatsrealms/src/render/hd2d_renderer.cpp#L970-L1042)) naturally defocusses distant scenery, distant terrain does not need high-frequency tile micro-geometry, individual grass blades, or micro-props.
* Level-of-Detail (LOD) transitions happen entirely **inside the blur circle of confusion**, eliminating the visible "LOD popping" that plagues traditional open-world games.

---

## 2. Hierarchical Raw-Block LOD (Block Mipmapping)

Instead of a uniform 1.5m grid across the entire world, the terrain is partitioned into concentric rings of **aggregated raw blocks**:

```
[ Center: Player / Camera Focus ]
       │
       ├── Ring 0: LOD 0 (0m – 70m) ── Full Micro-Detail Diorama
       │   • 1.5m TileCells with corner elevations, slopes, stairs, water planes
       │   • Dense 2.5D Flora (SpriteGrassTuft, SpriteWildflower, clover)
       │   • 3D Architectural Props (fences, lanterns, houses, bridges)
       │   • Casts & receives dynamic directional shadows + point lights
       │
       ├── Ring 1: LOD 1 (70m – 200m) ── Simplified Blocks & Landmarks
       │   • 3.0m aggregated blocks (2x2 downsampled elevation)
       │   • Micro-flora completely stripped (saves ~80% sprite instances)
       │   • Macro landmarks retained (large pine trees, towers, ruins)
       │   • Excluded from shadow caster pass (saves shadow map fill-rate)
       │
       ├── Ring 2: LOD 2 (200m – 600m) ── Greedy-Meshed Macro Blocks
       │   • 6.0m aggregated blocks (4x4 downsampled elevation)
       │   • Raw stepped cliff faces and plateau tops
       │   • Contiguous coplanar surfaces merged via Greedy Meshing
       │   • No individual props or sprites
       │
       └── Ring 3: LOD 3 (600m – 2000m+) ── Horizon Ridges & Mountain Silhouettes
           • 12.0m – 24.0m macro steps
           • Shaded with directional sun, sky ambient, and atmospheric Rayleigh haze
           • Softened into painterly shapes by the hexagonal bokeh post-process
```

---

## 3. Key Performance Pillars

### 3.1. Greedy Meshing for Raw Blocks
In the current implementation of [`RealmWorld::rebuildWorldGeometry()`](file:///c:/git/eatsrealms/src/world/realm_world.cpp#L1509-L1570), each [`TileCell`](file:///c:/git/eatsrealms/include/eatsrealms/realm_world.hpp#L29-L56) generates separate top and side quads.

In distant LOD 1–3 chunks:
* Adjacent raw blocks sharing the same elevation and material layer are merged into **single large quads** using repeating texture coordinates (`uvScale = 0.5f`).
* A flat valley or mountain plateau of $16 \times 16 = 256$ tiles that would normally emit 512 triangles merges into **just 2 triangles**.
* Stepped cliff walls merge horizontally and vertically into uniform quad ribbons.

### 3.2. Flora & Sprite Stripping
In diorama scenes, meadow vegetation accounts for the majority of scene instances (e.g. ~9,000 [`Sprite3DInstance`](file:///c:/git/eatsrealms/include/eatsrealms/hd2d_types.hpp#L133-L141) entities).
* Beyond $70\text{m}$, all micro-flora (grass blades, wildflowers, clovers, pebbles) is culled.
* For distant forested ridges (as seen in the reference artwork), rather than rendering thousands of individual tree sprites, chunks place **low-poly sawtooth canopy strips** along ridge edges facing the camera, or single low-poly billboard clusters.

### 3.3. Shadow Pass Exclusion
The shadow map pass ([`shadowWorldPipeline`](file:///c:/git/eatsrealms/src/render/hd2d_renderer.cpp#L1235)) in [`HD2DRenderer`](file:///c:/git/eatsrealms/src/render/hd2d_renderer.cpp#L1910-L1920) is restricted exclusively to **LOD 0** geometry and entities within the primary camera frustum.
* Distant raw blocks (LOD 1–3) do not cast into the $2048 \times 2048$ shadow cascade.
* In the main scene pass, distant blocks are shaded cleanly with directional sun lighting, ambient skylight, and distance fog.

### 3.4. Optical Bokeh Masking
Because [`fs_post()`](file:///c:/git/eatsrealms/src/render/hd2d_renderer.cpp#L970-L1042) applies a 38-tap hexagonal aperture blur kernel with dynamic tilt-shift bands:
* The circle-of-confusion radius grows with distance (`depthDelta` in [`computeCoC()`](file:///c:/git/eatsrealms/src/render/hd2d_renderer.cpp#L878-L897)).
* Top-screen tilt-shift (`topTilt`) adds an additional blur band to the upper background.
* Coarse block transitions (e.g. transitioning from 1.5m to 3.0m or 6.0m blocks) are diffused into smooth, creamy shapes. The human eye cannot detect the mesh simplification.

---

## 4. Performance & Geometry Budget Projections

Comparison for a **$2\text{km} \times 2\text{km}$ view distance** (~$7\text{ km}^2$ visible world):

| Metric | Naive Uniform 1.5m Grid | Eatsrealms Open (Hierarchical LOD + Greedy Mesh) |
| :--- | :--- | :--- |
| **Active Tiles** | ~3,500,000 unmerged cells | ~45,000 merged quads |
| **Vegetation Sprites** | ~200,000+ instances | ~6,000 instances (LOD 0 only) |
| **Scene Triangles** | ~12,000,000 (Unviable) | **~90,000 – 120,000 triangles** |
| **GPU Storage Buffer Size** | > 750 MB VRAM | **~12 – 16 MB VRAM** |
| **WebGPU Draw Calls** | Hundreds (if unbatched) | **6 Draw Calls** (Storage buffer pulling preserved) |
| **GPU Frame Time** | > 30 ms | **< 2.2 ms** (Solid 120+ FPS) |

---

## 5. Streaming Architecture

### 5.1. Chunk Representation (`RealmChunk`)
The current monolithic $64 \times 64$ [`RealmWorld::tiles_`](file:///c:/git/eatsrealms/include/eatsrealms/realm_world.hpp#L217) array is replaced by a chunked system:
* **Chunk Size:** $32 \times 32$ tiles ($48\text{m} \times 48\text{m}$ per chunk).
* **Chunk ID:** `ChunkCoord { int cx, int cz }` indexed via spatial hash map (`std::unordered_map`).
* **Chunk Data:**
  - `TileCell tiles[32 * 32]` (Full elevation and material data).
  - `int16_t lod1Elevation[16 * 16]` ($2\times 2$ downsampled min/max).
  - `int16_t lod2Elevation[8 * 8]` ($4\times 4$ downsampled).
  - Pre-generated or procedurally evaluated on-demand from deterministic Eatscript seeds.

### 5.2. Clipmap Streaming Ring
The engine maintains a sliding clipmap grid centered around the camera target:
* **LOD 0 Grid:** $3 \times 3$ chunks ($96\text{m} \times 96\text{m}$ to $144\text{m} \times 144\text{m}$) rendered with full micro-detail, flora, and shadows.
* **LOD 1 Ring:** Chunks between $70\text{m}$ and $200\text{m}$.
* **LOD 2 Ring:** Chunks between $200\text{m}$ and $600\text{m}$.
* **LOD 3 Ring:** Chunks between $600\text{m}$ and $2000\text{m}$.

As the party travels, chunks swap their active LOD mesh buffer. Geometry uploading continues to use single contiguous WebGPU storage buffers ([`worldVerts`](file:///c:/git/eatsrealms/src/render/hd2d_renderer.cpp#L168) and [`sprites`](file:///c:/git/eatsrealms/src/render/hd2d_renderer.cpp#L169)).

---

## 6. Implementation Roadmap

1. **Phase 1: Chunk Data Structure & Greedy Mesher**
   - Introduce `RealmChunk` ($32 \times 32$ tiles).
   - Implement downsampled elevation pyramids ($2\times 2$ and $4\times 4$).
   - Implement 2D greedy meshing for raw block tops and cliff walls.

2. **Phase 2: Distance-Based Flora & Shadow Filtering**
   - Filter [`Sprite3DInstance`](file:///c:/git/eatsrealms/include/eatsrealms/hd2d_types.hpp#L133-L141) generation to within `kFloraRadius = 70.0f`.
   - Update shadow pass upload to include only LOD 0 chunks.

3. **Phase 3: Clipmap Streaming Ring**
   - Center chunk streaming around camera focus point.
   - Stream procedural chunks generated by `WorldGenerator` and Eatscript recipes.

4. **Phase 4: Atmospheric Depth & Ridge Enhancements**
   - Extend [`AtmosphereConfig`](file:///c:/git/eatsrealms/include/eatsrealms/hd2d_types.hpp#L163-L245) with vertical exponential height fog to pool mist in low-lying valleys.
   - Add procedural sawtooth canopy strips for distant ridge silhouettes.

5. **Phase 5: Diverse 3D Foliage & GPU-Guarded Open World Flora**
   - **3D Tree Architecture:** Procedural chunks emit faceted 3D models with timber trunks, root buttresses, and multi-tier foliage skirts (Pine, Emerald Oak, Golden Autumn Oak, Snow Pine, Swamp Dead Tree, and Coastal Palm).
   - **Lumina-Style Meadow Scatter:** Procedural chunks scatter coherent clusters of multi-blade grass tufts, tall river grass, scarlet poppies, bluebells, yellow buttercups, four-leaf clovers, 3D moss boulders, and 3D star ferns.
   - **Zero-Allocation GPU Streaming:** `uploadRealmGeometryAndEntities` reuses storage buffers and bind groups with capacity tracking, eliminating GPU driver stalls during continuous chunk streaming.
   - **Distance-Culled Micro-Flora:** Micro-flora sprites are strictly culled beyond 70m (`dx*dx + dz*dz <= 4900.0f`), bounding active on-screen sprites to < 200 instances (< 0.05ms GPU raster time) regardless of world scale.
