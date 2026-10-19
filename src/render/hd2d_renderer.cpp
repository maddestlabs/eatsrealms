#include "eatsrealms/hd2d_renderer.hpp"

#include <webgpu/webgpu.h>
#if !defined(__EMSCRIPTEN__)
#include <webgpu/wgpu.h>
#else
#include <emscripten.h>
#include <emscripten/html5.h>
#ifndef WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT
#define WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector static_cast<WGPUSType>(0x00040000)
typedef struct WGPUEmscriptenSurfaceSourceCanvasHTMLSelector {
    WGPUChainedStruct chain;
    WGPUStringView selector;
} WGPUEmscriptenSurfaceSourceCanvasHTMLSelector;
#endif
extern "C" {
WGPUDevice emscripten_webgpu_get_device(void);
}
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#else
#include <GLFW/glfw3.h>
#endif

#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace eatsrealms {

namespace {

struct Mat4 {
    float m[16]{};

    static Mat4 identity() noexcept {
        Mat4 r{};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }

    static Mat4 multiply(const Mat4& a, const Mat4& b) noexcept {
        Mat4 r{};
        for (int c = 0; c < 4; ++c) {
            for (int row = 0; row < 4; ++row) {
                r.m[c * 4 + row] =
                    a.m[0 * 4 + row] * b.m[c * 4 + 0] +
                    a.m[1 * 4 + row] * b.m[c * 4 + 1] +
                    a.m[2 * 4 + row] * b.m[c * 4 + 2] +
                    a.m[3 * 4 + row] * b.m[c * 4 + 3];
            }
        }
        return r;
    }

    static Mat4 lookAt(Vec3f eye, Vec3f center, Vec3f up) noexcept {
        Vec3f f{center.x - eye.x, center.y - eye.y, center.z - eye.z};
        float fLen = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
        if (fLen > 1e-6f) { f.x /= fLen; f.y /= fLen; f.z /= fLen; }

        Vec3f s{
            up.y * f.z - up.z * f.y,
            up.z * f.x - up.x * f.z,
            up.x * f.y - up.y * f.x
        };
        float sLen = std::sqrt(s.x * s.x + s.y * s.y + s.z * s.z);
        if (sLen > 1e-6f) { s.x /= sLen; s.y /= sLen; s.z /= sLen; }

        Vec3f u{
            f.y * s.z - f.z * s.y,
            f.z * s.x - f.x * s.z,
            f.x * s.y - f.y * s.x
        };

        Mat4 res = identity();
        res.m[0] = s.x;  res.m[4] = s.y;  res.m[8]  = s.z;
        res.m[1] = u.x;  res.m[5] = u.y;  res.m[9]  = u.z;
        res.m[2] = -f.x; res.m[6] = -f.y; res.m[10] = -f.z;
        res.m[12] = -(s.x * eye.x + s.y * eye.y + s.z * eye.z);
        res.m[13] = -(u.x * eye.x + u.y * eye.y + u.z * eye.z);
        res.m[14] =  (f.x * eye.x + f.y * eye.y + f.z * eye.z);
        return res;
    }

    static Mat4 perspective(float fovYRadians, float aspect, float zNear, float zFar) noexcept {
        float f = 1.0f / std::tan(fovYRadians * 0.5f);
        Mat4 res{};
        res.m[0]  = f / aspect;
        res.m[5]  = f;
        res.m[10] = zFar / (zNear - zFar);
        res.m[11] = -1.0f;
        res.m[14] = (zFar * zNear) / (zNear - zFar);
        return res;
    }

    static Mat4 ortho(float left, float right, float bottom, float top, float zNear, float zFar) noexcept {
        Mat4 res = identity();
        res.m[0]  = 2.0f / (right - left);
        res.m[5]  = 2.0f / (top - bottom);
        res.m[10] = 1.0f / (zNear - zFar);
        res.m[12] = -(right + left) / (right - left);
        res.m[13] = -(top + bottom) / (top - bottom);
        res.m[14] = zNear / (zNear - zFar);
        return res;
    }
};

struct alignas(16) SceneUniforms {
    Mat4 viewProj;
    Mat4 lightViewProj;
    float camPos[4];     // xyz = camera position, w = timeSec
    float sunDir[4];     // xyz = normalized direction TO sun, w = fogDensity
    float sunColor[4];   // rgb = sun radiance, w = exposure
    float skyZenith[4];  // rgb = zenith color, w = focusDistance
    float skyHorizon[4]; // rgb = horizon color, w = focusRange
    float params[4];     // x = bokehRadius, y = width, z = height, w = fovRad
    float camBasisX[4];  // xyz = camera right, w = showReticle
    float camBasisY[4];  // xyz = camera up, w = pointLightCount
    float camBasisZ[4];  // xyz = camera forward, w = unused
};

// ============================================================================
// WGSL SHADER 0: SHADOW MAP PASS (Opaque 3D World + Alpha-Tested 2.5D Sprites)
// ============================================================================
static const char* kShadowWgslSource = R"(
struct SceneUniforms {
    viewProj      : mat4x4<f32>,
    lightViewProj : mat4x4<f32>,
    camPos        : vec4<f32>,
    sunDir        : vec4<f32>,
    sunColor      : vec4<f32>,
    skyZenith     : vec4<f32>,
    skyHorizon    : vec4<f32>,
    params        : vec4<f32>,
    camBasisX     : vec4<f32>,
    camBasisY     : vec4<f32>,
    camBasisZ     : vec4<f32>,
};

struct WorldVertex {
    posAndAo     : vec4<f32>,
    normAndRough : vec4<f32>,
    tangAndLayer : vec4<f32>,
    uvAndFlags   : vec4<f32>,
};

struct Sprite3DInstance {
    posAndWidth : vec4<f32>,
    sizeAndMeta : vec4<f32>,
};

@group(0) @binding(0) var<uniform> u : SceneUniforms;
@group(0) @binding(1) var<storage, read> worldVerts : array<WorldVertex>;
@group(0) @binding(2) var<storage, read> sprites : array<Sprite3DInstance>;
@group(0) @binding(3) var albedoAtlas : texture_2d_array<f32>;

@vertex
fn vs_shadow_world(@builtin(vertex_index) vertexIndex : u32) -> @builtin(position) vec4<f32> {
    let v = worldVerts[vertexIndex];
    return u.lightViewProj * vec4<f32>(v.posAndAo.xyz, 1.0);
}

struct SpriteShadowOut {
    @builtin(position) clipPos : vec4<f32>,
    @location(0) uv            : vec2<f32>,
    @location(1) @interpolate(flat) texLayer : u32,
};

@vertex
fn vs_shadow_sprite(@builtin(vertex_index) vertexIndex : u32) -> SpriteShadowOut {
    let spriteIdx = vertexIndex / 6u;
    let cornerIdx = vertexIndex % 6u;
    let sp = sprites[spriteIdx];

    let basePos = sp.posAndWidth.xyz;
    let width   = sp.posAndWidth.w;
    let height  = sp.sizeAndMeta.x;
    let layer   = bitcast<u32>(sp.sizeAndMeta.y);
    let flags   = bitcast<u32>(sp.sizeAndMeta.z);
    let phase   = sp.sizeAndMeta.w;

    var corners = array<vec2<f32>, 6>(
        vec2<f32>(-0.5, 0.0),
        vec2<f32>( 0.5, 0.0),
        vec2<f32>( 0.5, 1.0),
        vec2<f32>(-0.5, 0.0),
        vec2<f32>( 0.5, 1.0),
        vec2<f32>(-0.5, 1.0)
    );
    let c = corners[cornerIdx];

    var uCoord = c.x + 0.5;
    if ((flags & 1u) != 0u) {
        uCoord = 1.0 - uCoord;
    }
    let uv = vec2<f32>(uCoord, 1.0 - c.y);

    let time = u.camPos.w;
    var swayX = 0.0;
    var bobY  = 0.0;
    if ((flags & 2u) != 0u) {
        swayX = sin(time * 1.8 + phase) * 0.14 * c.y;
    }
    if ((flags & 4u) != 0u) {
        bobY = abs(sin(time * 2.6 + phase)) * 0.06 * c.y;
    }

    // Orient shadow-casting billboard along camera horizontal right vector so shadow matches screen silhouette
    let rightVec = normalize(vec3<f32>(u.camBasisX.x, 0.0, u.camBasisX.z));
    let worldPos = basePos + rightVec * (c.x * width + swayX) + vec3<f32>(0.0, c.y * height + bobY, 0.0);

    var out : SpriteShadowOut;
    out.clipPos  = u.lightViewProj * vec4<f32>(worldPos, 1.0);
    out.uv       = uv;
    out.texLayer = layer;
    return out;
}

@fragment
fn fs_shadow_sprite(in : SpriteShadowOut) {
    let texCoord = clamp(vec2<i32>(in.uv * 64.0), vec2<i32>(0, 0), vec2<i32>(63, 63));
    let sampleCol = textureLoad(albedoAtlas, texCoord, i32(in.texLayer), 0);
    if (sampleCol.a < 0.5) {
        discard;
    }
}
)";

// ============================================================================
// WGSL SHADER 1: HD-2D LIT SCENE PASS (Sky + 3D World + 2.5D Sprite3D)
// ============================================================================
static const char* kSceneWgslSource = R"(
struct SceneUniforms {
    viewProj      : mat4x4<f32>,
    lightViewProj : mat4x4<f32>,
    camPos        : vec4<f32>,
    sunDir        : vec4<f32>,
    sunColor      : vec4<f32>,
    skyZenith     : vec4<f32>,
    skyHorizon    : vec4<f32>,
    params        : vec4<f32>,
    camBasisX     : vec4<f32>,
    camBasisY     : vec4<f32>,
    camBasisZ     : vec4<f32>,
};

struct WorldVertex {
    posAndAo     : vec4<f32>,
    normAndRough : vec4<f32>,
    tangAndLayer : vec4<f32>,
    uvAndFlags   : vec4<f32>,
};

struct Sprite3DInstance {
    posAndWidth : vec4<f32>,
    sizeAndMeta : vec4<f32>,
};

struct PointLight {
    posAndRadius : vec4<f32>,
    colAndInt    : vec4<f32>,
};

@group(0) @binding(0) var<uniform> u : SceneUniforms;
@group(0) @binding(1) var<storage, read> worldVerts : array<WorldVertex>;
@group(0) @binding(2) var<storage, read> sprites : array<Sprite3DInstance>;
@group(0) @binding(3) var<storage, read> pointLights : array<PointLight>;
@group(0) @binding(4) var albedoAtlas : texture_2d_array<f32>;
@group(0) @binding(5) var normalAtlas : texture_2d_array<f32>;
@group(0) @binding(6) var atlasSampler : sampler;
@group(0) @binding(7) var shadowMap : texture_depth_2d;
@group(0) @binding(8) var shadowSampler : sampler_comparison;

// Crisp Sub-Pixel Anti-Aliased Pixel-Art Sampler
fn computeCrispUV(uv : vec2<f32>, fw : vec2<f32>) -> vec2<f32> {
    let texSize = 64.0;
    let pixel = uv * texSize;
    let safeFw = max(fw * texSize, vec2<f32>(0.001, 0.001));
    let seam = floor(pixel + 0.5);
    let frac = clamp((pixel - seam) / safeFw + 0.5, vec2<f32>(0.0), vec2<f32>(1.0));
    return (seam - 0.5 + frac) / texSize;
}

fn hash2D(p : vec2<f32>) -> f32 {
    let h = dot(p, vec2<f32>(127.1, 311.7));
    return fract(sin(h) * 43758.5453123);
}

fn valueNoise2D(p : vec2<f32>) -> f32 {
    let i = floor(p);
    let f = fract(p);
    let u2 = f * f * (3.0 - 2.0 * f);
    let a = hash2D(i);
    let b = hash2D(i + vec2<f32>(1.0, 0.0));
    let c = hash2D(i + vec2<f32>(0.0, 1.0));
    let d = hash2D(i + vec2<f32>(1.0, 1.0));
    return mix(mix(a, b, u2.x), mix(c, d, u2.x), u2.y);
}

fn fbmClouds(p : vec2<f32>) -> f32 {
    var v = 0.0;
    var amp = 0.5;
    var pos = p;
    for (var i = 0; i < 4; i = i + 1) {
        v += amp * valueNoise2D(pos);
        pos = pos * 2.03 + vec2<f32>(13.7, 7.3);
        amp *= 0.5;
    }
    return v;
}

