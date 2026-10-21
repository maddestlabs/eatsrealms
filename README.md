# Eatsrealms

Eatsrealms is a native 2.5D (HD-2D) RPG and diorama engine inspired by Octopath Traveler. It pairs 3D multi-height terrain and procedural low-poly architecture with 2.5D normal-mapped pixel-art billboards, dynamic point lights, directional shadows, and cinematic tilt-shift post-processing.

See it live: [Eatsrealms](https://maddestlabs.github.io/eatsrealms/)

## Tech Stack

- Core Language: Modern C++20 (MSVC, Clang, GCC) with OpenMP acceleration for procedural generation
- Graphics API: WebGPU via Google Dawn (wgpu-native)
- Shading Language: WGSL (WebGPU Shading Language)
- Render Pipeline:
  - Pass 1: 2048x2048 directional sun shadow map with hardware PCF and alpha-tested billboard cutout
  - Pass 2: HDR lit pass (RGBA16Float) with normal-mapped pixel art, PBR-lite lighting, and dynamic point light pool
  - Pass 3: Post-processing with 24-tap golden-angle spiral tilt-shift bokeh depth of field, bloom, vignette, and ACES filmic tonemapping
- Windowing and Input: Bundled GLFW 3.4
- Procedural Generation: Custom C++20 PixelCanvas engine generating a 24-layer Texture2DArray atlas with deterministic noise, palette quantization, and Sobel height-to-normal calculation
- Audio Engine: miniaudio library for real-time procedural foley synthesis, footstep/click audio, and ambient sound
- Scripting Language: Eatscript, an embedded Python-syntax scripting language and bytecode VM supporting scene recipes and live hot-reloading
- Image Output: Headless WebGPU offscreen readback and minimal PNG encoder
- Build System: CMake 3.20+ and PowerShell (build.ps1)
- Testing: CTest and automated unit test suite (eatsrealms_tests)

## Architecture Overview

1. Procedural PixelCanvas: Synthesizes material tiles and character sprites directly into GPU texture arrays in milliseconds with zero external image files.
2. World Geometry: Multi-height stepped terrain, ramps, water planes, and procedural low-poly props (gabled roofs, timber frames, stone bridges) merged into compact vertex buffers.
3. 2.5D Sprite3D Billboards: Camera-aligned, shadow-casting, normal-mapped sprite instances rendered via instanced storage buffers.
4. Lighting and Post-Processing: Directional sunlight, shadow mapping, dynamic point lights, and real-time tilt-shift miniature depth of field.
5. Eatscript Integration: Declarative scene generation recipes (.eats files) defining terrain, architecture, cameras, and entities.
6. Narrative History Simulator: "The Archaeology of Intent" world generation simulating historical eras, characters, foundation, prosperity, calamity, and ruin diorama synthesis.
7. Eatsrealms Open Architecture: Massive continuous open world extending dioramas across kilometers:
   - **Hierarchical Raw-Block LOD (Block Mipmapping):** Downsampled elevation pyramids (LOD 0: 1.5m diorama, LOD 1: 3.0m aggregated blocks, LOD 2: 6.0m macro blocks, LOD 3: 12.0m horizon ridges).
   - **2D Greedy Meshing:** Compresses coplanar block tops and cardinal cliff wall ribbons, collapsing 512+ raw block triangles into ~18 merged triangles per chunk.
   - **Distance-Based Flora Culling & Shadow Exclusion:** Automatically strips micro-flora beyond 70m (`kFloraRadius`), and restricts shadow cascade casting to LOD 0 geometry.
   - **Sliding Clipmap Streaming Ring:** Procedural chunks evaluated on-demand from deterministic Eatscript seeds centered on camera target.
   - **Atmospheric Height Fog & Ridge Silhouettes:** Vertical exponential height fog pooling in valleys and procedural sawtooth pine canopy strips on distant mountain ridges.

## Building and Running

### Prerequisites

- Windows 10/11, Linux, or macOS
- C++20 compatible compiler (Visual Studio 2022 MSVC, Clang 14+, or GCC 12+)
- CMake 3.20+
- PowerShell (on Windows)

### Build Script (Windows)

Use the automated build script:

```powershell
# Build and launch interactive diorama viewer
./build.ps1

# Build and run unit tests
./build.ps1 -Test

# Render headless preview snapshot
./build.ps1 -RenderPreview -NoLaunch

# Clean and rebuild in Release
./build.ps1 -Clean -Config Release
```

### Web Build (WebAssembly and WebGPU)

Compile to WebAssembly and launch the local HTTP development server:

```powershell
# Build and serve WebGPU diorama with live COOP/COEP headers
./build-web.ps1

# Specify custom port
./build-web.ps1 -Port 9000

# Compile only without starting dev server
./build-web.ps1 -NoServe
```

### Manual CMake Build

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

### CLI Options

The main binary supports both interactive and headless execution:

- `--script <path>`: Path to the Eatscript scene recipe (default: `scenes/starfall_vale.eats`)
- `--render-png <path>`: Render a frame directly to a PNG file and exit
- `--width <pixels>`: Viewport or snapshot width (default: 1280)
- `--height <pixels>`: Viewport or snapshot height (default: 720)

## Diorama Navigation Controls

- **1 / 2 / 3 / 4 / 5 / 6**: Switch Scene (`1`: Starfall Vale, `2`: Whispering Woods, `3`: Azure Cove, `4`: Cursed Hamlet, `5`: Highland Bridge, `6`: Eatsrealms Open)
- **[ / ]**: Cycle Previous / Next Scene Recipe
- **Mouse Move**: Free Look (Pitch and Yaw)
- **W / S / A / D**: Move and Strafe
- **E / Space | Q / C**: Ascend / Descend
- **Left Shift / Ctrl**: Sprint (2.5x) / Precision (0.25x)
- **Mouse Wheel**: Adjust Flight Speed
- **Left Click**: Raycast Terrain and Procedural Foley
- **TAB / ESC**: Toggle Cursor Lock
- **F**: Toggle Dynamic Tilt-Shift Auto-Focus
- **R**: Reset Camera to HD-2D Default
