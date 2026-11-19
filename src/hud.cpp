#include "hud.hpp"
#include "campus.hpp"
#include "font_atlas.hpp"

#include "graphics.hpp"
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace wsim {
namespace {

using Color = glm::vec4;
using Point = glm::vec2;
constexpr Color ivory{.99f,.98f,1.f,1.f};
constexpr Color muted{.76f,.78f,.85f,1.f};
constexpr Color accent{.99f,.81f,.42f,1.f};
constexpr Color panel{.12f,.09f,.18f,.99f};
constexpr Color border{.53f,.40f,.65f,1.f};
constexpr Color lilac{.84f,.76f,1.f,1.f};

constexpr char vertex[] = WESTERNSIM_GLSL R"GLSL(
layout(location=0) in vec2 screen;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 tint;
uniform vec2 size;
out vec2 atlasUV;
out vec4 rgba;
void main() {
    gl_Position = vec4(screen / size * vec2(2,-2) + vec2(-1,1),0,1);
    atlasUV = uv;
    rgba = tint;
}
)GLSL";
constexpr char fragment[] = WESTERNSIM_GLSL R"GLSL(
in vec2 atlasUV;
in vec4 rgba;
out vec4 color;
uniform sampler2D atlas;
uniform sampler2D campus;
void main() {
    if (atlasUV.x >= 2.) {
        color = texture(campus, atlasUV - vec2(2,0));
    } else {
        float alpha = atlasUV.x < 0. ? 1. : step(.46, texture(atlas, atlasUV).r);
        color = vec4(rgba.rgb, rgba.a * alpha);
    }
}
)GLSL";

struct HudVertex { Point point, uv; Color color; };

class Canvas {
public:
    std::vector<HudVertex> vertices;

    void triangle(Point a, Point b, Point c, Color color) {
        constexpr Point blank{-1.f};
        vertices.insert(vertices.end(), {{a,blank,color},{b,blank,color},{c,blank,color}});
    }

    void rect(float x, float y, float w, float h, Color color) {
        quad({x,y},{x+w,y},{x+w,y+h},{x,y+h}, color);
    }

    void mapImage(Point p, float edge) {
        // The atlas UV range [2,3] selects the campus texture in the fragment shader.
        quad(p,p+Point(edge,0),p+edge,p+Point(0,edge),ivory,{2,0},{3,1});
    }

    void card(float x, float y, float w, float h, Color fill = panel, Color edge = border) {
        rect(x+4,y+4,w,h,{.06f,.045f,.09f,.85f});
        rect(x,y,w,h,edge);
        rect(x+2,y+2,w-4,h-4,fill);
    }

    static int glyph(unsigned char c, bool bold) {
        return (c >= 32 && c <= 126 ? c - 32 : '?' - 32) + (bold ? 96 : 0);
    }

    static float width(std::string_view text, float size, bool bold) {
        float sum = 0;
        for (const auto c : text) sum += font::advances[glyph(c,bold)];
        return sum * size / (font::size * 32.f);
    }

    void text(float x, float baseline, std::string_view text, float size, Color color, bool bold = false) {
        const float scale = size / font::size;
        const float quadSize = font::cell * scale;
        for (const auto c : text) {
            const int slot = glyph(c, bold);
            if (c != ' ') {
                const Point p{x - 2 * scale, baseline - font::size * scale};
                const Point low{float(slot % font::columns * font::cell) / font::width,
                                float(slot / font::columns * font::cell) / font::height};
                const Point high = low + Point(float(font::cell)/font::width, float(font::cell)/font::height);
                quad(p, p + Point(quadSize,0), p + quadSize, p + Point(0,quadSize), color, low, high);
            }
            x += font::advances[slot] * scale / 32.f;
        }
    }

    void centered(float x, float baseline, std::string_view label, float size, Color color, bool bold = false) {
        text(x - width(label, size, bold) / 2, baseline, label, size, color, bold);
    }

    void key(float x, float y, float w, std::string_view label, float h = 22) {
        card(x, y, w, h, {.19f,.16f,.26f,.99f}, {.71f,.66f,.83f,.34f});
        centered(x+w/2,y+h*.70f,label,11,ivory,true);
    }

private:
    void quad(Point a, Point b, Point c, Point d, Color color, Point low = {-1,-1}, Point high = {-1,-1}) {
        const HudVertex va{a,low,color}, vb{b,{high.x,low.y},color},
                        vc{c,high,color}, vd{d,{low.x,high.y},color};
        vertices.insert(vertices.end(), {va,vb,vc,va,vc,vd});
    }
};