fn evaluateSunsetSky(rayDir : vec3<f32>) -> vec3<f32> {
    let sunD = normalize(u.sunDir.xyz);
    let elev = clamp(rayDir.y, 0.0, 1.0);

    let horizonCol = vec3<f32>(0.80, 0.44, 0.36);
    let midSkyCol  = vec3<f32>(0.32, 0.19, 0.32);
    let zenithCol  = vec3<f32>(0.11, 0.08, 0.20);

    var sky = mix(horizonCol, midSkyCol, smoothstep(0.0, 0.28, elev));
    sky = mix(sky, zenithCol, smoothstep(0.24, 0.78, elev));

    let sunAlignment = max(dot(rayDir, sunD), 0.0);
    sky += u.sunColor.rgb * (0.22 * pow(sunAlignment, 3.0) + 0.32 * pow(sunAlignment, 16.0));

    if (rayDir.y > 0.01) {
        let cloudPlaneUV = rayDir.xz / (rayDir.y + 0.20) * 1.55 + vec2<f32>(u.camPos.w * 0.012, -0.8);
        let c1 = fbmClouds(cloudPlaneUV);
        let c2 = fbmClouds(cloudPlaneUV * 2.1 - vec2<f32>(0.7, 0.5));
        let cloudMask = smoothstep(0.42, 0.74, c1) * (1.0 - smoothstep(0.01, 0.0, rayDir.y));
        let cloudShadow = vec3<f32>(0.22, 0.13, 0.22);
        let cloudRim    = vec3<f32>(0.96, 0.58, 0.42);
        let cloudLit    = mix(cloudShadow, cloudRim, smoothstep(0.32, 0.68, c2) * 0.72 + 0.28 * sunAlignment);
        sky = mix(sky, cloudLit, cloudMask * 0.68);
    }
    return sky;
}

struct SkyOut {
    @builtin(position) pos : vec4<f32>,
    @location(0) ndc : vec2<f32>,
};

@vertex
fn vs_sky(@builtin(vertex_index) vertexIndex : u32) -> SkyOut {
    var tri = array<vec2<f32>, 3>(
        vec2<f32>(-1.0, -1.0),
        vec2<f32>( 3.0, -1.0),
        vec2<f32>(-1.0,  3.0)
    );
    var out : SkyOut;
    let p = tri[vertexIndex];
    out.pos = vec4<f32>(p.x, p.y, 0.9999, 1.0);
    out.ndc = p;
    return out;
}

@fragment
fn fs_sky(in : SkyOut) -> @location(0) vec4<f32> {
    let aspect = u.params.y / max(u.params.z, 1.0);
    let tanHalfFov = tan(u.params.w * 0.5);
    let rayDir = normalize(
        u.camBasisZ.xyz +
        u.camBasisX.xyz * (in.ndc.x * aspect * tanHalfFov) +
        u.camBasisY.xyz * (in.ndc.y * tanHalfFov)
    );
    let col = evaluateSunsetSky(rayDir);
    return vec4<f32>(col, 180.0);
}

fn sampleSunShadow(worldPos : vec3<f32>, normal : vec3<f32>) -> f32 {
    let biasedPos = worldPos + normal * 0.18;
    let lightClip = u.lightViewProj * vec4<f32>(biasedPos, 1.0);
    let ndc = lightClip.xyz / lightClip.w;
    let shadowUV = vec2<f32>(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
    let compZ = ndc.z - 0.0022;

    if (shadowUV.x <= 0.01 || shadowUV.x >= 0.99 || shadowUV.y <= 0.01 || shadowUV.y >= 0.99 || ndc.z <= 0.0 || ndc.z >= 1.0) {
        return 1.0;
    }

    let texel = 1.0 / 2048.0;
    var vis = 0.0;
    for (var dy = -1; dy <= 1; dy = dy + 1) {
        for (var dx = -1; dx <= 1; dx = dx + 1) {
            let offset = vec2<f32>(f32(dx), f32(dy)) * texel * 1.25;
            vis += textureSampleCompareLevel(shadowMap, shadowSampler, shadowUV + offset, compZ);
        }
    }
    return vis / 9.0;
}

fn evaluatePointLights(worldPos : vec3<f32>, N : vec3<f32>, wrapAmount : f32) -> vec3<f32> {
    let count = i32(u.camBasisY.w);
    var total = vec3<f32>(0.0);
    for (var i = 0; i < count; i = i + 1) {
        let pl = pointLights[i];
        let toL = pl.posAndRadius.xyz - worldPos;
        let distSq = dot(toL, toL);
        let rad = max(pl.posAndRadius.w, 1.0);
        if (distSq < rad * rad) {
            let dist = sqrt(distSq);
            let lDir = toL / max(dist, 0.001);
            let falloff = clamp(1.0 - (dist / rad), 0.0, 1.0);
            let atten = (falloff * falloff) / (1.0 + distSq * 0.08);
            let nDotL = max((dot(N, lDir) + wrapAmount) / (1.0 + wrapAmount), 0.0);
            total += pl.colAndInt.rgb * pl.colAndInt.w * nDotL * atten;
        }
    }
    return total;
}

// ----------------------------------------------------------------------------
// 3D World Terrain & Architectural Props Vertex/Fragment Shaders
// ----------------------------------------------------------------------------
struct WorldVSOut {
    @builtin(position) clipPos : vec4<f32>,
    @location(0) worldPos      : vec3<f32>,
    @location(1) normal        : vec3<f32>,
    @location(2) tangent       : vec3<f32>,
    @location(3) uv            : vec2<f32>,
    @location(4) aoAndRough    : vec2<f32>,
    @location(5) @interpolate(flat) texLayer : u32,
    @location(6) @interpolate(flat) flags    : u32,
};

@vertex
fn vs_world(@builtin(vertex_index) vertexIndex : u32) -> WorldVSOut {
    let v = worldVerts[vertexIndex];
    var out : WorldVSOut;
    out.clipPos    = u.viewProj * vec4<f32>(v.posAndAo.xyz, 1.0);
    out.worldPos   = v.posAndAo.xyz;
    out.normal     = v.normAndRough.xyz;
    out.tangent    = v.tangAndLayer.xyz;
    out.uv         = v.uvAndFlags.xy;
    out.aoAndRough = vec2<f32>(v.posAndAo.w, v.normAndRough.w);
    out.texLayer   = bitcast<u32>(v.tangAndLayer.w);
    out.flags      = bitcast<u32>(v.uvAndFlags.z);
    return out;
}

@fragment
fn fs_world(in : WorldVSOut) -> @location(0) vec4<f32> {
    var uv = in.uv;
    if ((in.flags & 1u) != 0u) {
        // Animated water surface ripple & scroll
        let t = u.camPos.w;
        uv += vec2<f32>(t * 0.14 + sin(in.worldPos.z * 1.4 + t * 2.2) * 0.05, cos(in.worldPos.x * 1.4 + t * 1.9) * 0.05);
    }

    let fw = fwidth(uv);
    let crispUV = computeCrispUV(uv, fw);
    let layer = i32(in.texLayer);

    let albSample  = textureSampleLevel(albedoAtlas, atlasSampler, crispUV, layer, 0.0);
    let normSample = textureSampleLevel(normalAtlas, atlasSampler, crispUV, layer, 0.0);

    // Decode linear sRGB albedo
    var albedo = pow(albSample.rgb, vec3<f32>(2.2));
    let emissive = normSample.a;

    // Construct tangent-space basis & perturb surface normal with Sobel pixel-art normal map
    let N0 = normalize(in.normal);
    let T0 = normalize(in.tangent);
    let B0 = cross(N0, T0);
    let tNorm = normalize(normSample.rgb * 2.0 - vec3<f32>(1.0));
    let N = normalize(T0 * tNorm.x + B0 * tNorm.y + N0 * tNorm.z);

    let V = normalize(u.camPos.xyz - in.worldPos);
    let L = normalize(u.sunDir.xyz);
    let H = normalize(V + L);

    let shadowVis = sampleSunShadow(in.worldPos, N0);
    let sunDiff   = max(dot(N, L), 0.0) * shadowVis;

    let roughness = in.aoAndRough.y;
    let specPower = mix(64.0, 10.0, roughness);
    let spec      = pow(max(dot(N, H), 0.0), specPower) * (1.0 - roughness * 0.72) * shadowVis;

    let aoFactor  = mix(0.18, 1.0, pow(in.aoAndRough.x, 1.2));
    let skyWeight = 0.5 * N.y + 0.5;
    let ambientSky  = mix(vec3<f32>(0.12, 0.08, 0.14), vec3<f32>(0.34, 0.24, 0.32), skyWeight);
    let bounceLight = vec3<f32>(0.55, 0.30, 0.18) * max(-N.y * 0.4 + 0.3, 0.0) * 0.18;

    let dynamicLights = evaluatePointLights(in.worldPos, N, 0.20);

    var color = albedo * ((ambientSky + bounceLight) * aoFactor + u.sunColor.rgb * 1.25 * sunDiff + dynamicLights * aoFactor)
              + u.sunColor.rgb * spec * 0.35
              + albedo * (emissive * 2.4);

    if ((in.flags & 1u) != 0u) {
        // Reflective sky sheen on river & millpond water
        let reflDir = reflect(-V, N);
        let skyRefl = evaluateSunsetSky(vec3<f32>(reflDir.x, abs(reflDir.y), reflDir.z));
        let fresnel = 0.18 + 0.55 * pow(1.0 - max(dot(N, V), 0.0), 3.0);
        color = mix(color, skyRefl + u.sunColor.rgb * spec * 1.2, fresnel * 0.55);
    }

    let viewDist = length(in.worldPos - u.camPos.xyz);
    let fogAmount = clamp(1.0 - exp(-max(viewDist - 36.0, 0.0) * u.sunDir.w), 0.0, 0.55);
    let fogColor  = evaluateSunsetSky(-V);
    color = mix(color, fogColor, fogAmount);

    return vec4<f32>(color, viewDist);
}

// ----------------------------------------------------------------------------
// 2.5D Shadow-Casting & Normal-Mapped Billboard Sprite Shaders (`Sprite3D`)
// ----------------------------------------------------------------------------
struct SpriteVSOut {
    @builtin(position) clipPos : vec4<f32>,
    @location(0) worldPos      : vec3<f32>,
    @location(1) uv            : vec2<f32>,
    @location(2) rightVec      : vec3<f32>,
    @location(3) @interpolate(flat) texLayer : u32,
    @location(4) @interpolate(flat) flags    : u32,
};

@vertex
fn vs_sprite(@builtin(vertex_index) vertexIndex : u32) -> SpriteVSOut {
    let spriteIdx = vertexIndex / 6u;
    let cornerIdx = vertexIndex % 6u;
    let sp = sprites[spriteIdx];

    let basePos = sp.posAndWidth.xyz;
    let width   = sp.posAndWidth.w;
    let height  = sp.sizeAndMeta.x;
    let layer   = bitcast<u32>(sp.sizeAndMeta.y);
    let flags   = bitcast<u32>(sp.sizeAndMeta.z);
    let phase   = sp.sizeAndMeta.w;

    var corners = array<vec2<f32>, 6>(
        vec2<f32>(-0.5, 0.0),
        vec2<f32>( 0.5, 0.0),
        vec2<f32>( 0.5, 1.0),
        vec2<f32>(-0.5, 0.0),
        vec2<f32>( 0.5, 1.0),
        vec2<f32>(-0.5, 1.0)
    );
    let c = corners[cornerIdx];

    var uCoord = c.x + 0.5;
    if ((flags & 1u) != 0u) {
        uCoord = 1.0 - uCoord;
    }
    let uv = vec2<f32>(uCoord, 1.0 - c.y);

    let time = u.camPos.w;
    var swayX = 0.0;
    var bobY  = 0.0;
    if ((flags & 2u) != 0u) {
        swayX = sin(time * 1.8 + phase) * 0.14 * c.y;
    }
    if ((flags & 4u) != 0u) {
        bobY = abs(sin(time * 2.6 + phase)) * 0.06 * c.y;
    }

    // Y-axis upright grounded billboard facing the camera
    let rightVec = normalize(vec3<f32>(u.camBasisX.x, 0.0, u.camBasisX.z));
    let worldPos = basePos + rightVec * (c.x * width + swayX) + vec3<f32>(0.0, c.y * height + bobY, 0.0);

    var out : SpriteVSOut;
    out.clipPos  = u.viewProj * vec4<f32>(worldPos, 1.0);
    out.worldPos = worldPos;
    out.uv       = uv;
    out.rightVec = rightVec;
    out.texLayer = layer;
    out.flags    = flags;
    return out;
}

@fragment
fn fs_sprite(in : SpriteVSOut) -> @location(0) vec4<f32> {
    let fw = fwidth(in.uv);
    let crispUV = computeCrispUV(in.uv, fw);
    let layer = i32(in.texLayer);

    // Exact nearest-texel alpha check for razor-sharp pixel-art silhouette edges
    let texCoord = clamp(vec2<i32>(in.uv * 64.0), vec2<i32>(0, 0), vec2<i32>(63, 63));
    let exactTexel = textureLoad(albedoAtlas, texCoord, layer, 0);
    if (exactTexel.a < 0.5) {
        discard;
    }

    let albSample  = textureSampleLevel(albedoAtlas, atlasSampler, crispUV, layer, 0.0);
    let normSample = textureSampleLevel(normalAtlas, atlasSampler, crispUV, layer, 0.0);

    let albedo   = pow(exactTexel.rgb, vec3<f32>(2.2));
    let emissive = normSample.a;

    // Construct billboard world-space normal from Sobel dome normal map
    let billForward = normalize(vec3<f32>(-u.camBasisZ.x, 0.25, -u.camBasisZ.z));
    let billUp      = vec3<f32>(0.0, 1.0, 0.0);
    let tNorm       = normalize(normSample.rgb * 2.0 - vec3<f32>(1.0));
    let flipSign    = select(1.0, -1.0, (in.flags & 1u) != 0u);
    let N           = normalize(in.rightVec * (tNorm.x * flipSign) + billUp * (-tNorm.y) + billForward * tNorm.z);

    let V = normalize(u.camPos.xyz - in.worldPos);
    let L = normalize(u.sunDir.xyz);

    let shadowVis = sampleSunShadow(in.worldPos, vec3<f32>(0.0, 1.0, 0.0));
    // Subsurface wrap lighting so 2.5D characters and foliage feel warmly integrated into the 3D scene
    let wrapDiff  = max((dot(N, L) + 0.45) / 1.45, 0.0) * shadowVis;

    // Golden-hour rim backlight on pixel-art sprite edges
    let edgeRim = pow(clamp(1.0 - abs(tNorm.z), 0.0, 1.0), 1.6) * shadowVis * 0.38;

    // Subtle grounding contact shadow at sprite feet (v near 1.0)
    let footAO = mix(0.68, 1.0, smoothstep(0.98, 0.72, in.uv.y));

    let ambientSky    = vec3<f32>(0.30, 0.22, 0.28);
    let dynamicLights = evaluatePointLights(in.worldPos, N, 0.55);

    var color = albedo * (ambientSky * footAO + u.sunColor.rgb * 1.18 * wrapDiff + dynamicLights)
              + u.sunColor.rgb * edgeRim
              + albedo * (emissive * 2.8);

    let viewDist = length(in.worldPos - u.camPos.xyz);
    let fogAmount = clamp(1.0 - exp(-max(viewDist - 36.0, 0.0) * u.sunDir.w), 0.0, 0.55);
    let fogColor  = evaluateSunsetSky(-V);
    color = mix(color, fogColor, fogAmount);

    return vec4<f32>(color, viewDist);
}
)";

