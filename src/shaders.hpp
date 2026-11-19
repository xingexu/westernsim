#pragma once

#include "graphics.hpp"

namespace wsim::shaders {

inline constexpr char worldVertex[] = WESTERNSIM_GLSL R"GLSL(
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in float material;
uniform mat4 viewProjection, model, lightMatrix;
out vec3 point, face;
out vec4 shadowPoint;
flat out int tile;
void main() {
    vec4 p = model * vec4(position, 1);
    point = p.xyz;
    face = normalize(mat3(model) * normal);
    tile = int(material);
    shadowPoint = lightMatrix * p;
    gl_Position = viewProjection * p;
}
)GLSL";

inline constexpr char worldFragment[] = WESTERNSIM_GLSL R"GLSL(
#ifdef GL_ES
precision highp sampler2DShadow;
#endif
in vec3 point, face;
in vec4 shadowPoint;
flat in int tile;
out vec4 color;
uniform vec3 eye, lightDirection, clockCenter;
uniform vec3 palette[29];
uniform float time;
uniform sampler2DShadow shadowMap;

float hash(vec3 p) {
    p = fract(p * .1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float sunlight(vec3 n) {
    vec3 p = shadowPoint.xyz * .5 + .5;
    if (any(greaterThan(p,vec3(1.))) || any(lessThan(p,vec3(0.)))) return 1.;
    float depth = p.z - mix(.0012,.00050,max(dot(n,lightDirection),0.));
    vec2 px = 1. / vec2(textureSize(shadowMap,0));
    return .25 * (texture(shadowMap,vec3(p.xy+px*vec2(-1.2,.4),depth)) +
                  texture(shadowMap,vec3(p.xy+px*vec2(.4,1.2),depth)) +
                  texture(shadowMap,vec3(p.xy+px*vec2(1.2,-.4),depth)) +
                  texture(shadowMap,vec3(p.xy+px*vec2(-.4,-1.2),depth)));
}

float edgeDistance(vec2 p) {
    vec2 edge = .5 - abs(fract(p)-.5);
    return min(edge.x,edge.y);
}

vec3 masonry(vec2 p, vec3 stone, vec2 repeats, float joint) {
    vec2 tileUV = p * repeats;
    tileUV.x += mod(floor(tileUV.y),2.)*.5;
    float edge = edgeDistance(tileUV);
    float fine = smoothstep(joint,joint+.033,edge);
    float bevel = (1.-smoothstep(joint+.02,joint+.10,edge)) * fine;
    float tone = mix(.91,1.09,hash(vec3(floor(tileUV),17.)));
    return mix(stone*.69,stone*tone,fine) + stone*.095*bevel;
}

void main() {
    vec3 n = normalize(face);
    vec3 view = normalize(eye - point);
    vec3 albedo = palette[tile];
    vec3 cell = floor(point - n * .01);
    float grain = hash(cell);
    vec3 texel = point - n*.001;
    vec3 uv = fract(texel);
    vec2 face = abs(n.y) > .5 ? point.xz : abs(n.x) > .5 ? point.zy : point.xy;
    vec2 faceUV = fract(face);
    float roughSpecular = 0.;
    if (tile == 1 && n.y > .5) {
        float swale = .5 + .5*sin(point.x*.41+sin(point.z*.30))*sin(point.z*.49);
        albedo *= .91 + .17*swale;
        float grainAtDistance = 1.-smoothstep(12.,35.,length(eye-point));
        albedo *= 1. + (hash(vec3(floor(point.xz*25.),7.))-.5)*.11*grainAtDistance;
    }
    if (tile == 1 && n.y < .5) {
        albedo *= .78;
        float fringe = .78 + .044*sin(face.x*18.) + .033*sin(face.x*9.);
        albedo = mix(mix(palette[2],palette[1],.08),albedo,smoothstep(fringe-.055,fringe+.055,uv.y));
    }
    if (tile == 5 || tile == 7) {
        float bark = step(.64, hash(vec3(floor(texel.x * 5.), floor(texel.y * 3.), floor(texel.z * 5.))));
        albedo *= 1. - bark * (tile == 7 ? .26 : .11);
    }
    if (tile == 11) {
        float grainLine = smoothstep(.70, .98, sin(texel.z * 22. + sin(texel.x * 1.7)));
        albedo *= 1.0 - grainLine * .14;
        if (n.y > .5 && uv.x < .025) albedo *= .79;
    }
    if (tile == 9) albedo = masonry(face,albedo,abs(n.y) > .5 ? vec2(1.32) : vec2(1.2,1.65),.021);
    if (tile == 10) albedo = masonry(face,albedo,vec2(1.6,3.3),.045);
    if (tile == 14) albedo = masonry(face,albedo,vec2(1.05,2.12),.033);
    if (tile == 18) albedo *= .93 + .11 * hash(vec3(floor(face*.5),11.));
    if (tile == 16 || tile == 22 || tile == 23) {
        float across = faceUV.x;
        float frame = 1.-smoothstep(.024,.049,min(across,1.-across));
        float mullion = 1.-smoothstep(.010,.030,abs(across-.5));
        vec3 reflected = reflect(-view,n);
        float skyTint = clamp(reflected.y*.65+.63,0.,1.);
        float glimmer = smoothstep(.40,.51,skyTint)*(.94+.06*sin(reflected.x*11.+reflected.z*7.));
        albedo *= .77;
        albedo += glimmer * vec3(.10,.17,.24);
        roughSpecular = .18 * (1.-max(frame,mullion));
        if (tile == 22 || tile == 23) {
            vec2 panel = face * vec2(3.,4.);
            float glass = hash(vec3(floor(panel),15.));
            albedo += mix(vec3(.11,.16,.38),vec3(.49,.27,.14),glass)*.67;
            albedo *= mix(.59,1.,smoothstep(.025,.055,edgeDistance(panel)));
        }
        albedo = mix(albedo,palette[14]*.79,max(frame,mullion));
        if (tile == 23) albedo = mix(albedo,palette[14],smoothstep(.61,.68,uv.y+.88*abs(across-.5)));
    }
    if (tile == 24) {
        vec3 relative = texel - clockCenter;
        vec2 clock = vec2(abs(n.x) > .5 ? relative.z : relative.x,relative.y);
        if (n.z < -.5 || n.x > .5) clock.x = -clock.x;
        float radius = length(clock);
        albedo = radius > 1.37 ? palette[14] : radius > 1.19 ? vec3(.78,.67,.46) : palette[24];
        bool tick = (abs(clock.x) < .11 || abs(clock.y) < .11) && radius > .96;
        bool hands = (abs(clock.x) < .11 && clock.y > -.13 && clock.y < .83) ||
                     (abs(clock.y+.33*clock.x) < .12 && clock.x > -.15 && clock.x < .59);
        if (radius < 1.18 && (tick || hands)) albedo = vec3(1.,.89,.65);
    }
    if (tile == 15 || tile == 19) {
        vec2 course = face * vec2(1.7,2.7);
        float ridges = smoothstep(.015,.11,edgeDistance(course));
        albedo *= mix(.76,1.08,ridges);
        roughSpecular = tile == 19 ? .10 : .025;
    }
    if (tile == 6 || tile == 8 || tile == 20 || tile == 21 || tile == 28) {
        float cluster = .5+.5*sin(dot(texel,vec3(8.4,9.7,10.3)))*sin(dot(texel,vec3(-7.2,3.4,11.)));
        albedo *= .92 + .16*cluster;
        if (tile == 28) {
            vec2 petals = fract(face*6.)-.5;
            float blossom = 1.-smoothstep(.10,.20,length(petals));
            float bloom = hash(vec3(floor(face*6.),19.));
            albedo = mix(albedo,mix(vec3(.97,.71,.95),vec3(1.,.79,.29),bloom),blossom*step(.55,bloom));
        }
    }
    if (tile == 17 && n.y > .5) {
        albedo *= .96 + .075*hash(vec3(floor(face*12.),3.));
        albedo *= .94 + .06*smoothstep(.04,.14,edgeDistance(face*.25));
    }
    if (tile == 25) roughSpecular = .19;
    float diffuse = max(0., dot(n, lightDirection));
    float direct = sunlight(n);
    vec3 ambient = mix(vec3(.59,.62,.76),vec3(.76,.83,.93),n.y*.5+.5);
    vec3 lit = albedo * (ambient + diffuse * direct * vec3(.41,.35,.24));
    vec3 halfway = normalize(lightDirection+view);
    lit += vec3(1.,.92,.78) * roughSpecular * direct * pow(max(0.,dot(n,halfway)),24.);
    if (tile == 13) {
        float across = abs(n.x) > .5 ? uv.z : uv.x;
        bool frame = n.y > .5 || uv.y < .12 || uv.y > .86 || across < .12 || across > .88;
        lit = frame ? vec3(.37,.29,.25) : mix(vec3(.99,.57,.21),vec3(1.,.84,.48),uv.y);
    }
    if (tile == 12) {
        float a = sin(dot(texel.xz, vec2(.74, 1.61)) + time * 1.6);
        float b = sin(dot(texel.xz, vec2(1.34,-.66)) - time * 1.2);
        vec3 ripple = normalize(vec3(.045 * a, 1., .049 * b));
        float fresnel = pow(1. - clamp(dot(view,ripple),0.,1.), 3.);
        float glint = pow(max(dot(reflect(-lightDirection,ripple),view),0.), 26.);
        float lines = smoothstep(.82,.99,.5+.5*sin(texel.z*8.5+a+b*1.7));
        lit = mix(vec3(.10,.46,.57),vec3(.60,.82,.97),fresnel*.80);
        lit += vec3(.60,.42,.23) * glint + vec3(.10,.19,.20)*lines*(.33+.38*b*b);
        lit *= .86 + .14*direct;
    }
    float distanceToEye = length(eye - point);
    float fog = 1.0 - exp(-pow(distanceToEye * .0097, 1.61));
    vec3 haze = vec3(.72,.85,.95);
    color = vec4(mix(lit,haze,fog*.78),1.);
}
)GLSL";

inline constexpr char skyVertex[] = WESTERNSIM_GLSL R"GLSL(
out vec2 ndc;
void main() {
    ndc = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2. - 1.;
    gl_Position = vec4(ndc, 0., 1.);
}
)GLSL";