void branding(Canvas& canvas, float w) {
    canvas.card(28,25,197,62);
    canvas.rect(39,36,39,39,{.37f,.23f,.65f,1});
    canvas.centered(58.5f,64,"W",24,ivory,true);
    canvas.text(88,54,"Western Sim",20,ivory,true);
    canvas.text(89,72,"WESTERN UNIVERSITY",9.5f,lilac,true);

    canvas.card(w-269,25,241,39);
    canvas.key(w-260,33,25,"M");
    canvas.text(w-226,49,"Campus map",12,ivory,true);
    canvas.rect(w-141,34,1,20,border);
    canvas.key(w-132,33,33,"TAB");
    canvas.text(w-91,49,"Controls",12,ivory,true);
}

void compass(Canvas& canvas, float w, float heading) {
    const float center = w * .5f;
    canvas.card(center-105,25,210,40,{.09f,.08f,.14f,.75f});
    const int nearest = static_cast<int>(std::floor(heading / 15.f));
    constexpr std::string_view labels[]{"N","NE","E","SE","S","SW","W","NW"};
    for (int i = nearest - 3; i <= nearest + 4; ++i) {
        const float offset = (i * 15.f - heading) * 2.0f;
        if (std::abs(offset) > 91.f) continue;
        const float x = center + offset;
        const int mark = ((i % 24) + 24) % 24;
        if (mark % 3 == 0) canvas.centered(x,44,labels[mark/3],11,ivory,true);
        else canvas.rect(x,39,1,5,muted * Color(1,1,1,.64f));
        canvas.rect(x,48,1,4,border);
    }
    canvas.triangle({center-4,55},{center+4,55},{center,60},accent);
}

void help(Canvas& canvas, float w, float h) {
    canvas.rect(0,0,w,h,{.02f,.04f,.048f,.62f});
    const float x = w/2 - 226, y = h/2 - 137;
    canvas.card(x,y,452,274,{.105f,.086f,.16f,.98f});
    canvas.text(x+28,y+35,"WESTERN SIM",12,accent,true);
    canvas.text(x+28,y+73,"Field guide",30,ivory,true);
    canvas.key(x+379,y+25,44,"TAB");
    canvas.text(x+28,y+100,"Explore the campus and view buildings from outside.",13,muted);
    canvas.rect(x+28,y+121,396,1,border);
    struct Shortcut { std::string_view key, action; };
    constexpr Shortcut controls[]{
        {"ARROWS","Walk (or WASD)"}, {"MOUSE","Orbit camera"}, {"SPACE","Jump / swim up"}, {"SHIFT","Hold to sprint"},
#ifdef __EMSCRIPTEN__
        {"I","Diagnostics"},
#else
        {"F1","Diagnostics"},
#endif
        {"R","Return to UC Hill"}, {"ESC","Release / capture cursor"},
        {"M","Campus map"}, {"C","Street / overhead view"}, {"B","Spawn a box"}
    };
    for (std::size_t i = 0; i < std::size(controls); ++i) {
        const float col = x + 28 + 203 * (i % 2);
        const float row = y + 136 + 31 * (i / 2);
        canvas.key(col,row,76,controls[i].key);
        canvas.text(col+84,row+16,controls[i].action,11.5f,ivory);
    }
}