// ============================================================================
// WGSL SHADER 2: TILT-SHIFT BOKEH DEPTH OF FIELD + HDR BLOOM + ACES FILMIC
// ============================================================================
static const char* kPostProcessWgslSource = R"(
struct SceneUniforms {
    viewProj      : mat4x4<f32>,
    lightViewProj : mat4x4<f32>,
    camPos        : vec4<f32>,
    sunDir        : vec4<f32>,
    sunColor      : vec4<f32>,
    skyZenith     : vec4<f32>,
    skyHorizon    : vec4<f32>,
    params        : vec4<f32>,
    camBasisX     : vec4<f32>,
    camBasisY     : vec4<f32>,
    camBasisZ     : vec4<f32>,
};

@group(0) @binding(0) var<uniform> u : SceneUniforms;
@group(0) @binding(1) var sceneTex : texture_2d<f32>;
@group(0) @binding(2) var sceneSampler : sampler;

struct PostVSOut {
    @builtin(position) pos : vec4<f32>,
    @location(0) uv : vec2<f32>,
};

@vertex
fn vs_post(@builtin(vertex_index) vertexIndex : u32) -> PostVSOut {
    var tri = array<vec2<f32>, 3>(
        vec2<f32>(-1.0, -1.0),
        vec2<f32>( 3.0, -1.0),
        vec2<f32>(-1.0,  3.0)
    );
    var out : PostVSOut;
    let p = tri[vertexIndex];
    out.pos = vec4<f32>(p, 0.0, 1.0);
    out.uv  = vec2<f32>((p.x + 1.0) * 0.5, (1.0 - p.y) * 0.5);
    return out;
}

fn computeCoC(dist : f32, uv : vec2<f32>) -> f32 {
    let focusDist  = u.skyZenith.w;
    let focusRange = max(u.skyHorizon.w, 1.0);

    let depthDelta = dist - focusDist;
    var depthCoC = 0.0;
    if (depthDelta < 0.0) {
        depthCoC = clamp((-depthDelta - focusRange * 0.35) / (focusRange * 0.95), 0.0, 1.0);
    } else {
        depthCoC = clamp((depthDelta - focusRange * 0.85) / (focusRange * 2.4), 0.0, 0.65);
    }

    // Signature Octopath / Tilt-Shift miniature lens band
    let bottomTilt = smoothstep(0.75, 0.99, uv.y) * 0.85;
    let topTilt    = smoothstep(0.16, 0.0, uv.y) * 0.32;

    return clamp(max(depthCoC, max(bottomTilt, topTilt)), 0.0, 1.0);
}

fn acesFilmic(x : vec3<f32>) -> vec3<f32> {
    let a = 2.51;
    let b = 0.03;
    let c = 2.43;
    let d = 0.59;
    let e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), vec3<f32>(0.0), vec3<f32>(1.0));
}

@fragment
fn fs_post(in : PostVSOut) -> @location(0) vec4<f32> {
    let centerSample = textureSampleLevel(sceneTex, sceneSampler, in.uv, 0.0);
    let centerDist   = centerSample.a;
    let centerCoC    = computeCoC(centerDist, in.uv);
    let maxRadiusPx  = u.params.x;

    let texelSize  = vec2<f32>(1.0 / max(u.params.y, 1.0), 1.0 / max(u.params.z, 1.0));
    let blurRadius = centerCoC * maxRadiusPx;

    var accumColor  = centerSample.rgb;
    var totalWeight = 1.0;
    var bloomAccum  = vec3<f32>(0.0);

    // 24-Tap Golden-Angle Spiral Bokeh Disk Blur + Soft HDR Lantern/Window Bloom
    let goldenAngle = 2.39996323;
    for (var i = 1; i <= 24; i = i + 1) {
        let fi = f32(i);
        let spiralR = sqrt(fi / 24.0);
        let theta   = fi * goldenAngle;
        let dir     = vec2<f32>(cos(theta), sin(theta));

        // Sample wide bloom halo even in sharp focus zones
        let bloomUV  = clamp(in.uv + dir * spiralR * 12.0 * texelSize, vec2<f32>(0.001), vec2<f32>(0.999));
        let bloomTap = textureSampleLevel(sceneTex, sceneSampler, bloomUV, 0.0).rgb;
        let bloomLuma = dot(bloomTap, vec3<f32>(0.299, 0.587, 0.114));
        if (bloomLuma > 1.05) {
            bloomAccum += (bloomTap - vec3<f32>(1.0)) * 0.045;
        }

        if (blurRadius > 0.35) {
            let r = spiralR * blurRadius;
            let sampleUV = clamp(in.uv + dir * r * texelSize, vec2<f32>(0.001), vec2<f32>(0.999));
            let tap = textureSampleLevel(sceneTex, sceneSampler, sampleUV, 0.0);
            let tapCoC = computeCoC(tap.a, sampleUV);

            let weight = select(tapCoC, 1.0, tap.a <= centerDist + 3.5);
            let luma = dot(tap.rgb, vec3<f32>(0.299, 0.587, 0.114));
            let bokehBoost = 1.0 + clamp(luma - 0.80, 0.0, 3.0) * 0.85;
            let w = max(weight, 0.15) * bokehBoost;

            accumColor += tap.rgb * w;
            totalWeight += w;
        }
    }

    var hdrColor = ((accumColor / totalWeight) + bloomAccum) * u.sunColor.w;

    // Characteristic HD-2D optical vignette
    let centeredUV = in.uv - vec2<f32>(0.5, 0.5);
    let vignette = 1.0 - dot(centeredUV, centeredUV) * 0.38;
    hdrColor *= vignette;

    var mapped = acesFilmic(hdrColor);
    mapped = pow(mapped, vec3<f32>(1.0 / 2.2));

    // Subtle miniature reticle dot when in Fly Mode
    if (u.camBasisX.w > 0.5) {
        let screenPx = (in.uv - vec2<f32>(0.5, 0.5)) * vec2<f32>(u.params.y, u.params.z);
        let dist = length(screenPx);
        if (dist < 2.2) {
            mapped = mix(mapped, vec3<f32>(1.0, 0.98, 0.92), 0.85);
        } else if (dist >= 2.2 && dist < 3.4) {
            mapped = mix(mapped, vec3<f32>(0.08, 0.06, 0.05), 0.55);
        }
    }

    return vec4<f32>(mapped, 1.0);
}
)";

WGPUShaderModule createShaderModule(WGPUDevice device, const char* wgslSource) {
    WGPUShaderSourceWGSL wgslDesc{};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = WGPUStringView{ wgslSource, std::strlen(wgslSource) };

    WGPUShaderModuleDescriptor smDesc{};
    smDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);
    return wgpuDeviceCreateShaderModule(device, &smDesc);
}

void fillSceneUniforms(const RealmWorld& world, uint32_t width, uint32_t height, float timeSec, SceneUniforms& outU, bool showReticle = false) {
    const auto& cam = world.camera();
    const auto& atm = world.atmosphere();

    float aspect = static_cast<float>(width) / static_cast<float>(std::max(1u, height));
    float fovRad = cam.fovDegrees * (3.1415926535f / 180.0f);

    Mat4 view = Mat4::lookAt(cam.position, cam.target, {0.0f, 1.0f, 0.0f});
    Mat4 proj = Mat4::perspective(fovRad, aspect, 1.0f, 260.0f);
    outU.viewProj = Mat4::multiply(proj, view);

    Vec3f sunDir = atm.sunDirection;
    float sLen = std::sqrt(sunDir.x * sunDir.x + sunDir.y * sunDir.y + sunDir.z * sunDir.z);
    if (sLen > 1e-6f) { sunDir.x /= sLen; sunDir.y /= sLen; sunDir.z /= sLen; }

    Vec3f sceneCenter{0.0f, 5.0f, -2.0f};
    Vec3f lightEye{
        sceneCenter.x + sunDir.x * 85.0f,
        sceneCenter.y + sunDir.y * 85.0f,
        sceneCenter.z + sunDir.z * 85.0f
    };
    Mat4 lightView = Mat4::lookAt(lightEye, sceneCenter, {0.0f, 1.0f, 0.0f});
    Mat4 lightProj = Mat4::ortho(-46.0f, 46.0f, -38.0f, 38.0f, 5.0f, 180.0f);
    outU.lightViewProj = Mat4::multiply(lightProj, lightView);

    outU.camPos[0] = cam.position.x;
    outU.camPos[1] = cam.position.y;
    outU.camPos[2] = cam.position.z;
    outU.camPos[3] = timeSec;

    outU.sunDir[0] = sunDir.x;
    outU.sunDir[1] = sunDir.y;
    outU.sunDir[2] = sunDir.z;
    outU.sunDir[3] = atm.fogDensity;

    outU.sunColor[0] = atm.sunColor.x;
    outU.sunColor[1] = atm.sunColor.y;
    outU.sunColor[2] = atm.sunColor.z;
    outU.sunColor[3] = atm.exposure;

    outU.skyZenith[0] = atm.skyZenith.x;
    outU.skyZenith[1] = atm.skyZenith.y;
    outU.skyZenith[2] = atm.skyZenith.z;
    outU.skyZenith[3] = cam.focusDistance;

    outU.skyHorizon[0] = atm.skyHorizon.x;
    outU.skyHorizon[1] = atm.skyHorizon.y;
    outU.skyHorizon[2] = atm.skyHorizon.z;
    outU.skyHorizon[3] = cam.focusRange;

    outU.params[0] = cam.bokehRadius;
    outU.params[1] = static_cast<float>(width);
    outU.params[2] = static_cast<float>(height);
    outU.params[3] = fovRad;

    Vec3f fwd{cam.target.x - cam.position.x, cam.target.y - cam.position.y, cam.target.z - cam.position.z};
    float fL = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
    if (fL > 1e-6f) { fwd.x /= fL; fwd.y /= fL; fwd.z /= fL; }
    Vec3f right{fwd.z, 0.0f, -fwd.x};
    float rL = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
    if (rL > 1e-6f) { right.x /= rL; right.y /= rL; right.z /= rL; }
    Vec3f upVec{
        fwd.y * right.z - fwd.z * right.y,
        fwd.z * right.x - fwd.x * right.z,
        fwd.x * right.y - fwd.y * right.x
    };

    outU.camBasisX[0] = right.x; outU.camBasisX[1] = right.y; outU.camBasisX[2] = right.z; outU.camBasisX[3] = showReticle ? 1.0f : 0.0f;
    outU.camBasisY[0] = upVec.x; outU.camBasisY[1] = upVec.y; outU.camBasisY[2] = upVec.z; outU.camBasisY[3] = static_cast<float>(world.pointLights().size());
    outU.camBasisZ[0] = fwd.x;   outU.camBasisZ[1] = fwd.y;   outU.camBasisZ[2] = fwd.z;   outU.camBasisZ[3] = 0.0f;
}

