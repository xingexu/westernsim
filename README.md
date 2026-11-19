# Western Sim

**A playable, pixel-art recreation of Western University, built from scratch in C++17 and OpenGL 3.3.**

Start below UC Hill, walk past University College and Middlesex College, or follow the road across the Thames. Press **M** to open the campus map. Use the material bar to build directly into the world, then save your changes to a local file.

![The third-person avatar at the foot of UC Hill, looking up toward University College and the Middlesex College clock tower](docs/western-campus.webp)

## Build and run

Use a C++17 compiler, CMake 3.20+, and an OpenGL 3.3-capable graphics driver. On Debian and Ubuntu, install `build-essential cmake libglfw3-dev`. On macOS, install Xcode Command Line Tools and CMake. On Windows, use the Visual Studio **Desktop development with C++** workload and CMake. CMake uses a compatible system GLFW/GLM when available and fetches pinned versions otherwise. GLAD is generated and included in the source.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
./build/westernsim
```

With a multi-config generator such as Visual Studio, run `build/Release/westernsim.exe` instead. On Linux the GLFW source fallback uses X11 and needs the `xorg-dev` package.

The world starts fresh at launch. **F5** writes `world.qlw` to your working directory and **F9** loads it. Start with `--save FILE` to use a different file. Changes include the full voxel layout, and loading returns the avatar to UC Hill.

| Control | Action |
| --- | --- |
| **WASD** / mouse | Walk / orbit the third-person camera |
| **Shift** / **Space** | Sprint / jump or swim upward |
| **Left click** / **Right click** | Remove / place the targeted block |
| **1–8** or scroll | Select building material |
| **E** | Toggle building |
| **M** / **Tab** | Campus map / field guide |
| **F5** / **F9** | Save / load |
| **F1** / **F2** | Diagnostics / switch meshing algorithms |
| **R** / **Esc** | Return to UC Hill / release or recapture the cursor |

## Campus

The `192 × 64 × 192` campus contains 20 named landmarks. Look for the stained-glass bays at University College, the copper spire and clock face at Middlesex College, the stepped massing and window bands at Weldon, and the pavilion and seating at Concrete Beach. The University Drive Bridge crosses the Thames on a raised stone deck.

The in-game map samples the top block of each column in the original voxel campus, giving it one map pixel per column. It follows the player's current position and keeps the original campus as a reference while you build.

<img src="docs/campus-map.webp" width="820" alt="Pixel-art campus map with 20 numbered landmarks, the Thames River, and the player's position">

The campus is hand-authored at play scale. The building footprints and routes follow [Western's campus map](https://www.uwo.ca/biology/obd2019/program__schedule/campus_map.html), with architectural details drawn from the [Western University walking tour](https://international.uwo.ca/pdf/2024/WI_Walking%20Tour%20and%20Map_2024%20web.pdf).

## Engine

| System | Implementation |
| --- | --- |
| Geometry | Contiguous `16³` chunks. The reference mesher emits the outer faces, including seam-aware occlusion. Greedy meshing merges coplanar rectangles with matching material and orientation. |
| Rendering | Indexed OpenGL meshes, camera-frustum culling, directional shadows, and animated water. The scene uses nearest-neighbor upscaling, ordered dithering, and materials sampled on a coarse grid. |
| Avatar | Articulated boxes, camera-relative locomotion and a third-person orbit that retracts against the world. |
| Physics | Fixed 120 Hz gravity and voxel AABB collision, with one-block step-up and swimming. |
| Building | Voxel DDA picking, collision-aware placement, remeshing of edited chunks and affected seam neighbors, and versioned binary snapshots. |

The CTest suite covers triangle counts, material splits, chunk seams, face winding, collisions, picking, save round trips, invalid saves, and the campus landmarks and bridge.

## Benchmark

Run `./build/westernsim --benchmark` in a desktop session (or under Xvfb on Linux). The same fixed camera sweep runs in both modes at **1280 × 720 output** and **426 × 240 internal scene resolution**, with VSync off and 30 warm-up plus 120 measured frames per mode. The timing includes the shadow pass, camera pass, nearest-neighbor upscaling, and HUD. Whole-world triangle counts are recorded before camera culling. Switch modes with **F2** during play or press **F1** to inspect live submission counts.

| Meshing mode | World triangles | Mean camera-pass triangles | FPS |
| --- | ---: | ---: | ---: |
| Exposed faces | 361,504 | 202,975.5 | 16.8 |
| Greedy | 62,364 | 32,995.3 | 45.2 |

Greedy meshing reduced the world mesh by **82.7%**. These figures are from a Release build on Ubuntu with Mesa llvmpipe CPU rendering and an OpenGL 3.3 core context. See the [captured benchmark output](docs/benchmark.txt) for the renderer, resolution, and full run statistics.

**Resume bullet**

> Built a C++17/OpenGL 3.3 voxel engine with a playable, editable Western University campus, fixed-step collision, and chunk frustum culling. Reduced world mesh triangles by 82.7% (362K to 62K) with greedy meshing and raised measured rendering throughput from 16.8 to 45.2 FPS at 720p output on Mesa llvmpipe.

## Credits

Western Sim uses [GLFW](https://www.glfw.org/), [GLAD](https://github.com/Dav1dde/glad), [GLM](https://github.com/g-truc/glm), and [Pixelify Sans](https://github.com/eifetx/Pixelify-Sans). The bundled GLAD and font notices are included in the source.