inline constexpr char avatarVertex[] = WESTERNSIM_GLSL R"GLSL(
layout(location=0) in vec3 position;
layout(location=1) in vec3 color;
uniform mat4 transform;
out vec3 bakedColor;
void main() {
    bakedColor = color;
    gl_Position = transform * vec4(position, 1.);
}
)GLSL";

inline constexpr char avatarFragment[] = WESTERNSIM_GLSL R"GLSL(
in vec3 bakedColor;
out vec4 color;
void main() {
    color = vec4(bakedColor, 1.);
}
)GLSL";

inline constexpr char skyFragment[] = WESTERNSIM_GLSL R"GLSL(
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
    vec3 sky = mix(vec3(.78,.88,1.),vec3(.27,.62,.95),horizon);
    float sun = max(0.,dot(ray,lightDirection));
    sky = mix(sky,vec3(1.,.81,.47), .39 * pow(sun,8.));
    sky += vec3(.16,.094,.031) * pow(sun,56.);
    sky = mix(sky,vec3(1.,.96,.8),smoothstep(.9987,.9991,sun));
    if (ray.y > .026) {
        vec2 p = ray.xz / (ray.y + .09) * vec2(2.7,4.1) + vec2(time*.025,0);
        float mass = .60*noise(p) + .25*noise(p*2.03) + .10*noise(p*4.10) + .05*noise(p*8.25);
        float mask = smoothstep(.54,.66,mass) * smoothstep(.023,.14,ray.y);
        vec3 cloud = mix(vec3(.78,.86,.96),vec3(1.,.985,.95),smoothstep(.54,.70,mass));
        sky = mix(sky,cloud,mask);
    }
    float vignette = 1. - .12 * pow(length(ndc * vec2(.69,.58)), 2.1);
    color = vec4(sky*vignette,1.);
}
)GLSL";

inline constexpr char depthVertex[] = WESTERNSIM_GLSL R"GLSL(
layout(location=0) in vec3 position;
layout(location=2) in float material;
uniform mat4 lightMatrix, model;
flat out float tile;
void main() { tile = material; gl_Position = lightMatrix * model * vec4(position,1.); }
)GLSL";

inline constexpr char depthFragment[] = WESTERNSIM_GLSL R"GLSL(
flat in float tile;
void main() { if (tile == 12.) discard; }
)GLSL";

inline constexpr char lineVertex[] = WESTERNSIM_GLSL R"GLSL(
layout(location=0) in vec3 position;
uniform mat4 transform;
void main() { gl_Position = transform * vec4(position, 1.); }
)GLSL";

inline constexpr char lineFragment[] = WESTERNSIM_GLSL R"GLSL(
out vec4 color;
void main() { color = vec4(.98,.91,.67,1.); }
)GLSL";

} // namespace wsim::shaders
