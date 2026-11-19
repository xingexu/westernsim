#pragma once

namespace ql::shaders {

inline constexpr char worldVertex[] = R"GLSL(#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in float material;
uniform mat4 viewProjection, model, lightMatrix;
uniform float materialOverride;
out vec3 point, face;
out vec4 shadowPoint;
flat out int tile;
void main() {
    vec4 p = model * vec4(position, 1);
    point = p.xyz;
    face = normalize(mat3(model) * normal);
    tile = int(materialOverride < 0 ? material : materialOverride);
    shadowPoint = lightMatrix * p;
    gl_Position = viewProjection * p;
}
)GLSL";

inline constexpr char worldFragment[] = R"GLSL(#version 330 core
in vec3 point, face;
in vec4 shadowPoint;
flat in int tile;
out vec4 color;
uniform vec3 eye, lightDirection;
uniform vec3 palette[37];
uniform float time;
uniform sampler2D shadowMap;

float hash(vec3 p) {
    p = fract(p * .1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float shadow(vec3 n) {
    vec3 p = shadowPoint.xyz * .5 + .5;
    if (p.z > 1.0 || p.z < 0.0) return 0.0;
    float bias = mix(.0011, .00045, max(dot(n, lightDirection), 0.0));
    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0));
    float occluded = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            occluded += p.z - bias > texture(shadowMap, p.xy + vec2(x,y) * texel).r ? 1.0 : 0.0;
    return occluded / 9.0;
}

void main() {
    vec3 n = normalize(face);
    vec3 view = normalize(eye - point);
    vec3 albedo = palette[clamp(tile, 0, 36)];
    vec3 cell = floor(point - n * .01);
    float grain = hash(cell);
    vec3 uv = fract(point - n * .01);
    float variation = mix(.91, 1.10, grain);
    if (tile == 1 && n.y < .5) {
        albedo *= .78;
        if (uv.y < .72 + grain * .10) albedo = mix(palette[2], palette[1], .14);
    }
    if (tile == 5 || tile == 7) {
        float bark = step(.64, hash(vec3(floor(point.x * 5.), floor(point.y * 4.), floor(point.z * 5.))));
        albedo *= 1. - bark * (tile == 7 ? .26 : .11);
    }
    if (tile == 11) {
        float grainLine = smoothstep(.79, .98, sin(point.z * 27. + sin(point.x * 1.7)));
        albedo *= 1.0 - grainLine * .14;
        if (n.y > .5 && uv.x < .025) albedo *= .79;
    }
    if (tile == 9 || tile == 10 || tile == 18)
        albedo *= .96 + .08 * hash(floor(point * 4. - n * .03));
    if (tile == 14) {
        float offset = fract(floor(point.y * 2.) * .5) * .5;
        vec2 stone = vec2((point.x + point.z)*.92 + offset,point.y*2.);
        float mortar = step(fract(stone.x),.025) + step(fract(stone.y),.04);
        albedo *= .92 + .17 * hash(floor(vec3(stone,n.x+n.z))) - .09 * min(mortar,1.);
    }
    if (tile == 16 || tile == 22) {
        float across = abs(n.x) > .5 ? uv.z : uv.x;
        float mullion = step(abs(across - .5),.029);
        albedo = mix(albedo,albedo * .62,mullion);
        albedo += vec3(.10,.13,.16) * (.29 + .48 * max(n.x+n.z,0.));
        if (tile == 22) albedo += vec3(.12,.06,.18)*hash(floor(point*3.-n*.03));
    }
    if (tile == 17 && n.y > .5 && (uv.x < .022 || uv.z < .022)) albedo *= .91;
    if (tile >= 30) variation = 1.;
    float diffuse = max(0., dot(n, lightDirection));
    float shade = shadow(n);
    vec3 ambient = mix(vec3(.47,.54,.63), vec3(.69,.72,.69), n.y * .5 + .5);
    vec3 lit = albedo * variation * (ambient + diffuse * (1. - shade * .79) * vec3(.47,.39,.25));
    if (tile == 13) {
        float across = abs(n.x) > .5 ? uv.z : uv.x;
        bool frame = n.y > .5 || uv.y < .12 || uv.y > .86 || across < .12 || across > .88;
        lit = frame ? vec3(.37,.29,.25) : mix(vec3(.99,.57,.21),vec3(1.,.84,.48),uv.y);
    }
    if (tile == 12) {
        float a = sin(dot(point.xz, vec2(.74, 1.61)) + time * 1.6);
        float b = sin(dot(point.xz, vec2(1.34,-.66)) - time * 1.2);
        vec3 ripple = normalize(vec3(.045 * a, 1., .049 * b));
        float fresnel = pow(1. - clamp(dot(view,ripple),0.,1.), 3.);
        float glint = pow(max(dot(reflect(-lightDirection,ripple),view),0.), 26.);
        float lines = pow(.5 + .5 * sin(point.z * 6.5 + a + b * 1.7), 13.);
        lit = mix(vec3(.072,.37,.40), vec3(.59,.79,.80), fresnel * .68);
        lit += vec3(.22,.26,.20) * glint + vec3(.13,.18,.17) * lines * (.33 + .38*b*b);
        lit *= 1. - shade * .16;
    }
    float distanceToEye = length(eye - point);
    float fog = 1.0 - exp(-pow(distanceToEye * .0097, 1.61));
    vec3 haze = mix(vec3(.65,.82,.83), vec3(.91,.84,.66), .34);
    color = vec4(mix(lit, haze, fog * .83), 1.);
}
)GLSL";

