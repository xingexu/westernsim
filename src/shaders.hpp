#pragma once

#include "graphics.hpp"

namespace wsim::shaders {

inline constexpr char worldVertex[] = WESTERNSIM_GLSL R"GLSL(
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in float material;
uniform mat4 viewProjection, model, lightMatrix;
out vec3 point, face;
out vec3 localPosition;
flat out vec3 localNormal;
out vec4 shadowPoint;
flat out int tile;
void main() {
    vec4 p = model * vec4(position, 1);
    point = p.xyz;
    face = normalize(mat3(model) * normal);
    localPosition = position;
    localNormal = normal;
    tile = int(material);
    shadowPoint = lightMatrix * p;
    gl_Position = viewProjection * p;
}
)GLSL";

inline constexpr char worldFragment[] = WESTERNSIM_GLSL R"GLSL(
#ifdef GL_ES
precision highp sampler2DShadow;
precision highp sampler2D;
#endif
in vec3 point, face;
in vec3 localPosition;
flat in vec3 localNormal;
in vec4 shadowPoint;
flat in int tile;
out vec4 color;
uniform vec3 eye, lightDirection, clockCenter;
uniform vec3 palette[70];
uniform sampler2DShadow shadowMap;
uniform sampler2D waveMap;
uniform bool isCrate;

float surfaceHeight(ivec2 cell) {
    return texelFetch(waveMap,clamp(cell,ivec2(0),textureSize(waveMap,0)-1),0).r;
}

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

vec3 glazing(vec2 bay, vec3 tint, float panes, float crossbar) {
    vec2 f = fract(bay);
    vec3 frame = tint*.37;
    if (abs(fract(f.x*panes)-.5) > .462 || abs(f.y-crossbar) < .025) return frame;
    float reflection = .78 + .29*floor(f.y*6.)/5.;
    float light = hash(vec3(floor(bay*vec2(panes,1.)),4.));
    return tint*reflection + vec3(.13,.12,.08)*step(.82,light);
}