struct GpuHd2dContext {
    WGPUInstance instance{nullptr};
    WGPUSurface surface{nullptr};
    WGPUAdapter adapter{nullptr};
    WGPUDevice device{nullptr};
    WGPUQueue queue{nullptr};
    WGPUTextureFormat outputFormat{WGPUTextureFormat_RGBA8Unorm};

    WGPURenderPipeline shadowWorldPipeline{nullptr};
    WGPURenderPipeline shadowSpritePipeline{nullptr};
    WGPURenderPipeline skyPipeline{nullptr};
    WGPURenderPipeline worldPipeline{nullptr};
    WGPURenderPipeline spritePipeline{nullptr};
    WGPURenderPipeline postPipeline{nullptr};

    WGPUBindGroupLayout shadowBGL{nullptr};
    WGPUBindGroupLayout sceneBGL{nullptr};
    WGPUBindGroupLayout postBGL{nullptr};
    WGPUBindGroup shadowBindGroup{nullptr};
    WGPUBindGroup sceneBindGroup{nullptr};
    WGPUBindGroup postBindGroup{nullptr};

    WGPUBuffer uniformBuffer{nullptr};
    WGPUBuffer worldVertBuffer{nullptr};
    WGPUBuffer spriteBuffer{nullptr};
    WGPUBuffer pointLightBuffer{nullptr};

    // Procedural Texture2DArray Atlases (64x64x24 Albedo + 64x64x24 Normal/Emissive)
    WGPUTexture albedoAtlasTex{nullptr};
    WGPUTextureView albedoAtlasView{nullptr};
    WGPUTexture normalAtlasTex{nullptr};
    WGPUTextureView normalAtlasView{nullptr};
    WGPUSampler atlasSampler{nullptr};

    // 2048x2048 Shadow Map
    WGPUTexture shadowTexture{nullptr};
    WGPUTextureView shadowView{nullptr};
    WGPUSampler shadowSampler{nullptr};

    // Offscreen HDR + Depth targets
    uint32_t targetWidth{0};
    uint32_t targetHeight{0};
    WGPUTexture depthTexture{nullptr};
    WGPUTextureView depthView{nullptr};
    WGPUTexture hdrColorTexture{nullptr};
    WGPUTextureView hdrColorView{nullptr};
    WGPUSampler linearSampler{nullptr};

    uint32_t uploadedWorldVertexCount{0};
    uint32_t uploadedSpriteCount{0};

    bool init(GLFWwindow* optWindow, uint32_t width, uint32_t height, const ProceduralAtlasData& atlas) {
        targetWidth = width;
        targetHeight = height;

#if defined(__EMSCRIPTEN__)
        int hasDevice = EM_ASM_INT({
            return (typeof Module !== 'undefined' && Module['preinitializedWebGPUDevice']) ? 1 : 0;
        });
        if (hasDevice) {
            device = emscripten_webgpu_get_device();
        }

        WGPUInstanceDescriptor instDesc{};
        instance = wgpuCreateInstance(&instDesc);

        if (optWindow) {
            WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasDesc{};
            canvasDesc.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
            canvasDesc.selector = WGPUStringView{ "#canvas", WGPU_STRLEN };

            WGPUSurfaceDescriptor surfDesc{};
            surfDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&canvasDesc);
            surface = wgpuInstanceCreateSurface(instance, &surfDesc);
        }

        if (!device && instance) {
            struct ReqData {
                WGPUAdapter adapter{nullptr};
                WGPUDevice device{nullptr};
            } req;

            WGPURequestAdapterOptions aOpt{};
            aOpt.compatibleSurface = surface;
            aOpt.powerPreference = WGPUPowerPreference_HighPerformance;

            WGPURequestAdapterCallbackInfo aCb{};
            aCb.mode = WGPUCallbackMode_AllowProcessEvents;
            aCb.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adp, WGPUStringView, void* u, void*) {
                if (status == WGPURequestAdapterStatus_Success) static_cast<ReqData*>(u)->adapter = adp;
            };
            aCb.userdata1 = &req;
            wgpuInstanceRequestAdapter(instance, &aOpt, aCb);
            wgpuInstanceProcessEvents(instance);
            adapter = req.adapter;

            if (adapter) {
                WGPUDeviceDescriptor dDesc{};
                WGPURequestDeviceCallbackInfo dCb{};
                dCb.mode = WGPUCallbackMode_AllowProcessEvents;
                dCb.callback = [](WGPURequestDeviceStatus status, WGPUDevice dev, WGPUStringView, void* u, void*) {
                    if (status == WGPURequestDeviceStatus_Success) static_cast<ReqData*>(u)->device = dev;
                };
                dCb.userdata1 = &req;
                wgpuAdapterRequestDevice(adapter, &dDesc, dCb);
                wgpuInstanceProcessEvents(instance);
                device = req.device;
            }
        }

        if (!device) {
            device = emscripten_webgpu_get_device();
        }
        if (!device) {
            std::cerr << "[Hd2dRenderer] Failed to acquire WebGPU device in browser environment.\n";
            return false;
        }

        queue = wgpuDeviceGetQueue(device);

        if (surface) {
            outputFormat = WGPUTextureFormat_BGRA8Unorm;
            configureSurface(width, height);
        } else {
            outputFormat = WGPUTextureFormat_RGBA8Unorm;
        }

        return createPipelinesAndResources(width, height, atlas);
#else
        WGPUInstanceDescriptor instDesc{};
        instance = wgpuCreateInstance(&instDesc);
        if (!instance) return false;

        if (optWindow) {
#if defined(_WIN32)
            HWND hwnd = glfwGetWin32Window(optWindow);
            WGPUSurfaceSourceWindowsHWND winDesc{};
            winDesc.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
            winDesc.hinstance = GetModuleHandle(nullptr);
            winDesc.hwnd = hwnd;

            WGPUSurfaceDescriptor surfDesc{};
            surfDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&winDesc);
            surface = wgpuInstanceCreateSurface(instance, &surfDesc);
#endif
        }

        struct ReqData {
            WGPUAdapter adapter{nullptr};
            WGPUDevice device{nullptr};
        } req;

        WGPURequestAdapterOptions aOpt{};
        aOpt.compatibleSurface = surface;
        aOpt.powerPreference = WGPUPowerPreference_HighPerformance;

        WGPURequestAdapterCallbackInfo aCb{};
        aCb.mode = WGPUCallbackMode_AllowProcessEvents;
        aCb.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adp, WGPUStringView, void* u, void*) {
            if (status == WGPURequestAdapterStatus_Success) static_cast<ReqData*>(u)->adapter = adp;
        };
        aCb.userdata1 = &req;
        wgpuInstanceRequestAdapter(instance, &aOpt, aCb);
        wgpuInstanceProcessEvents(instance);
        adapter = req.adapter;
        if (!adapter) return false;

        WGPUDeviceDescriptor dDesc{};
        WGPURequestDeviceCallbackInfo dCb{};
        dCb.mode = WGPUCallbackMode_AllowProcessEvents;
        dCb.callback = [](WGPURequestDeviceStatus status, WGPUDevice dev, WGPUStringView, void* u, void*) {
            if (status == WGPURequestDeviceStatus_Success) static_cast<ReqData*>(u)->device = dev;
        };
        dCb.userdata1 = &req;
        wgpuAdapterRequestDevice(adapter, &dDesc, dCb);
        wgpuInstanceProcessEvents(instance);
        device = req.device;
        if (!device) return false;

        queue = wgpuDeviceGetQueue(device);

        if (surface) {
            WGPUSurfaceCapabilities caps{};
            wgpuSurfaceGetCapabilities(surface, adapter, &caps);
            outputFormat = (caps.formatCount > 0) ? caps.formats[0] : WGPUTextureFormat_BGRA8Unorm;
            configureSurface(width, height);
        } else {
            outputFormat = WGPUTextureFormat_RGBA8Unorm;
        }

        return createPipelinesAndResources(width, height, atlas);