void landmarkSigns(Canvas& canvas, float w, float h, const World& world,
                   const Camera& camera, Point player) {
    struct Sign {
        Point point;
        float distance;
        std::string_view name;
    };
    constexpr float fadeStart = 38.f, range = 58.f;
    const auto viewProjection = camera.projection * camera.view;
    std::array<Sign,std::size(landmarks)> signs{};
    std::size_t count = 0;
    for (const auto& landmark : landmarks) {
        const float distance = glm::distance(player,glm::clamp(player,Point(landmark.low),Point(landmark.high)));
        if (distance >= range) continue;
        const auto roof = glm::vec3(landmark.roof) + glm::vec3(.5f,2.6f,.5f);
        const auto clip = viewProjection * glm::vec4(roof,1.f);
        if (clip.w <= 0 || std::abs(clip.x) > clip.w || clip.y < -clip.w ||
            std::abs(clip.z) > clip.w) continue;
        const auto viewRay = roof-camera.eye;
        const auto obstruction = raycast(world,camera.eye,viewRay,glm::length(viewRay));
        if (obstruction &&
            !landmarkContainsColumn(landmark,{obstruction->block.x,obstruction->block.z})) continue;
        const Point screen = (Point(clip)/clip.w * Point(.5f,-.5f) + .5f) * Point(w,h);
        signs[count++] = {screen,distance,landmark.name};
    }
    std::sort(signs.begin(),signs.begin()+count,[](const Sign& a, const Sign& b) {
        return a.distance < b.distance;
    });
    std::array<glm::vec4,std::size(landmarks)> placed{};
    std::size_t used = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& sign = signs[i];
        constexpr float boxHeight = 30.f, fontSize = 15.f;
        const float boxWidth = Canvas::width(sign.name,fontSize,true)+24.f;
        const float x = std::round(std::clamp(sign.point.x-boxWidth*.5f,16.f,w-boxWidth-16.f));
        const auto fits = [&](float y) {
            return y >= 76.f && y+boxHeight < h-16.f &&
                std::none_of(placed.begin(),placed.begin()+used,[&](glm::vec4 box) {
                    return x < box.z+6.f && x+boxWidth+6.f > box.x &&
                           y < box.w+11.f && y+boxHeight+11.f > box.y;
                });
        };
        constexpr float row = 42.f;
        float y = std::round(std::max(76.f,sign.point.y-boxHeight-7.f));
        if (!fits(y)) y += fits(y-row) ? -row : row;
        if (!fits(y)) continue;
        placed[used++] = {x,y,x+boxWidth,y+boxHeight};
        const float opacity = std::clamp((range-sign.distance)/(range-fadeStart),0.f,1.f);
        const Color fade{1.f,1.f,1.f,opacity};
        const float tip = std::clamp(sign.point.x,x+9.f,x+boxWidth-9.f);
        if (sign.point.y < y)
            canvas.triangle({tip-5,y+1},{tip+5,y+1},{tip,y-6},border*fade);
        else
            canvas.triangle({tip-5,y+boxHeight-1},{tip+5,y+boxHeight-1},{tip,y+boxHeight+6},border*fade);
        canvas.card(x,y,boxWidth,boxHeight,panel*fade,border*fade);
        canvas.centered(x+boxWidth*.5f,y+21.f,sign.name,fontSize,ivory*fade,true);
    }
}

void campusMap(Canvas& canvas, float w, float h, const HudState& state) {
    constexpr float panelWidth = 924, panelHeight = 676, mapWidth = 534;
    const float x = (w-panelWidth)/2, y = (h-panelHeight)/2;
    const float mapX = x+26, mapY = y+102;
    const auto point = [&](glm::vec2 p) {
        return Point(mapX,mapY) + p * (mapWidth/worldSize);
    };
    canvas.rect(0,0,w,h,{.058f,.043f,.083f,.72f});
    canvas.card(x,y,panelWidth,panelHeight,{.105f,.085f,.158f,.99f});
    canvas.text(x+26,y+33,"WESTERN UNIVERSITY  /  LONDON, ONTARIO",11,lilac,true);
    canvas.text(x+25,y+75,"Campus map",34,ivory,true);
    canvas.key(x+panelWidth-66,y+30,40,"M");

    canvas.rect(mapX-3,mapY-3,mapWidth+6,mapWidth+6,{.45f,.37f,.52f,1});
    canvas.mapImage({mapX,mapY},mapWidth);

    const Color purple{.32f,.22f,.49f,1}, dark{.22f,.23f,.30f,1};
    const float menuX = x+582;
    canvas.text(menuX,y+119,"LANDMARKS",13,lilac,true);
    std::vector<Point> placed;
    for (int i = 0; i < mapLandmarkCount; ++i) {
        const auto& l = landmarks[i];
        const Point anchor = point(glm::vec2(l.position)+.5f);
        Point c = anchor;
        for (;;) {
            const auto conflict = std::any_of(placed.begin(),placed.end(),[c](Point other) {
                return std::abs(other.x-c.x) < 16 && std::abs(other.y-c.y) < 15;
            });
            if (!conflict) break;
            c.y -= 15;
        }
        placed.push_back(c);
        if (c != anchor) {
            canvas.rect(c.x-1,c.y,2,anchor.y-c.y,lilac);
            canvas.rect(anchor.x-2,anchor.y-2,4,4,lilac);
        }
        const float row = y+144+24.f*i;
        const bool signature = i < 2;
        canvas.rect(c.x-8,c.y-7,16,14,{.17f,.13f,.25f,1});
        canvas.rect(c.x-7,c.y-6,14,12,signature ? purple : Color(.30f,.28f,.39f,1));
        char number[5];
        std::snprintf(number,sizeof(number),"%02d",i+1);
        canvas.centered(c.x,c.y+3.4f,number,8.5f,ivory,true);
        canvas.rect(menuX,row-8,25,19,{.24f,.18f,.34f,1});
        canvas.centered(menuX+12.5f,row+6,number,10,lilac,true);
        canvas.text(menuX+35,row+6,l.name,14.5f,ivory,signature);
        canvas.rect(menuX,row+14,x+panelWidth-26-menuX,1,{.62f,.53f,.72f,.21f});
    }
    const auto label = [&](Point p, std::string_view text) {
        canvas.rect(p.x-4,p.y-12,Canvas::width(text,11,true)+8,16,{.15f,.13f,.22f,.90f});
        canvas.text(p.x,p.y,text,11,ivory,true);
    };
    canvas.rect(mapX+9,mapY+10,25,34,{.15f,.13f,.22f,.90f});
    canvas.centered(mapX+21.5f,mapY+24,"N",13,ivory,true);
    canvas.rect(mapX+20,mapY+28,3,10,lilac);
    canvas.rect(mapX+17,mapY+31,9,3,lilac);
    for (const auto& feature : mapLabels) label(point(feature.position),feature.name);
    const Color you{.97f,.69f,.26f,1};
    const auto player = point(glm::clamp(state.position,Point(4),Point(worldSize-4)));
    canvas.rect(player.x-6,player.y-6,12,12,dark);
    canvas.rect(player.x-4,player.y-4,8,8,you);
    canvas.rect(menuX,y+621,13,13,you);
    canvas.text(menuX+21,y+632,"Your location",14,ivory,true);
    constexpr float scaleLength = 100.f * mapWidth / worldSize;
    const float scaleY = y+650;
    canvas.rect(mapX,scaleY,scaleLength,2,lilac);
    canvas.rect(mapX,scaleY-3,2,5,lilac);
    canvas.rect(mapX+scaleLength-2,scaleY-3,2,5,lilac);
    canvas.text(mapX+scaleLength+7,scaleY+3,"200 m",11,lilac);
    canvas.text(mapX+210,scaleY+3,"City of London / (c) OpenStreetMap contributors",10,muted);
}