vec3 facade(vec2 p, vec3 tone, int kind) {
    bool physics = kind == 52;
    bool gothic = kind == 38 || kind == 42 || kind == 47 || physics || kind == 55;
    bool brick = kind == 35 || kind == 41 || kind == 44 || kind == 56 || kind == 66;
    bool brutalist = kind == 40 || kind == 49 || kind == 50 || kind == 54;
    bool recreation = kind == 48;
    bool bands = kind == 37 || kind == 45 || kind == 54;
    if (kind == 46) {
        vec2 bay = p/vec2(4.8,2.05), f = fract(bay);
        if (f.y < .125 || f.x > .89) return masonry(p,tone,vec2(1.15,2.5),.02);
        return glazing(bay,vec3(.25,.43,.50),6.,.68);
    }
    if (kind == 68 || kind == 53) {
        vec2 bay = p/vec2(2.55,2.15), f = fract(bay);
        if (f.y < .12 || f.x > .93) return tone;
        return glazing(bay,kind == 68 ? vec3(.25,.39,.47) : vec3(.32,.49,.52),3.,.75);
    }
    if (kind == 39 || kind == 51 || kind == 69) {
        vec2 bay = p/vec2(kind == 39 ? 2.4 : 1.16,1.8), f = fract(bay);
        float column = step(.14,f.x)*step(f.x,.86);
        vec3 concrete = tone*(.89+.16*column+.05*hash(vec3(floor(p*3.),10.)));
        if (f.y < .44 || f.y > .77 || column == 0.) return concrete;
        return glazing(bay,vec3(.29,.34,.36),kind == 39 ? 3. : 1.,.65);
    }
    float bayWidth = gothic ? 2. : bands ? 1.25 : 1.5;
    float floorHeight = gothic ? 2.25 : 1.65;
    if (kind == 42) bayWidth = 1.7;
    if (kind == 49 || kind == 50) bayWidth = 1.1;
    if (kind == 45 || recreation) floorHeight = 2.;
    if (kind == 66 || kind == 67) bayWidth = 2.6;
    vec2 bay = p / vec2(bayWidth,floorHeight);
    vec2 f = fract(bay);
    float weather = hash(vec3(floor(bay),19.));
    vec2 courses = gothic ? vec2(2.3,3.5) : brick ? vec2(2.7,5.5) : vec2(1.2,2.0);
    vec3 wall = mix(tone,masonry(p,tone,courses,.031),brutalist ? .20 : .73);
    if (kind == 66 || kind == 67) {
        bool glazedBay = mod(floor(bay.x),kind == 66 ? 4. : 5.) < 2.;
        if (glazedBay && f.x < .92 && f.y > .12 && f.y < .91)
            return glazing(bay,vec3(.29,.43,.49),4.,.64);
        if (kind == 67 && f.y < .13) wall *= .70;
    }
    if (kind == 41 && mod(floor(bay.x),7.) >= 6. && f.y > .11 && f.x < .93)
        return glazing(bay,vec3(.27,.38,.46),3.,.64);
    if (f.y > .88 || (brutalist && f.y < .09)) wall *= 1.10;
    if ((kind == 41 || kind == 44 || kind == 56) && mod(floor(bay.x),5.) < 2.)
        wall = mix(wall,tone*vec3(1.18,1.22,1.25),.74);
    if (kind == 40 && mod(floor(bay.x),7.) == 1.)
        wall = mix(wall,vec3(.38,.28,.50),.52);
    if (kind == 50 && f.x < .12) wall *= .75;
    float dx = abs(f.x-.5);
    float width = bands ? .43 : gothic ? .31 : .36;
    float upper = gothic ? .84-max(0.,dx-.07)*.66 : .79;
    float bottom = .18;
    if (kind == 44 || kind == 56) width = .26;
    if (kind == 49) { width = .30; bottom = .22; upper = .72; }
    if (kind == 50) { width = .29; bottom = .25; upper = .68; }
    if (kind == 54 && mod(floor(bay.x),5.) > 2.) width = .15;
    if (kind == 45 && mod(floor(bay.x),6.) > 3.) width = .22;
    if (recreation) { width = .474; bottom = .085; upper = .93; }
    if (physics && dx < .10 && abs(f.y-.9) < .048)
        return tone*(1.03+.22*step(f.y,.90));
    if (f.y < bottom-.04 || f.y > upper+.045 || dx > width+.055) return wall;
    if (f.y < bottom || f.y > upper || dx > width)
        return tone*1.13;
    if ((!gothic && dx < .036) || (gothic && abs(dx-.108) < .025) || abs(f.y-.42) < .022)
        return tone*.67;
    vec3 glass = mix(vec3(.22,.25,.31),vec3(.39,.53,.58),weather);
    if (recreation) glass = mix(vec3(.25,.33,.37),vec3(.39,.56,.61),weather);
    if (f.y > .61 || abs(f.x+f.y*.65-.76) < .065) glass += vec3(.08,.13,.16);
    return glass;
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
    face = (floor(face * 20.) + .5) / 20.;
    vec2 faceUV = fract(face);
    float roughSpecular = 0.;
    if (tile == 1 && n.y > .5) {
        float swale = .5 + .5*sin(face.x*.41+sin(face.y*.30))*sin(face.y*.49);
        float grassTone = floor(hash(vec3(floor(face*.75),31.))*4.)/4.;
        float fleck = hash(vec3(floor(face*14.),7.));
        albedo *= .90 + .13*swale + .12*grassTone;
        if (length(eye-point) < 20.) {
            if (fleck > .89) albedo *= 1.14;
            if (fleck < .13) albedo *= .83;
        }
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
    if (isCrate) {
        vec3 p = floor(localPosition*32.)/32.;
        vec2 board = abs(localNormal.y) > .5 ? p.xz : abs(localNormal.x) > .5 ? p.zy : p.xy;
        float frame = edgeDistance(board);
        float seam = fract(board.y*4.);
        float grainLine = sin(board.x*25.+sin(board.y*170.)*.56);
        vec3 timber = vec3(.72,.46,.26) * (.88+grainLine*.057+.13*hash(vec3(floor(board.y*4.),3.,8.)));
        timber *= seam < .095 ? .69 : 1.;
        if (frame < .11 || abs(board.x-board.y) < .068)
            timber = vec3(.54,.33,.18) * (.93+grainLine*.06);
        if (frame < .03) timber *= .74;
        albedo = timber;
    }
    if (tile == 9) albedo = masonry(face,albedo,abs(n.y) > .5 ? vec2(1.32) : vec2(1.2,1.65),.021);
    if (tile == 10) albedo = masonry(face,albedo,vec2(1.6,3.3),.045);
    if (tile == 14) albedo = masonry(face,albedo,vec2(1.05,2.12),.033);
    if (tile == 18) albedo *= .93 + .11 * hash(vec3(floor(face*.5),11.));
    if (((tile >= 35 && tile <= 56 && tile != 43) || tile >= 66) && abs(n.y) < .5) {
        vec2 facadePoint = vec2(point.x*.89+point.z*.455,point.y);
        if (abs(n.x) > .5) facadePoint.x = point.z*.89-point.x*.455;
        vec3 detail = facade((floor(facadePoint*24.)+.5)/24.,albedo,tile);
        albedo = mix(detail,albedo*.84,smoothstep(65.,150.,length(eye-point)));
    }
    if (tile == 57 || tile == 59 || tile == 61 || tile == 64)
        albedo *= .90+.16*hash(vec3(floor(face*9.),float(tile)));
    if (tile == 58 || tile == 60)
        albedo *= .91+.11*step(.5,fract(face.x*.18))+.07*hash(vec3(floor(face*6.),18.));
    if (tile == 62) albedo = masonry(face,albedo,vec2(3.5,5.),.034);
    if (tile == 63) albedo *= .90+.09*smoothstep(.012,.05,edgeDistance(face*.42));
    if (tile == 65) albedo *= .77+.23*smoothstep(.024,.17,abs(fract(face.x*2.)-.5));
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
        vec2 clock = 2.*vec2(abs(n.x) > .5 ? relative.z : relative.x,relative.y);
        if (n.z < -.5 || n.x > .5) clock.x = -clock.x;
        float radius = length(clock);
        albedo = radius > 1.37 ? masonry(face,palette[14],vec2(2.3,3.5),.031) :
                 radius > 1.19 ? vec3(.78,.67,.46) : palette[24];
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
        float leaf = hash(vec3(floor(face*6.),17.));
        float cluster = hash(vec3(floor(face*1.8),3.));
        albedo *= .80 + .13*floor(leaf*4.)/3. + .19*floor(cluster*4.)/3.;
        if (leaf > .93) albedo += vec3(.045,.04,.012);
    }
    if (tile == 17 && n.y > .5) {
        albedo *= .96 + .075*hash(vec3(floor(face*12.),3.));
        albedo *= .94 + .06*smoothstep(.04,.14,edgeDistance(face*.25));
    }
    if (tile == 25) roughSpecular = .19;
    float diffuse = max(0., dot(n, lightDirection));
    float direct = sunlight(n);
    vec3 ambient = mix(vec3(.59,.60,.75),vec3(.84,.85,.93),n.y*.5+.5);
    vec3 lit = albedo * (ambient + diffuse * floor(direct*4.+.5)*.25 * vec3(.42,.38,.28));
    vec3 halfway = normalize(lightDirection+view);
    lit += vec3(1.,.92,.78) * roughSpecular * direct * pow(max(0.,dot(n,halfway)),24.);
    if (tile == 13) {
        float across = abs(n.x) > .5 ? uv.z : uv.x;
        bool frame = n.y > .5 || uv.y < .12 || uv.y > .86 || across < .12 || across > .88;
        lit = frame ? vec3(.37,.29,.25) : mix(vec3(.99,.57,.21),vec3(1.,.84,.48),uv.y);
    }
    if (tile == 12) {
        vec3 surface = n;
        float lift = 0.;
        if (n.y > .5) {
            ivec2 p = ivec2(floor(point.xz));
            vec2 gradient = vec2(surfaceHeight(p+ivec2(1,0))-surfaceHeight(p-ivec2(1,0)),
                                 surfaceHeight(p+ivec2(0,1))-surfaceHeight(p-ivec2(0,1)));
            surface = normalize(vec3(-gradient.x*3.5,1.,-gradient.y*3.5));
            lift = surfaceHeight(p);
        }
        float reflection = floor((1.-max(0.,dot(view,surface)))*6.)/6.;
        lit = mix(vec3(.23,.44,.53),vec3(.45,.63,.68),reflection);
        lit *= .955+.09*hash(vec3(floor(face*vec2(1.4,2.7)),4.));
        lit += vec3(.22,.28,.26)*clamp(lift*2.5, -.35,.8);
        lit += vec3(.33,.32,.23)*pow(max(0.,dot(surface,halfway)),15.)*direct;
        lit *= .87 + .13*direct;
    }
    float distanceToEye = length(eye - point);
    float fog = 1.0 - exp(-pow(distanceToEye * .0075, 1.57));
    vec3 haze = vec3(.73,.81,.87);
    vec2 px = mod(floor(gl_FragCoord.xy),2.);
    float dither = (px.x*2.+px.y*3.)*.17-.4;
    color = vec4(floor(clamp(mix(lit,haze,fog*.86),0.,1.)*41.+dither+.5)/41.,1.);
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

inline constexpr char pixelFragment[] = WESTERNSIM_GLSL R"GLSL(
in vec2 ndc;
out vec4 color;
uniform sampler2D scene;
void main() {
    color = texture(scene,ndc*.5+.5);
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
    float horizon = floor(smoothstep(-.12, .54, ray.y)*16.)/16.;
    vec3 sky = mix(vec3(.78,.86,.91),vec3(.37,.58,.79),horizon);
    float sun = max(0.,dot(ray,lightDirection));
    sky = mix(sky,vec3(1.,.81,.47), .39 * pow(sun,8.));
    sky += vec3(.16,.094,.031) * pow(sun,56.);
    sky = mix(sky,vec3(1.,.96,.8),smoothstep(.9987,.9991,sun));
    if (ray.y > .026) {
        vec2 p = ray.xz / (ray.y + .09) * vec2(2.7,4.1) + vec2(time*.025,0);
        float mass = .60*noise(p) + .25*noise(p*2.03) + .10*noise(p*4.10) + .05*noise(p*8.25);
        float mask = step(.58,mass)*smoothstep(.023,.14,ray.y);
        vec3 cloud = mass > .66 ? vec3(1.,.96,.85) :
                     mass > .62 ? vec3(.93,.91,.86) : vec3(.79,.83,.87);
        sky = mix(sky,cloud,mask);
    }
    color = vec4(sky,1.);
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

} // namespace wsim::shaders