#endif
    }

    void configureSurface(uint32_t width, uint32_t height) {
        if (!surface) return;
        WGPUSurfaceConfiguration config{};
        config.device = device;
        config.format = outputFormat;
        config.usage = WGPUTextureUsage_RenderAttachment;
        config.alphaMode = WGPUCompositeAlphaMode_Auto;
        config.width = width;
        config.height = height;
        config.presentMode = WGPUPresentMode_Fifo;
        wgpuSurfaceConfigure(surface, &config);
    }

    bool createPipelinesAndResources(uint32_t width, uint32_t height, const ProceduralAtlasData& atlas) {
        WGPUShaderModule shadowSM = createShaderModule(device, kShadowWgslSource);
        WGPUShaderModule sceneSM  = createShaderModule(device, kSceneWgslSource);
        WGPUShaderModule postSM   = createShaderModule(device, kPostProcessWgslSource);
        if (!shadowSM || !sceneSM || !postSM) return false;

        // 0. Shadow Bind Group Layout (4 bindings: uniforms, worldVerts, sprites, albedoAtlas)
        WGPUBindGroupLayoutEntry shEntries[4]{};
        shEntries[0].binding = 0;
        shEntries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        shEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
        shEntries[1].binding = 1;
        shEntries[1].visibility = WGPUShaderStage_Vertex;
        shEntries[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
        shEntries[2].binding = 2;
        shEntries[2].visibility = WGPUShaderStage_Vertex;
        shEntries[2].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
        shEntries[3].binding = 3;
        shEntries[3].visibility = WGPUShaderStage_Fragment;
        shEntries[3].texture.sampleType = WGPUTextureSampleType_Float;
        shEntries[3].texture.viewDimension = WGPUTextureViewDimension_2DArray;

        WGPUBindGroupLayoutDescriptor shBGLDesc{};
        shBGLDesc.entryCount = 4;
        shBGLDesc.entries = shEntries;
        shadowBGL = wgpuDeviceCreateBindGroupLayout(device, &shBGLDesc);

        WGPUPipelineLayoutDescriptor shPLDesc{};
        shPLDesc.bindGroupLayoutCount = 1;
        shPLDesc.bindGroupLayouts = &shadowBGL;
        WGPUPipelineLayout shadowPL = wgpuDeviceCreatePipelineLayout(device, &shPLDesc);

        // Shadow Pipelines (World opaque + Sprite alpha-tested)
        WGPUDepthStencilState shadowDepthState{};
        shadowDepthState.format = WGPUTextureFormat_Depth32Float;
        shadowDepthState.depthWriteEnabled = WGPUOptionalBool_True;
        shadowDepthState.depthCompare = WGPUCompareFunction_Less;

        WGPURenderPipelineDescriptor shWorldDesc{};
        shWorldDesc.layout = shadowPL;
        shWorldDesc.vertex.module = shadowSM;
        shWorldDesc.vertex.entryPoint = WGPUStringView{"vs_shadow_world", 15};
        shWorldDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        shWorldDesc.primitive.cullMode = WGPUCullMode_None;
        shWorldDesc.depthStencil = &shadowDepthState;
        shWorldDesc.multisample.count = 1;
        shWorldDesc.multisample.mask = ~0u;
        shadowWorldPipeline = wgpuDeviceCreateRenderPipeline(device, &shWorldDesc);

        WGPUFragmentState shSpriteFrag{};
        shSpriteFrag.module = shadowSM;
        shSpriteFrag.entryPoint = WGPUStringView{"fs_shadow_sprite", 16};
        shSpriteFrag.targetCount = 0;
        shSpriteFrag.targets = nullptr;

        WGPURenderPipelineDescriptor shSpriteDesc{};
        shSpriteDesc.layout = shadowPL;
        shSpriteDesc.vertex.module = shadowSM;
        shSpriteDesc.vertex.entryPoint = WGPUStringView{"vs_shadow_sprite", 16};
        shSpriteDesc.fragment = &shSpriteFrag;
        shSpriteDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        shSpriteDesc.primitive.cullMode = WGPUCullMode_None;
        shSpriteDesc.depthStencil = &shadowDepthState;
        shSpriteDesc.multisample.count = 1;
        shSpriteDesc.multisample.mask = ~0u;
        shadowSpritePipeline = wgpuDeviceCreateRenderPipeline(device, &shSpriteDesc);

        wgpuPipelineLayoutRelease(shadowPL);
        wgpuShaderModuleRelease(shadowSM);

        // 1. Main Scene Bind Group Layout (9 bindings)
        WGPUBindGroupLayoutEntry bglEntries[9]{};
        bglEntries[0].binding = 0;
        bglEntries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        bglEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
        bglEntries[1].binding = 1;
        bglEntries[1].visibility = WGPUShaderStage_Vertex;
        bglEntries[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
        bglEntries[2].binding = 2;
        bglEntries[2].visibility = WGPUShaderStage_Vertex;
        bglEntries[2].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
        bglEntries[3].binding = 3;
        bglEntries[3].visibility = WGPUShaderStage_Fragment;
        bglEntries[3].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
        bglEntries[4].binding = 4;
        bglEntries[4].visibility = WGPUShaderStage_Fragment;
        bglEntries[4].texture.sampleType = WGPUTextureSampleType_Float;
        bglEntries[4].texture.viewDimension = WGPUTextureViewDimension_2DArray;
        bglEntries[5].binding = 5;
        bglEntries[5].visibility = WGPUShaderStage_Fragment;
        bglEntries[5].texture.sampleType = WGPUTextureSampleType_Float;
        bglEntries[5].texture.viewDimension = WGPUTextureViewDimension_2DArray;
        bglEntries[6].binding = 6;
        bglEntries[6].visibility = WGPUShaderStage_Fragment;
        bglEntries[6].sampler.type = WGPUSamplerBindingType_Filtering;
        bglEntries[7].binding = 7;
        bglEntries[7].visibility = WGPUShaderStage_Fragment;
        bglEntries[7].texture.sampleType = WGPUTextureSampleType_Depth;
        bglEntries[7].texture.viewDimension = WGPUTextureViewDimension_2D;
        bglEntries[8].binding = 8;
        bglEntries[8].visibility = WGPUShaderStage_Fragment;
        bglEntries[8].sampler.type = WGPUSamplerBindingType_Comparison;

        WGPUBindGroupLayoutDescriptor sceneBGLDesc{};
        sceneBGLDesc.entryCount = 9;
        sceneBGLDesc.entries = bglEntries;
        sceneBGL = wgpuDeviceCreateBindGroupLayout(device, &sceneBGLDesc);

        WGPUPipelineLayoutDescriptor scenePLDesc{};
        scenePLDesc.bindGroupLayoutCount = 1;
        scenePLDesc.bindGroupLayouts = &sceneBGL;
        WGPUPipelineLayout scenePL = wgpuDeviceCreatePipelineLayout(device, &scenePLDesc);

        WGPUColorTargetState hdrTarget{};
        hdrTarget.format = WGPUTextureFormat_RGBA16Float;
        hdrTarget.writeMask = WGPUColorWriteMask_All;

        // Sky Pipeline
        WGPUDepthStencilState skyDepthState{};
        skyDepthState.format = WGPUTextureFormat_Depth32Float;
        skyDepthState.depthWriteEnabled = WGPUOptionalBool_False;
        skyDepthState.depthCompare = WGPUCompareFunction_Always;

        WGPUFragmentState skyFrag{};
        skyFrag.module = sceneSM;
        skyFrag.entryPoint = WGPUStringView{"fs_sky", 6};
        skyFrag.targetCount = 1;
        skyFrag.targets = &hdrTarget;

        WGPURenderPipelineDescriptor skyPipeDesc{};
        skyPipeDesc.layout = scenePL;
        skyPipeDesc.vertex.module = sceneSM;
        skyPipeDesc.vertex.entryPoint = WGPUStringView{"vs_sky", 6};
        skyPipeDesc.fragment = &skyFrag;
        skyPipeDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        skyPipeDesc.depthStencil = &skyDepthState;
        skyPipeDesc.multisample.count = 1;
        skyPipeDesc.multisample.mask = ~0u;
        skyPipeline = wgpuDeviceCreateRenderPipeline(device, &skyPipeDesc);

        // World 3D Pipeline
        WGPUDepthStencilState sceneDepthState{};
        sceneDepthState.format = WGPUTextureFormat_Depth32Float;
        sceneDepthState.depthWriteEnabled = WGPUOptionalBool_True;
        sceneDepthState.depthCompare = WGPUCompareFunction_Less;

        WGPUFragmentState worldFrag{};
        worldFrag.module = sceneSM;
        worldFrag.entryPoint = WGPUStringView{"fs_world", 8};
        worldFrag.targetCount = 1;
        worldFrag.targets = &hdrTarget;

        WGPURenderPipelineDescriptor worldPipeDesc{};
        worldPipeDesc.layout = scenePL;
        worldPipeDesc.vertex.module = sceneSM;
        worldPipeDesc.vertex.entryPoint = WGPUStringView{"vs_world", 8};
        worldPipeDesc.fragment = &worldFrag;
        worldPipeDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        worldPipeDesc.primitive.cullMode = WGPUCullMode_None;
        worldPipeDesc.depthStencil = &sceneDepthState;
        worldPipeDesc.multisample.count = 1;
        worldPipeDesc.multisample.mask = ~0u;
        worldPipeline = wgpuDeviceCreateRenderPipeline(device, &worldPipeDesc);

        // Sprite3D Billboard Pipeline
        WGPUFragmentState spriteFrag{};
        spriteFrag.module = sceneSM;
        spriteFrag.entryPoint = WGPUStringView{"fs_sprite", 9};
        spriteFrag.targetCount = 1;
        spriteFrag.targets = &hdrTarget;

        WGPURenderPipelineDescriptor spritePipeDesc{};
        spritePipeDesc.layout = scenePL;
        spritePipeDesc.vertex.module = sceneSM;
        spritePipeDesc.vertex.entryPoint = WGPUStringView{"vs_sprite", 9};
        spritePipeDesc.fragment = &spriteFrag;
        spritePipeDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        spritePipeDesc.primitive.cullMode = WGPUCullMode_None;
        spritePipeDesc.depthStencil = &sceneDepthState;
        spritePipeDesc.multisample.count = 1;
        spritePipeDesc.multisample.mask = ~0u;
        spritePipeline = wgpuDeviceCreateRenderPipeline(device, &spritePipeDesc);

        wgpuPipelineLayoutRelease(scenePL);
        wgpuShaderModuleRelease(sceneSM);

        // 2. Post-Process Bind Group Layout & Pipeline
        WGPUBindGroupLayoutEntry postEntries[3]{};
        postEntries[0].binding = 0;
        postEntries[0].visibility = WGPUShaderStage_Fragment;
        postEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
        postEntries[1].binding = 1;
        postEntries[1].visibility = WGPUShaderStage_Fragment;
        postEntries[1].texture.sampleType = WGPUTextureSampleType_Float;
        postEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        postEntries[2].binding = 2;
        postEntries[2].visibility = WGPUShaderStage_Fragment;
        postEntries[2].sampler.type = WGPUSamplerBindingType_Filtering;

        WGPUBindGroupLayoutDescriptor postBGLDesc{};
        postBGLDesc.entryCount = 3;
        postBGLDesc.entries = postEntries;
        postBGL = wgpuDeviceCreateBindGroupLayout(device, &postBGLDesc);

        WGPUPipelineLayoutDescriptor postPLDesc{};
        postPLDesc.bindGroupLayoutCount = 1;
        postPLDesc.bindGroupLayouts = &postBGL;
        WGPUPipelineLayout postPL = wgpuDeviceCreatePipelineLayout(device, &postPLDesc);

        WGPUColorTargetState outTarget{};
        outTarget.format = outputFormat;
        outTarget.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState postFrag{};
        postFrag.module = postSM;
        postFrag.entryPoint = WGPUStringView{"fs_post", 7};
        postFrag.targetCount = 1;
        postFrag.targets = &outTarget;

        WGPURenderPipelineDescriptor postPipeDesc{};
        postPipeDesc.layout = postPL;
        postPipeDesc.vertex.module = postSM;
        postPipeDesc.vertex.entryPoint = WGPUStringView{"vs_post", 7};
        postPipeDesc.fragment = &postFrag;
        postPipeDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        postPipeDesc.multisample.count = 1;
        postPipeDesc.multisample.mask = ~0u;
        postPipeline = wgpuDeviceCreateRenderPipeline(device, &postPipeDesc);

        wgpuPipelineLayoutRelease(postPL);
        wgpuShaderModuleRelease(postSM);

        // 3. Samplers, Uniform Buffer & PointLight Buffer
        WGPUSamplerDescriptor atlasSampDesc{};
        atlasSampDesc.addressModeU = WGPUAddressMode_Repeat;
        atlasSampDesc.addressModeV = WGPUAddressMode_Repeat;
        atlasSampDesc.magFilter = WGPUFilterMode_Linear;
        atlasSampDesc.minFilter = WGPUFilterMode_Linear;
        atlasSampDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
        atlasSampDesc.maxAnisotropy = 1;
        atlasSampler = wgpuDeviceCreateSampler(device, &atlasSampDesc);

        WGPUSamplerDescriptor shSampDesc{};
        shSampDesc.addressModeU = WGPUAddressMode_ClampToEdge;
        shSampDesc.addressModeV = WGPUAddressMode_ClampToEdge;
        shSampDesc.magFilter = WGPUFilterMode_Linear;
        shSampDesc.minFilter = WGPUFilterMode_Linear;
        shSampDesc.compare = WGPUCompareFunction_LessEqual;
        shSampDesc.maxAnisotropy = 1;
        shadowSampler = wgpuDeviceCreateSampler(device, &shSampDesc);

        WGPUSamplerDescriptor linSampDesc{};
        linSampDesc.addressModeU = WGPUAddressMode_ClampToEdge;
        linSampDesc.addressModeV = WGPUAddressMode_ClampToEdge;
        linSampDesc.magFilter = WGPUFilterMode_Linear;
        linSampDesc.minFilter = WGPUFilterMode_Linear;
        linSampDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
        linSampDesc.maxAnisotropy = 1;
        linearSampler = wgpuDeviceCreateSampler(device, &linSampDesc);

        WGPUBufferDescriptor uDesc{};
        uDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        uDesc.size = sizeof(SceneUniforms);
        uniformBuffer = wgpuDeviceCreateBuffer(device, &uDesc);

        WGPUBufferDescriptor plDesc{};
        plDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
        plDesc.size = sizeof(PointLight) * RealmWorld::kMaxPointLights;
        pointLightBuffer = wgpuDeviceCreateBuffer(device, &plDesc);

        // 4. Create & Upload Single Texture2DArray Albedo + Normal Atlases (64x64x24)
        WGPUTextureDescriptor arrDesc{};
        arrDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        arrDesc.dimension = WGPUTextureDimension_2D;
        arrDesc.size = WGPUExtent3D{atlas.tileSize, atlas.tileSize, atlas.layerCount};
        arrDesc.format = WGPUTextureFormat_RGBA8Unorm;
        arrDesc.mipLevelCount = 1;
        arrDesc.sampleCount = 1;
        albedoAtlasTex = wgpuDeviceCreateTexture(device, &arrDesc);
        normalAtlasTex = wgpuDeviceCreateTexture(device, &arrDesc);

        WGPUTextureViewDescriptor arrViewDesc{};
        arrViewDesc.format = WGPUTextureFormat_RGBA8Unorm;
        arrViewDesc.dimension = WGPUTextureViewDimension_2DArray;
        arrViewDesc.baseMipLevel = 0;
        arrViewDesc.mipLevelCount = 1;
        arrViewDesc.baseArrayLayer = 0;
        arrViewDesc.arrayLayerCount = atlas.layerCount;
        arrViewDesc.aspect = WGPUTextureAspect_All;
        albedoAtlasView = wgpuTextureCreateView(albedoAtlasTex, &arrViewDesc);
        normalAtlasView = wgpuTextureCreateView(normalAtlasTex, &arrViewDesc);

        WGPUTexelCopyBufferLayout layout{};
        layout.offset = 0;
        layout.bytesPerRow = atlas.tileSize * 4u;
        layout.rowsPerImage = atlas.tileSize;
        WGPUExtent3D extent{atlas.tileSize, atlas.tileSize, atlas.layerCount};

        WGPUTexelCopyTextureInfo dstAlb{};
        dstAlb.texture = albedoAtlasTex;
        dstAlb.mipLevel = 0;
        dstAlb.origin = WGPUOrigin3D{0, 0, 0};
        dstAlb.aspect = WGPUTextureAspect_All;
        wgpuQueueWriteTexture(queue, &dstAlb, atlas.albedoLayers.data(), atlas.albedoLayers.size() * sizeof(uint32_t), &layout, &extent);

        WGPUTexelCopyTextureInfo dstNorm{};
        dstNorm.texture = normalAtlasTex;
        dstNorm.mipLevel = 0;
        dstNorm.origin = WGPUOrigin3D{0, 0, 0};
        dstNorm.aspect = WGPUTextureAspect_All;
        wgpuQueueWriteTexture(queue, &dstNorm, atlas.normalLayers.data(), atlas.normalLayers.size() * sizeof(uint32_t), &layout, &extent);

        // 5. Shadow Map Texture (2048x2048 Depth32Float)
        WGPUTextureDescriptor shTexDesc{};
        shTexDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
        shTexDesc.dimension = WGPUTextureDimension_2D;
        shTexDesc.size = WGPUExtent3D{2048, 2048, 1};
        shTexDesc.format = WGPUTextureFormat_Depth32Float;
        shTexDesc.mipLevelCount = 1;
        shTexDesc.sampleCount = 1;
        shadowTexture = wgpuDeviceCreateTexture(device, &shTexDesc);
        shadowView = wgpuTextureCreateView(shadowTexture, nullptr);

        reallocateViewportTargets(width, height);
        return shadowWorldPipeline && shadowSpritePipeline && skyPipeline && worldPipeline && spritePipeline && postPipeline;
    }

    void reallocateViewportTargets(uint32_t width, uint32_t height) {
        targetWidth = width;
        targetHeight = height;

        if (depthView) { wgpuTextureViewRelease(depthView); depthView = nullptr; }
        if (depthTexture) { wgpuTextureDestroy(depthTexture); wgpuTextureRelease(depthTexture); depthTexture = nullptr; }
        if (hdrColorView) { wgpuTextureViewRelease(hdrColorView); hdrColorView = nullptr; }
        if (hdrColorTexture) { wgpuTextureDestroy(hdrColorTexture); wgpuTextureRelease(hdrColorTexture); hdrColorTexture = nullptr; }

        WGPUTextureDescriptor dDesc{};
        dDesc.usage = WGPUTextureUsage_RenderAttachment;
        dDesc.dimension = WGPUTextureDimension_2D;
        dDesc.size = WGPUExtent3D{width, height, 1};
        dDesc.format = WGPUTextureFormat_Depth32Float;
        dDesc.mipLevelCount = 1;
        dDesc.sampleCount = 1;
        depthTexture = wgpuDeviceCreateTexture(device, &dDesc);
        depthView = wgpuTextureCreateView(depthTexture, nullptr);

        WGPUTextureDescriptor cDesc{};
        cDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
        cDesc.dimension = WGPUTextureDimension_2D;
        cDesc.size = WGPUExtent3D{width, height, 1};
        cDesc.format = WGPUTextureFormat_RGBA16Float;
        cDesc.mipLevelCount = 1;
        cDesc.sampleCount = 1;
        hdrColorTexture = wgpuDeviceCreateTexture(device, &cDesc);
        hdrColorView = wgpuTextureCreateView(hdrColorTexture, nullptr);

        rebuildPostBindGroup();
    }

    void rebuildPostBindGroup() {
        if (postBindGroup) { wgpuBindGroupRelease(postBindGroup); postBindGroup = nullptr; }
        WGPUBindGroupEntry entries[3]{};
        entries[0].binding = 0;
        entries[0].buffer = uniformBuffer;
        entries[0].size = sizeof(SceneUniforms);
        entries[1].binding = 1;
        entries[1].textureView = hdrColorView;
        entries[2].binding = 2;
        entries[2].sampler = linearSampler;

        WGPUBindGroupDescriptor bgDesc{};
        bgDesc.layout = postBGL;
        bgDesc.entryCount = 3;
        bgDesc.entries = entries;
        postBindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);
    }

    void uploadRealmGeometryAndEntities(RealmWorld& world) {
        world.rebuildWorldGeometry();
        const auto& verts = world.worldVertices();
        const auto& sprites = world.sprites();
        const auto& lights = world.pointLights();

        uploadedWorldVertexCount = static_cast<uint32_t>(verts.size());
        uploadedSpriteCount = static_cast<uint32_t>(sprites.size());

        if (!lights.empty()) {
            wgpuQueueWriteBuffer(queue, pointLightBuffer, 0, lights.data(), lights.size() * sizeof(PointLight));
        }

        if (worldVertBuffer) { wgpuBufferDestroy(worldVertBuffer); wgpuBufferRelease(worldVertBuffer); worldVertBuffer = nullptr; }
        if (spriteBuffer) { wgpuBufferDestroy(spriteBuffer); wgpuBufferRelease(spriteBuffer); spriteBuffer = nullptr; }

        size_t vertBytes = std::max<size_t>(sizeof(WorldVertex), verts.size() * sizeof(WorldVertex));
        WGPUBufferDescriptor vDesc{};
        vDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
        vDesc.size = vertBytes;
        worldVertBuffer = wgpuDeviceCreateBuffer(device, &vDesc);
        if (!verts.empty()) {
            wgpuQueueWriteBuffer(queue, worldVertBuffer, 0, verts.data(), verts.size() * sizeof(WorldVertex));
        }

        size_t spriteBytes = std::max<size_t>(sizeof(Sprite3DInstance), sprites.size() * sizeof(Sprite3DInstance));
        WGPUBufferDescriptor sDesc{};
        sDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
        sDesc.size = spriteBytes;
        spriteBuffer = wgpuDeviceCreateBuffer(device, &sDesc);
        if (!sprites.empty()) {
            wgpuQueueWriteBuffer(queue, spriteBuffer, 0, sprites.data(), sprites.size() * sizeof(Sprite3DInstance));
        }

        if (shadowBindGroup) { wgpuBindGroupRelease(shadowBindGroup); shadowBindGroup = nullptr; }
        WGPUBindGroupEntry shEntries[4]{};
        shEntries[0].binding = 0;
        shEntries[0].buffer = uniformBuffer;
        shEntries[0].size = sizeof(SceneUniforms);
        shEntries[1].binding = 1;
        shEntries[1].buffer = worldVertBuffer;
        shEntries[1].size = vertBytes;
        shEntries[2].binding = 2;
        shEntries[2].buffer = spriteBuffer;
        shEntries[2].size = spriteBytes;
        shEntries[3].binding = 3;
        shEntries[3].textureView = albedoAtlasView;

        WGPUBindGroupDescriptor shBgDesc{};
        shBgDesc.layout = shadowBGL;
        shBgDesc.entryCount = 4;
        shBgDesc.entries = shEntries;
        shadowBindGroup = wgpuDeviceCreateBindGroup(device, &shBgDesc);

        if (sceneBindGroup) { wgpuBindGroupRelease(sceneBindGroup); sceneBindGroup = nullptr; }
        WGPUBindGroupEntry scEntries[9]{};
        scEntries[0].binding = 0;
        scEntries[0].buffer = uniformBuffer;
        scEntries[0].size = sizeof(SceneUniforms);
        scEntries[1].binding = 1;
        scEntries[1].buffer = worldVertBuffer;
        scEntries[1].size = vertBytes;
        scEntries[2].binding = 2;
        scEntries[2].buffer = spriteBuffer;
        scEntries[2].size = spriteBytes;
        scEntries[3].binding = 3;
        scEntries[3].buffer = pointLightBuffer;
        scEntries[3].size = sizeof(PointLight) * RealmWorld::kMaxPointLights;
        scEntries[4].binding = 4;
        scEntries[4].textureView = albedoAtlasView;
        scEntries[5].binding = 5;
        scEntries[5].textureView = normalAtlasView;
        scEntries[6].binding = 6;
        scEntries[6].sampler = atlasSampler;
        scEntries[7].binding = 7;
        scEntries[7].textureView = shadowView;
        scEntries[8].binding = 8;
        scEntries[8].sampler = shadowSampler;

        WGPUBindGroupDescriptor scBgDesc{};
        scBgDesc.layout = sceneBGL;
        scBgDesc.entryCount = 9;
        scBgDesc.entries = scEntries;
        sceneBindGroup = wgpuDeviceCreateBindGroup(device, &scBgDesc);
    }

    void recordFramePasses(WGPUCommandEncoder encoder, WGPUTextureView finalColorTargetView) {
        // 1. Pass 1: 2048x2048 Directional Sun Shadow Map (World 3D + Alpha-Tested 2.5D Sprites)
        WGPURenderPassDepthStencilAttachment shDepthAtt{};
        shDepthAtt.view = shadowView;
        shDepthAtt.depthLoadOp = WGPULoadOp_Clear;
        shDepthAtt.depthStoreOp = WGPUStoreOp_Store;
        shDepthAtt.depthClearValue = 1.0f;

        WGPURenderPassDescriptor shPassDesc{};
        shPassDesc.colorAttachmentCount = 0;
        shPassDesc.depthStencilAttachment = &shDepthAtt;

        WGPURenderPassEncoder shPass = wgpuCommandEncoderBeginRenderPass(encoder, &shPassDesc);
        wgpuRenderPassEncoderSetBindGroup(shPass, 0, shadowBindGroup, 0, nullptr);
        if (uploadedWorldVertexCount > 0) {
            wgpuRenderPassEncoderSetPipeline(shPass, shadowWorldPipeline);
            wgpuRenderPassEncoderDraw(shPass, uploadedWorldVertexCount, 1u, 0u, 0u);
        }
        if (uploadedSpriteCount > 0) {
            wgpuRenderPassEncoderSetPipeline(shPass, shadowSpritePipeline);
            wgpuRenderPassEncoderDraw(shPass, uploadedSpriteCount * 6u, 1u, 0u, 0u);
        }
        wgpuRenderPassEncoderEnd(shPass);
        wgpuRenderPassEncoderRelease(shPass);

        // 2. Pass 2: HD-2D Lit Scene Pass (Sky + 3D Terrain/Props + 2.5D Sprite3D Billboards)
        WGPURenderPassColorAttachment sceneColAtt{};
        sceneColAtt.view = hdrColorView;
        sceneColAtt.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        sceneColAtt.loadOp = WGPULoadOp_Clear;
        sceneColAtt.storeOp = WGPUStoreOp_Store;
        sceneColAtt.clearValue = WGPUColor{0.22, 0.16, 0.28, 180.0};

        WGPURenderPassDepthStencilAttachment sceneDepthAtt{};
        sceneDepthAtt.view = depthView;
        sceneDepthAtt.depthLoadOp = WGPULoadOp_Clear;
        sceneDepthAtt.depthStoreOp = WGPUStoreOp_Store;
        sceneDepthAtt.depthClearValue = 1.0f;

        WGPURenderPassDescriptor scenePassDesc{};
        scenePassDesc.colorAttachmentCount = 1;
        scenePassDesc.colorAttachments = &sceneColAtt;
        scenePassDesc.depthStencilAttachment = &sceneDepthAtt;

        WGPURenderPassEncoder scenePass = wgpuCommandEncoderBeginRenderPass(encoder, &scenePassDesc);
        wgpuRenderPassEncoderSetBindGroup(scenePass, 0, sceneBindGroup, 0, nullptr);

        wgpuRenderPassEncoderSetPipeline(scenePass, skyPipeline);
        wgpuRenderPassEncoderDraw(scenePass, 3u, 1u, 0u, 0u);

        if (uploadedWorldVertexCount > 0) {
            wgpuRenderPassEncoderSetPipeline(scenePass, worldPipeline);
            wgpuRenderPassEncoderDraw(scenePass, uploadedWorldVertexCount, 1u, 0u, 0u);
        }
        if (uploadedSpriteCount > 0) {
            wgpuRenderPassEncoderSetPipeline(scenePass, spritePipeline);
            wgpuRenderPassEncoderDraw(scenePass, uploadedSpriteCount * 6u, 1u, 0u, 0u);
        }
        wgpuRenderPassEncoderEnd(scenePass);
        wgpuRenderPassEncoderRelease(scenePass);

        // 3. Pass 3: Tilt-Shift Bokeh Depth of Field + HDR Bloom + ACES Filmic Grade
        WGPURenderPassColorAttachment postColAtt{};
        postColAtt.view = finalColorTargetView;
        postColAtt.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        postColAtt.loadOp = WGPULoadOp_Clear;
        postColAtt.storeOp = WGPUStoreOp_Store;
        postColAtt.clearValue = WGPUColor{0.0, 0.0, 0.0, 1.0};

        WGPURenderPassDescriptor postPassDesc{};
        postPassDesc.colorAttachmentCount = 1;
        postPassDesc.colorAttachments = &postColAtt;

        WGPURenderPassEncoder postPass = wgpuCommandEncoderBeginRenderPass(encoder, &postPassDesc);
        wgpuRenderPassEncoderSetPipeline(postPass, postPipeline);
        wgpuRenderPassEncoderSetBindGroup(postPass, 0, postBindGroup, 0, nullptr);
        wgpuRenderPassEncoderDraw(postPass, 3u, 1u, 0u, 0u);
        wgpuRenderPassEncoderEnd(postPass);
        wgpuRenderPassEncoderRelease(postPass);
    }

    void shutdown() {
        if (shadowBindGroup) wgpuBindGroupRelease(shadowBindGroup);
        if (sceneBindGroup) wgpuBindGroupRelease(sceneBindGroup);
        if (postBindGroup) wgpuBindGroupRelease(postBindGroup);
        if (shadowBGL) wgpuBindGroupLayoutRelease(shadowBGL);
        if (sceneBGL) wgpuBindGroupLayoutRelease(sceneBGL);
        if (postBGL) wgpuBindGroupLayoutRelease(postBGL);
        if (shadowWorldPipeline) wgpuRenderPipelineRelease(shadowWorldPipeline);
        if (shadowSpritePipeline) wgpuRenderPipelineRelease(shadowSpritePipeline);
        if (skyPipeline) wgpuRenderPipelineRelease(skyPipeline);
        if (worldPipeline) wgpuRenderPipelineRelease(worldPipeline);
        if (spritePipeline) wgpuRenderPipelineRelease(spritePipeline);
        if (postPipeline) wgpuRenderPipelineRelease(postPipeline);
        if (uniformBuffer) { wgpuBufferDestroy(uniformBuffer); wgpuBufferRelease(uniformBuffer); }
        if (worldVertBuffer) { wgpuBufferDestroy(worldVertBuffer); wgpuBufferRelease(worldVertBuffer); }
        if (spriteBuffer) { wgpuBufferDestroy(spriteBuffer); wgpuBufferRelease(spriteBuffer); }
        if (pointLightBuffer) { wgpuBufferDestroy(pointLightBuffer); wgpuBufferRelease(pointLightBuffer); }
        if (albedoAtlasView) wgpuTextureViewRelease(albedoAtlasView);
        if (albedoAtlasTex) { wgpuTextureDestroy(albedoAtlasTex); wgpuTextureRelease(albedoAtlasTex); }
        if (normalAtlasView) wgpuTextureViewRelease(normalAtlasView);
        if (normalAtlasTex) { wgpuTextureDestroy(normalAtlasTex); wgpuTextureRelease(normalAtlasTex); }
        if (shadowView) wgpuTextureViewRelease(shadowView);
        if (shadowTexture) { wgpuTextureDestroy(shadowTexture); wgpuTextureRelease(shadowTexture); }
        if (depthView) wgpuTextureViewRelease(depthView);
        if (depthTexture) { wgpuTextureDestroy(depthTexture); wgpuTextureRelease(depthTexture); }
        if (hdrColorView) wgpuTextureViewRelease(hdrColorView);
        if (hdrColorTexture) { wgpuTextureDestroy(hdrColorTexture); wgpuTextureRelease(hdrColorTexture); }
        if (atlasSampler) wgpuSamplerRelease(atlasSampler);
        if (shadowSampler) wgpuSamplerRelease(shadowSampler);
        if (linearSampler) wgpuSamplerRelease(linearSampler);
        if (surface) wgpuSurfaceRelease(surface);
        if (queue) wgpuQueueRelease(queue);
        if (device) { wgpuDeviceDestroy(device); wgpuDeviceRelease(device); }
        if (adapter) wgpuAdapterRelease(adapter);
        if (instance) wgpuInstanceRelease(instance);
    }
};