using MapColor = glm::ivec3;

MapColor mapColor(Block block, int elevation, int x, int z) {
    const int grain = ((x+z)%2 ? 2 : -2) + ((x*7+z*17)%13 == 0 ? 8 : 0);
    switch (block) {
        case Block::water: return MapColor(79,144,158) + ((x+7*z)%19 < 3 ? 13 : 0);
        case Block::grass: return MapColor(174,188,130) - std::clamp(elevation-11,-4,12)*2 + grain;
        case Block::sand: return MapColor(202,190,146) + grain;
        case Block::leaves:
        case Block::needles: return MapColor(105,132,79) + grain;
        case Block::canopyGreen: return MapColor(130,157,91) + grain;
        case Block::goldLeaves:
        case Block::canopyGold: return MapColor(159,162,94) + grain;
        case Block::redLeaves:
        case Block::canopyCopper: return MapColor(158,144,87) + grain;
        case Block::pavement: return MapColor(154,150,145) + grain;
        case Block::limestone:
        case Block::concrete: return MapColor(224,213,181) + grain;
        case Block::slate: return MapColor(108,92,128) + grain;
        case Block::copper: return MapColor(83,126,121) + grain;
        case Block::glass: return MapColor(138,140,157) + grain;
        default: return MapColor(164,144,131) + grain;
    }
}

} // namespace

struct Hud::Data {
    GLuint program = shaderProgram(vertex, fragment);
    GLint size = glGetUniformLocation(program,"size");
    GLuint vao = 0, buffer = 0, texture = 0, campusTexture = 0;
    Canvas canvas;

    Data() {
        glGenVertexArrays(1,&vao);
        glGenBuffers(1,&buffer);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER,buffer);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(HudVertex),
                              reinterpret_cast<void*>(offsetof(HudVertex,point)));
        glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(HudVertex),
                              reinterpret_cast<void*>(offsetof(HudVertex,uv)));
        glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,sizeof(HudVertex),
                              reinterpret_cast<void*>(offsetof(HudVertex,color)));
        std::vector<std::uint8_t> pixels;
        pixels.reserve(font::width * font::height);
        for (std::size_t i = 0; i < std::size(font::pixels); i += 2)
            pixels.insert(pixels.end(),font::pixels[i],font::pixels[i+1]);
        glGenTextures(1,&texture);
        glBindTexture(GL_TEXTURE_2D,texture);
        glTexImage2D(GL_TEXTURE_2D,0,GL_R8,font::width,font::height,0,GL_RED,GL_UNSIGNED_BYTE,pixels.data());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glUseProgram(program);
        glUniform1i(glGetUniformLocation(program,"atlas"),0);
        glUniform1i(glGetUniformLocation(program,"campus"),1);
        glGenTextures(1,&campusTexture);
    }

    ~Data() {
        glDeleteTextures(1,&campusTexture);
        glDeleteTextures(1,&texture);
        glDeleteBuffers(1,&buffer);
        glDeleteVertexArrays(1,&vao);
        glDeleteProgram(program);
    }
};