inline constexpr char skyVertex[] = R"GLSL(#version 330 core
out vec2 ndc;
void main() {
    ndc = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2. - 1.;
    gl_Position = vec4(ndc, 0., 1.);
}
)GLSL";

inline constexpr char skyFragment[] = R"GLSL(#version 330 core
in vec2 ndc;
out vec4 color;
uniform mat4 inverseViewProjection;
uniform vec3 eye, lightDirection;
uniform float time;

float hash(vec2 p) {
    return fract(sin(dot(p,vec2(127.1,311.7))) * 43758.5453);
}
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 k = f*f*(3.-2.*f);
    return mix(mix(hash(i),hash(i+vec2(1,0)),k.x),
               mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),k.x),k.y);
}
void main() {
    vec4 farPoint = inverseViewProjection * vec4(ndc, 1., 1.);
    vec3 ray = normalize(farPoint.xyz / farPoint.w - eye);
    float horizon = smoothstep(-.12, .52, ray.y);
    vec3 sky = mix(vec3(.78,.86,.82),vec3(.28,.59,.73),horizon);
    float sun = max(0.,dot(ray,lightDirection));
    sky = mix(sky,vec3(1.,.81,.47), .39 * pow(sun,8.));
    sky += vec3(.16,.094,.031) * pow(sun,56.);
    sky = mix(sky,vec3(1.,.96,.8),smoothstep(.9987,.9991,sun));
    if (ray.y > .026) {
        vec2 p = ray.xz / (ray.y + .09) * vec2(2.7,4.1) + vec2(time*.025,0);
        float mass = .60*noise(p) + .25*noise(p*2.03) + .10*noise(p*4.10) + .05*noise(p*8.25);
        float mask = smoothstep(.53,.70,mass) * smoothstep(.023,.16,ray.y) * .87;
        vec3 cloud = mix(vec3(.82,.87,.86),vec3(1.,.96,.84),smoothstep(.54,.70,mass));
        sky = mix(sky,cloud,mask);
    }
    float vignette = 1. - .12 * pow(length(ndc * vec2(.69,.58)), 2.1);
    color = vec4(sky * vignette,1.);
}
)GLSL";

inline constexpr char depthVertex[] = R"GLSL(#version 330 core
layout(location=0) in vec3 position;
layout(location=2) in float material;
uniform mat4 lightMatrix, model;
flat out float tile;
void main() { tile = material; gl_Position = lightMatrix * model * vec4(position,1.); }
)GLSL";

inline constexpr char depthFragment[] = R"GLSL(#version 330 core
flat in float tile;
void main() { if (tile == 12.) discard; }
)GLSL";

inline constexpr char lineVertex[] = R"GLSL(#version 330 core
layout(location=0) in vec3 position;
uniform mat4 transform;
void main() { gl_Position = transform * vec4(position, 1.); }
)GLSL";

inline constexpr char lineFragment[] = R"GLSL(#version 330 core
out vec4 color;
void main() { color = vec4(.98,.91,.67,1.); }
)GLSL";

} // namespace ql::shaders