uint32_t computeCrc32(const uint8_t* data, size_t len) noexcept {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1u) ^ (0xEDB88320u & (-(static_cast<int32_t>(crc & 1u))));
        }
    }
    return ~crc;
}

void appendBE32(std::vector<uint8_t>& buf, uint32_t val) {
    buf.push_back(static_cast<uint8_t>((val >> 24u) & 0xFFu));
    buf.push_back(static_cast<uint8_t>((val >> 16u) & 0xFFu));
    buf.push_back(static_cast<uint8_t>((val >> 8u) & 0xFFu));
    buf.push_back(static_cast<uint8_t>(val & 0xFFu));
}

void appendPngChunk(std::vector<uint8_t>& fileBytes, const char type[4], const uint8_t* data, size_t len) {
    appendBE32(fileBytes, static_cast<uint32_t>(len));
    size_t typeOffset = fileBytes.size();
    fileBytes.push_back(static_cast<uint8_t>(type[0]));
    fileBytes.push_back(static_cast<uint8_t>(type[1]));
    fileBytes.push_back(static_cast<uint8_t>(type[2]));
    fileBytes.push_back(static_cast<uint8_t>(type[3]));
    if (len > 0 && data) {
        fileBytes.insert(fileBytes.end(), data, data + len);
    }
    uint32_t crc = computeCrc32(fileBytes.data() + typeOffset, 4 + len);
    appendBE32(fileBytes, crc);
}

} // namespace