Hud::Hud() : data_(std::make_unique<Data>()) {}
Hud::~Hud() = default;

void Hud::setCampusMap(const World& world) {
    const int edge = world.edge();
    struct Top { int y; Block block; };
    std::vector<Top> tops(static_cast<std::size_t>(edge*edge));
    std::vector<glm::ivec3> crowns;
    for (int z = 0; z < edge; ++z) {
        for (int x = 0; x < edge; ++x) {
            int y = worldHeight-1;
            while (y > 0) {
                const auto block = world.get({x,y,z});
                if (block != Block::air && (!scenery(block) || canopy(block))) break;
                --y;
            }
            const auto block = world.get({x,y,z});
            tops[z*edge+x] = {y,block};
            if (canopy(block)) crowns.push_back({x,y,z});
        }
    }
    for (const auto& crown : crowns) {
        const auto block = world.get(crown);
        for (int dz = -2; dz <= 2; ++dz) {
            for (int dx = -2; dx <= 2; ++dx) {
                const int x = crown.x+dx, z = crown.z+dz;
                if (x < 0 || z < 0 || x >= edge || z >= edge || dx*dx+dz*dz > 2) continue;
                auto& top = tops[z*edge+x];
                if (top.y < crown.y) top = {crown.y,block};
            }
        }
    }
    std::vector<std::uint8_t> pixels;
    pixels.reserve(edge*edge*4);
    for (int z = 0; z < edge; ++z) {
        for (int x = 0; x < edge; ++x) {
            const auto top = tops[z*edge+x];
            const auto color = mapColor(top.block,top.y,x,z);
            pixels.insert(pixels.end(),{static_cast<std::uint8_t>(color.r),
                                        static_cast<std::uint8_t>(color.g),
                                        static_cast<std::uint8_t>(color.b),255});
        }
    }
    glBindTexture(GL_TEXTURE_2D,data_->campusTexture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,edge,edge,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
}

void Hud::draw(int width, int height, const World& world, const Camera& camera, const HudState& state) {
    auto& d = *data_;
    auto& canvas = d.canvas;
    canvas.vertices.clear();
    const float scale = std::min(width / 1050.f, height / 840.f);
    const float w = width / scale, h = height / scale;
    if (!state.map && !state.guide) landmarkSigns(canvas,w,h,world,camera,state.position);
    branding(canvas,w);
    compass(canvas,w,state.heading);
    if (!state.toast.empty()) {
        const float toastWidth = Canvas::width(state.toast,13,true) + 36;
        canvas.card((w-toastWidth)/2,h-172,toastWidth,34);
        canvas.centered(w/2,h-150,state.toast,13,accent,true);
    }
    if (state.diagnostics) {
        char line[128];
        canvas.card(w-248,77,220,90);
        std::snprintf(line,sizeof(line),"%.0f FPS   |   %s", state.fps,
                      state.meshing == Meshing::greedy ? "Greedy mesh" : "Exposed faces");
        canvas.text(w-233,101,line,13,accent,true);
        std::snprintf(line,sizeof(line),"Chunks in view     %zu / %zu",state.stats.chunks,state.stats.totalChunks);
        canvas.text(w-233,125,line,11.5f,ivory);
        std::snprintf(line,sizeof(line),"Triangles in view   %zu / %zu",state.stats.triangles,state.stats.totalTriangles);
        canvas.text(w-233,148,line,11.5f,ivory);
    }
    if (state.guide) help(canvas,w,h);
    if (state.map) campusMap(canvas,w,h,state);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(d.program);
    glUniform2f(d.size,w,h);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D,d.campusTexture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D,d.texture);
    glBindVertexArray(d.vao);
    glBindBuffer(GL_ARRAY_BUFFER,d.buffer);
    glBufferData(GL_ARRAY_BUFFER,canvas.vertices.size()*sizeof(HudVertex),canvas.vertices.data(),GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(canvas.vertices.size()));
    glDisable(GL_BLEND);
}

} // namespace wsim