bool Hd2dRenderer::writePngFile(const std::string& filePath, uint32_t width, uint32_t height, const uint8_t* rgbaPixels) {
    if (!rgbaPixels || width == 0 || height == 0) return false;

    const size_t rowBytes = static_cast<size_t>(width) * 4 + 1;
    std::vector<uint8_t> rawData(rowBytes * height);
    for (uint32_t y = 0; y < height; ++y) {
        rawData[y * rowBytes] = 0;
        std::memcpy(&rawData[y * rowBytes + 1], &rgbaPixels[static_cast<size_t>(y) * width * 4], static_cast<size_t>(width) * 4);
    }

    std::vector<uint8_t> zlibStream;
    zlibStream.push_back(0x78);
    zlibStream.push_back(0x01);

    size_t offset = 0;
    size_t remaining = rawData.size();
    while (remaining > 0) {
        uint16_t blockLen = static_cast<uint16_t>(std::min<size_t>(remaining, 65535u));
        bool isFinal = (remaining - blockLen == 0);
        zlibStream.push_back(isFinal ? 0x01 : 0x00);
        zlibStream.push_back(static_cast<uint8_t>(blockLen & 0xFFu));
        zlibStream.push_back(static_cast<uint8_t>((blockLen >> 8u) & 0xFFu));
        uint16_t nlen = ~blockLen;
        zlibStream.push_back(static_cast<uint8_t>(nlen & 0xFFu));
        zlibStream.push_back(static_cast<uint8_t>((nlen >> 8u) & 0xFFu));
        zlibStream.insert(zlibStream.end(), rawData.data() + offset, rawData.data() + offset + blockLen);
        offset += blockLen;
        remaining -= blockLen;
    }

    uint32_t s1 = 1, s2 = 0;
    for (uint8_t b : rawData) {
        s1 = (s1 + b) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    appendBE32(zlibStream, (s2 << 16u) | s1);

    std::vector<uint8_t> pngBytes = {137, 80, 78, 71, 13, 10, 26, 10};
    std::vector<uint8_t> ihdr;
    appendBE32(ihdr, width);
    appendBE32(ihdr, height);
    ihdr.push_back(8);
    ihdr.push_back(6);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    appendPngChunk(pngBytes, "IHDR", ihdr.data(), ihdr.size());
    appendPngChunk(pngBytes, "IDAT", zlibStream.data(), zlibStream.size());
    appendPngChunk(pngBytes, "IEND", nullptr, 0);

    std::ofstream ofs(filePath, std::ios::binary);
    if (!ofs.is_open()) return false;
    ofs.write(reinterpret_cast<const char*>(pngBytes.data()), static_cast<std::streamsize>(pngBytes.size()));
    return ofs.good();
}

bool Hd2dRenderer::renderToPng(RealmWorld& world, uint32_t width, uint32_t height, const std::string& outputPngPath) {
    GpuHd2dContext ctx;
    if (!ctx.init(nullptr, width, height, world.atlas())) {
        std::cerr << "[Hd2dRenderer] Failed to initialize headless WebGPU device.\n";
        return false;
    }

    ctx.uploadRealmGeometryAndEntities(world);

    SceneUniforms u{};
    fillSceneUniforms(world, width, height, 0.5f, u, false);
    wgpuQueueWriteBuffer(ctx.queue, ctx.uniformBuffer, 0, &u, sizeof(SceneUniforms));

    const uint32_t unpaddedBytesPerRow = width * 4u;
    const uint32_t paddedBytesPerRow   = (unpaddedBytesPerRow + 255u) & ~255u;
    const size_t readbackSize          = static_cast<size_t>(paddedBytesPerRow) * height;

    WGPUTextureDescriptor outTexDesc{};
    outTexDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    outTexDesc.dimension = WGPUTextureDimension_2D;
    outTexDesc.size = WGPUExtent3D{width, height, 1};
    outTexDesc.format = WGPUTextureFormat_RGBA8Unorm;
    outTexDesc.mipLevelCount = 1;
    outTexDesc.sampleCount = 1;
    WGPUTexture outTex = wgpuDeviceCreateTexture(ctx.device, &outTexDesc);
    WGPUTextureView outView = wgpuTextureCreateView(outTex, nullptr);

    WGPUBufferDescriptor readDesc{};
    readDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    readDesc.size = readbackSize;
    WGPUBuffer readBuffer = wgpuDeviceCreateBuffer(ctx.device, &readDesc);

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.device, nullptr);
    ctx.recordFramePasses(encoder, outView);

    WGPUTexelCopyTextureInfo srcCopy{};
    srcCopy.texture = outTex;
    srcCopy.mipLevel = 0;
    srcCopy.origin = WGPUOrigin3D{0, 0, 0};
    srcCopy.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferInfo dstCopy{};
    dstCopy.buffer = readBuffer;
    dstCopy.layout.offset = 0;
    dstCopy.layout.bytesPerRow = paddedBytesPerRow;
    dstCopy.layout.rowsPerImage = height;

    WGPUExtent3D copyExtent{width, height, 1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &srcCopy, &dstCopy, &copyExtent);

    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(ctx.queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);

    bool mapComplete = false;
    bool mapSuccess  = false;
    struct MapWaitState {
        bool* done;
        bool* ok;
    } waitState{&mapComplete, &mapSuccess};

    WGPUBufferMapCallbackInfo mapCb{};
    mapCb.mode = WGPUCallbackMode_AllowProcessEvents;
    mapCb.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* u1, void*) {
        auto* st = static_cast<MapWaitState*>(u1);
        *st->ok = (status == WGPUMapAsyncStatus_Success);
        *st->done = true;
    };
    mapCb.userdata1 = &waitState;

    wgpuBufferMapAsync(readBuffer, WGPUMapMode_Read, 0, readbackSize, mapCb);
    while (!mapComplete) {
#if !defined(__EMSCRIPTEN__)
        wgpuDevicePoll(ctx.device, true, nullptr);
#endif
        wgpuInstanceProcessEvents(ctx.instance);
    }

    std::vector<uint8_t> rgbaPixels(static_cast<size_t>(width) * height * 4);
    if (mapSuccess) {
        const auto* mapped = static_cast<const uint8_t*>(wgpuBufferGetConstMappedRange(readBuffer, 0, readbackSize));
        for (uint32_t y = 0; y < height; ++y) {
            std::memcpy(&rgbaPixels[static_cast<size_t>(y) * width * 4],
                        mapped + static_cast<size_t>(y) * paddedBytesPerRow,
                        static_cast<size_t>(width) * 4);
        }
        wgpuBufferUnmap(readBuffer);
    }

    wgpuBufferDestroy(readBuffer);
    wgpuBufferRelease(readBuffer);
    wgpuTextureViewRelease(outView);
    wgpuTextureDestroy(outTex);
    wgpuTextureRelease(outTex);
    ctx.shutdown();

    if (!mapSuccess) return false;
    return writePngFile(outputPngPath, width, height, rgbaPixels.data());
}

struct FlyCamState {
    float yaw{0.0f};
    float pitch{0.0f};
    float defaultYaw{0.0f};
    float defaultPitch{0.0f};
    float baseSpeed{20.0f};
    bool cursorCaptured{true};
    bool autoFocus{true};
    double lastMouseX{0.0};
    double lastMouseY{0.0};
    bool firstMouse{true};
    CameraConfig defaultCamera{};
};

struct LoopContext {
    RealmWorld* world{nullptr};
    RealmScriptEngine* scriptEngine{nullptr};
    std::string watchedScriptPath;
    AudioFoleyEngine* foleyEngine{nullptr};
    GLFWwindow* window{nullptr};
    GpuHd2dContext* ctx{nullptr};
    FlyCamState* camState{nullptr};
    std::filesystem::file_time_type lastScriptWriteTime{};
    double lastFrameTime{0.0};
    bool wasTabDown{false};
    bool wasEscDown{false};
    bool wasFDown{false};
    bool wasRDown{false};
    bool wasLeftDown{false};
};

static void renderSingleFrame(LoopContext& l) {
    glfwPollEvents();

    double currentFrameTime = glfwGetTime();
    float dt = static_cast<float>(currentFrameTime - l.lastFrameTime);
    if (dt < 0.0001f) dt = 0.0001f;
    if (dt > 0.1f) dt = 0.1f;
    l.lastFrameTime = currentFrameTime;

    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(l.window, &fbW, &fbH);
    if (fbW > 0 && fbH > 0 && (static_cast<uint32_t>(fbW) != l.ctx->targetWidth || static_cast<uint32_t>(fbH) != l.ctx->targetHeight)) {
        l.ctx->configureSurface(static_cast<uint32_t>(fbW), static_cast<uint32_t>(fbH));
        l.ctx->reallocateViewportTargets(static_cast<uint32_t>(fbW), static_cast<uint32_t>(fbH));
    }

#if !defined(__EMSCRIPTEN__)
    if (!l.watchedScriptPath.empty() && std::filesystem::exists(l.watchedScriptPath)) {
        auto currentWrite = std::filesystem::last_write_time(l.watchedScriptPath);
        if (currentWrite != l.lastScriptWriteTime) {
            l.lastScriptWriteTime = currentWrite;
            if (l.scriptEngine->executeFile(l.watchedScriptPath)) {
                l.ctx->uploadRealmGeometryAndEntities(*l.world);
                std::cout << "[Eatsrealms] Hot-reloaded '" << l.watchedScriptPath << "'\n";
            }
        }
    }
#endif

    bool tabDown = glfwGetKey(l.window, GLFW_KEY_TAB) == GLFW_PRESS;
    bool escDown = glfwGetKey(l.window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
    if ((tabDown && !l.wasTabDown) || (escDown && !l.wasEscDown)) {
        l.camState->cursorCaptured = !l.camState->cursorCaptured;
        if (l.camState->cursorCaptured) {
            glfwSetInputMode(l.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            l.camState->firstMouse = true;
        } else {
            glfwSetInputMode(l.window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
    }
    l.wasTabDown = tabDown;
    l.wasEscDown = escDown;

    bool fDown = glfwGetKey(l.window, GLFW_KEY_F) == GLFW_PRESS;
    if (fDown && !l.wasFDown) {
        l.camState->autoFocus = !l.camState->autoFocus;
    }
    l.wasFDown = fDown;

    bool rDown = glfwGetKey(l.window, GLFW_KEY_R) == GLFW_PRESS;
    if (rDown && !l.wasRDown) {
        l.world->camera() = l.camState->defaultCamera;
        l.camState->pitch = l.camState->defaultPitch;
        l.camState->yaw   = l.camState->defaultYaw;
    }
    l.wasRDown = rDown;

    if (l.camState->cursorCaptured) {
        double mx = 0.0, my = 0.0;
        glfwGetCursorPos(l.window, &mx, &my);
        if (l.camState->firstMouse) {
            l.camState->lastMouseX = mx;
            l.camState->lastMouseY = my;
            l.camState->firstMouse = false;
        }
        float xoffset = static_cast<float>(mx - l.camState->lastMouseX);
        float yoffset = static_cast<float>(l.camState->lastMouseY - my);
        l.camState->lastMouseX = mx;
        l.camState->lastMouseY = my;

        const float sensitivity = 0.0022f;
        l.camState->yaw   += xoffset * sensitivity;
        l.camState->pitch += yoffset * sensitivity;
        l.camState->pitch = std::clamp(l.camState->pitch, -1.535f, 1.535f);
    }

    Vec3f fwd{
        std::cos(l.camState->pitch) * std::sin(l.camState->yaw),
        std::sin(l.camState->pitch),
        std::cos(l.camState->pitch) * std::cos(l.camState->yaw)
    };
    Vec3f right{
        std::cos(l.camState->yaw),
        0.0f,
        -std::sin(l.camState->yaw)
    };

    Vec3f moveDir{0.0f, 0.0f, 0.0f};
    if (l.camState->cursorCaptured) {
        if (glfwGetKey(l.window, GLFW_KEY_W) == GLFW_PRESS || glfwGetKey(l.window, GLFW_KEY_UP) == GLFW_PRESS) {
            moveDir.x += fwd.x; moveDir.y += fwd.y; moveDir.z += fwd.z;
        }
        if (glfwGetKey(l.window, GLFW_KEY_S) == GLFW_PRESS || glfwGetKey(l.window, GLFW_KEY_DOWN) == GLFW_PRESS) {
            moveDir.x -= fwd.x; moveDir.y -= fwd.y; moveDir.z -= fwd.z;
        }
        if (glfwGetKey(l.window, GLFW_KEY_D) == GLFW_PRESS || glfwGetKey(l.window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
            moveDir.x += right.x; moveDir.z += right.z;
        }
        if (glfwGetKey(l.window, GLFW_KEY_A) == GLFW_PRESS || glfwGetKey(l.window, GLFW_KEY_LEFT) == GLFW_PRESS) {
            moveDir.x -= right.x; moveDir.z -= right.z;
        }
        if (glfwGetKey(l.window, GLFW_KEY_E) == GLFW_PRESS || glfwGetKey(l.window, GLFW_KEY_SPACE) == GLFW_PRESS) {
            moveDir.y += 1.0f;
        }
        if (glfwGetKey(l.window, GLFW_KEY_Q) == GLFW_PRESS || glfwGetKey(l.window, GLFW_KEY_C) == GLFW_PRESS) {
            moveDir.y -= 1.0f;
        }

        float lenSq = moveDir.x * moveDir.x + moveDir.y * moveDir.y + moveDir.z * moveDir.z;
        if (lenSq > 1e-5f) {
            float invLen = 1.0f / std::sqrt(lenSq);
            moveDir.x *= invLen; moveDir.y *= invLen; moveDir.z *= invLen;

            float speed = l.camState->baseSpeed;
            if (glfwGetKey(l.window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                glfwGetKey(l.window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
                speed *= 2.5f;
            }
            if (glfwGetKey(l.window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                glfwGetKey(l.window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS) {
                speed *= 0.25f;
            }

            l.world->camera().position.x += moveDir.x * speed * dt;
            l.world->camera().position.y += moveDir.y * speed * dt;
            l.world->camera().position.z += moveDir.z * speed * dt;
        }
    }

    l.world->camera().target.x = l.world->camera().position.x + fwd.x * 20.0f;
    l.world->camera().target.y = l.world->camera().position.y + fwd.y * 20.0f;
    l.world->camera().target.z = l.world->camera().position.z + fwd.z * 20.0f;

    if (l.camState->autoFocus) {
        RaycastHit hit = l.world->raycast(l.world->camera().position, fwd, 200.0f);
        float targetDist = hit.hit ? hit.distance : 40.0f;
        float blend = std::clamp(dt * 7.0f, 0.0f, 1.0f);
        l.world->camera().focusDistance = std::lerp(l.world->camera().focusDistance, targetDist, blend);
    }

    bool leftDown = glfwGetMouseButton(l.window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    if (leftDown && !l.wasLeftDown) {
        if (!l.camState->cursorCaptured) {
            l.camState->cursorCaptured = true;
            l.camState->firstMouse = true;
            glfwSetInputMode(l.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        } else {
            RaycastHit hit = l.world->raycast(l.world->camera().position, fwd, 200.0f);
            if (hit.hit) {
                l.foleyEngine->triggerMaterialImpact(hit.layerId, 0.9f);
            }
        }
    }
    l.wasLeftDown = leftDown;

    WGPUSurfaceTexture surfTex{};
    wgpuSurfaceGetCurrentTexture(l.ctx->surface, &surfTex);
    if (surfTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surfTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        return;
    }

    WGPUTextureView backbufferView = wgpuTextureCreateView(surfTex.texture, nullptr);

    SceneUniforms u{};
    fillSceneUniforms(*l.world, l.ctx->targetWidth, l.ctx->targetHeight, static_cast<float>(glfwGetTime()), u, l.camState->cursorCaptured);
    wgpuQueueWriteBuffer(l.ctx->queue, l.ctx->uniformBuffer, 0, &u, sizeof(SceneUniforms));

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(l.ctx->device, nullptr);
    l.ctx->recordFramePasses(encoder, backbufferView);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(l.ctx->queue, 1, &cmd);
#if !defined(__EMSCRIPTEN__)
    wgpuSurfacePresent(l.ctx->surface);
#else
    static bool s_firstFramePresented = false;
    if (!s_firstFramePresented) {
        s_firstFramePresented = true;
        emscripten_run_script("if (window.hideAppLoadingScreen) window.hideAppLoadingScreen();");
    }
#endif

    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(backbufferView);
    wgpuTextureRelease(surfTex.texture);
}

int Hd2dRenderer::runInteractiveWindow(
    RealmWorld& world,
    RealmScriptEngine& scriptEngine,
    const std::string& watchedScriptPath,
    AudioFoleyEngine& foleyEngine,
    uint32_t width,
    uint32_t height
) {
    if (!glfwInit()) {
        std::cerr << "[Hd2dRenderer] Failed to initialize GLFW.\n";
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height),
        "Eatsrealms - Native 2.5D (HD-2D) RPG & Diorama Engine (WebGPU + Eatscript Live Reload)", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }

    GpuHd2dContext ctx;
    if (!ctx.init(window, width, height, world.atlas())) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    ctx.uploadRealmGeometryAndEntities(world);

    std::filesystem::file_time_type lastScriptWriteTime{};
#if !defined(__EMSCRIPTEN__)
    if (!watchedScriptPath.empty() && std::filesystem::exists(watchedScriptPath)) {
        lastScriptWriteTime = std::filesystem::last_write_time(watchedScriptPath);
    }
#endif

    std::cout << "\n============================================================\n"
              << "  Eatsrealms HD-2D Diorama Navigation (Fly Mode Active)     \n"
              << "============================================================\n"
              << "  Mouse Move        : Free Look (Pitch & Yaw)\n"
              << "  W / S / A / D     : Move & Strafe\n"
              << "  E / Space | Q / C : Ascend / Descend\n"
              << "  Left Shift / Ctrl : Sprint (2.5x) / Precision (0.25x)\n"
              << "  Mouse Wheel       : Adjust Flight Speed\n"
              << "  Left Click        : Raycast Terrain + Procedural Foley\n"
              << "  TAB / ESC         : Toggle Cursor Lock (Fly <-> Desktop)\n"
              << "  F                 : Toggle Dynamic Tilt-Shift Auto-Focus\n"
              << "  R                 : Reset Camera to HD-2D Default\n"
              << "============================================================\n\n";

    FlyCamState camState;
    Vec3f initFwd{
        world.camera().target.x - world.camera().position.x,
        world.camera().target.y - world.camera().position.y,
        world.camera().target.z - world.camera().position.z
    };
    float initLen = std::sqrt(initFwd.x * initFwd.x + initFwd.y * initFwd.y + initFwd.z * initFwd.z);
    if (initLen > 1e-5f) {
        initFwd.x /= initLen; initFwd.y /= initLen; initFwd.z /= initLen;
    } else {
        initFwd = {0.0f, 0.0f, 1.0f};
    }
    camState.pitch = std::asin(std::clamp(initFwd.y, -1.0f, 1.0f));
    camState.yaw   = std::atan2(initFwd.x, initFwd.z);
    camState.defaultPitch = camState.pitch;
    camState.defaultYaw   = camState.yaw;
    camState.defaultCamera = world.camera();

    glfwSetWindowUserPointer(window, &camState);
    glfwSetScrollCallback(window, [](GLFWwindow* w, double, double yoffset) {
        auto* state = static_cast<FlyCamState*>(glfwGetWindowUserPointer(w));
        if (state) {
            state->baseSpeed = std::clamp(state->baseSpeed + static_cast<float>(yoffset) * 3.0f, 2.0f, 80.0f);
        }
    });

#if !defined(__EMSCRIPTEN__)
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
#else
    camState.cursorCaptured = false;
#endif

    LoopContext loop;
    loop.world = &world;
    loop.scriptEngine = &scriptEngine;
    loop.watchedScriptPath = watchedScriptPath;
    loop.foleyEngine = &foleyEngine;
    loop.window = window;
    loop.ctx = &ctx;
    loop.camState = &camState;
    loop.lastScriptWriteTime = lastScriptWriteTime;
    loop.lastFrameTime = glfwGetTime();

#if defined(__EMSCRIPTEN__)
    static LoopContext s_webLoop;
    static GpuHd2dContext s_webCtx;
    static FlyCamState s_webCam;
    s_webCtx = std::move(ctx);
    s_webCam = camState;
    s_webLoop = loop;
    s_webLoop.ctx = &s_webCtx;
    s_webLoop.camState = &s_webCam;
    glfwSetWindowUserPointer(window, s_webLoop.camState);

    emscripten_set_main_loop_arg([](void* arg) {
        auto* l = static_cast<LoopContext*>(arg);
        if (l && l->window && !glfwWindowShouldClose(l->window)) {
            renderSingleFrame(*l);
        }
    }, &s_webLoop, 0, 1);
    return 0;
#else
    while (!glfwWindowShouldClose(window)) {
        renderSingleFrame(loop);
    }

    ctx.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
#endif
}

} // namespace eatsrealms
