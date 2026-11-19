#include "hud.hpp"
#include "campus.hpp"
#include "font_atlas.hpp"

#include <glad/gl.h>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace ql {
namespace {

using Color = glm::vec4;
using Point = glm::vec2;
constexpr Color ivory{.98f,.95f,.86f,1.f};
constexpr Color muted{.72f,.79f,.76f,1.f};
constexpr Color accent{.99f,.80f,.45f,1.f};
constexpr Color panel{.093f,.082f,.143f,.92f};
constexpr Color border{.81f,.78f,.93f,.25f};
constexpr Color lilac{.80f,.72f,.97f,1.f};

constexpr char vertex[] = R"GLSL(#version 330 core
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
constexpr char fragment[] = R"GLSL(#version 330 core
in vec2 atlasUV;
in vec4 rgba;
out vec4 color;
uniform sampler2D atlas;
void main() {
    float alpha = atlasUV.x < 0. ? 1. : texture(atlas, atlasUV).r;
    color = vec4(rgba.rgb, rgba.a * alpha);
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

    void circle(Point p, float radius, Color color) {
        constexpr float turn = 6.2831853f;
        for (int i = 0; i < 18; ++i) {
            const float a = turn * i / 18, b = turn * (i+1) / 18;
            triangle(p,p + radius*Point(std::cos(a),std::sin(a)),
                     p + radius*Point(std::cos(b),std::sin(b)),color);
        }
    }

    void stroke(Point a, Point b, float thickness, Color color) {
        const auto d = glm::normalize(b-a);
        const Point n = Point(-d.y,d.x) * (thickness/2);
        quad(a+n,b+n,b-n,a-n,color);
        circle(a,thickness/2,color);
        circle(b,thickness/2,color);
    }

    void rounded(float x, float y, float w, float h, float r, Color color) {
        rect(x+r,y,w-2*r,h,color);
        rect(x,y+r,r,h-2*r,color);
        rect(x+w-r,y+r,r,h-2*r,color);
        constexpr float halfPi = 1.570796327f;
        const Point centers[]{{x+w-r,y+h-r},{x+r,y+h-r},{x+r,y+r},{x+w-r,y+r}};
        for (int k = 0; k < 4; ++k) {
            const auto c = centers[k];
            for (int j = 0; j < 5; ++j) {
                const float a = (k + j / 5.f) * halfPi;
                const float b = (k + (j+1) / 5.f) * halfPi;
                triangle(c, c + r * Point(std::cos(a), std::sin(a)),
                         c + r * Point(std::cos(b), std::sin(b)), color);
            }
        }
    }

    void card(float x, float y, float w, float h, float r, Color fill = panel, Color edge = border) {
        rounded(x,y,w,h,r,edge);
        rounded(x+1,y+1,w-2,h-2,r-1,fill);
    }

    static int glyph(unsigned char c, bool bold) {
        return (c >= 32 && c <= 126 ? c - 32 : '?' - 32) + (bold ? 96 : 0);
    }

    static float width(std::string_view text, float size, bool bold) {
        float sum = 0;
        for (const auto c : text) sum += font::advances[glyph(c,bold)];
        return sum * size / (24.f * 32.f);
    }

    void text(float x, float baseline, std::string_view text, float size, Color color, bool bold = false) {
        const float scale = size / 24.f;
        const float quadSize = font::cell * scale;
        for (const auto c : text) {
            const int slot = glyph(c, bold);
            if (c != ' ') {
                const Point p{x - 2 * scale, baseline - 24 * scale};
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
        card(x, y, w, h, 5, {.19f,.16f,.26f,.99f}, {.71f,.66f,.83f,.34f});
        centered(x+w/2,y+h*.70f,label,11,ivory,true);
    }

    void cube(float x, float y, float scale, glm::vec3 color) {
        const Point top{x,y-12*scale}, left{x-15*scale,y-4*scale},
                    center{x,y+4*scale}, right{x+15*scale,y-4*scale};
        const auto shift = Point(0,15*scale);
        quad(top,right,center,left,Color(glm::min(color*1.15f,glm::vec3(1)),1));
        quad(left,center,center+shift,left+shift,Color(color*.81f,1));
        quad(center,right,right+shift,center+shift,Color(color*.58f,1));
    }

private:
    void quad(Point a, Point b, Point c, Point d, Color color, Point low = {-1,-1}, Point high = {-1,-1}) {
        const HudVertex va{a,low,color}, vb{b,{high.x,low.y},color},
                        vc{c,high,color}, vd{d,{low.x,high.y},color};
        vertices.insert(vertices.end(), {va,vb,vc,va,vc,vd});
    }
};

void branding(Canvas& canvas, float w) {
    canvas.card(28,25,197,62,12);
    canvas.rounded(39,36,39,39,8,{.24f,.19f,.35f,1});
    canvas.cube(58.5f,53.5f,.82f,{.96f,.72f,.35f});
    canvas.text(88,54,"Quarrylight",20,ivory,true);
    canvas.text(89,72,"WESTERN UNIVERSITY",9.5f,lilac,true);

    canvas.card(w-269,25,241,39,9);
    canvas.key(w-260,33,25,"M");
    canvas.text(w-226,49,"Campus map",12,ivory,true);
    canvas.rect(w-141,34,1,20,border);
    canvas.key(w-132,33,33,"TAB");
    canvas.text(w-91,49,"Controls",12,ivory,true);
}

void compass(Canvas& canvas, float w, float heading) {
    const float center = w * .5f;
    canvas.card(center-105,25,210,40,9,{.09f,.08f,.14f,.75f});
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
    const float x = w/2 - 226, y = h/2 - 194;
    canvas.card(x,y,452,388,16,{.105f,.086f,.16f,.98f});
    canvas.text(x+28,y+35,"QUARRYLIGHT",10.5f,accent,true);
    canvas.text(x+28,y+73,"Field guide",30,ivory,true);
    canvas.key(x+379,y+25,44,"TAB");
    canvas.text(x+28,y+100,"Walk around Western, then build with the material bar.",13,muted);
    canvas.rect(x+28,y+121,396,1,border);
    struct Shortcut { std::string_view key, action; };
    constexpr Shortcut controls[]{
        {"WASD","Walk"}, {"MOUSE","Orbit camera"}, {"SPACE","Jump / swim up"}, {"SHIFT","Sprint"},
        {"LMB","Remove block"}, {"RMB","Place block"}, {"1-8 / WHEEL","Select material"}, {"E","Toggle building"},
        {"F5","Save campus"}, {"F9","Load campus"}, {"F1","Diagnostics"}, {"F2","Compare mesh modes"},
        {"R","Return to UC Hill"}, {"ESC","Release / capture cursor"},
        {"M","Campus map"}
    };
    for (std::size_t i = 0; i < std::size(controls); ++i) {
        const float col = x + 28 + 203 * (i % 2);
        const float row = y + 136 + 31 * (i / 2);
        canvas.key(col,row,76,controls[i].key);
        canvas.text(col+84,row+16,controls[i].action,11.5f,ivory);
    }
}

void campusMap(Canvas& canvas, float w, float h, const HudState& state) {
    constexpr float panelWidth = 886, panelHeight = 676, mapWidth = 534;
    const float x = (w-panelWidth)/2, y = (h-panelHeight)/2;
    const float mapX = x+26, mapY = y+102;
    const auto point = [&](glm::vec2 p) {
        return Point(mapX,mapY) + p * (mapWidth/worldSize);
    };
    canvas.rect(0,0,w,h,{.058f,.043f,.083f,.72f});
    canvas.card(x,y,panelWidth,panelHeight,17,{.105f,.085f,.158f,.99f});
    canvas.text(x+26,y+33,"WESTERN UNIVERSITY  /  LONDON, ONTARIO",11,lilac,true);
    canvas.text(x+25,y+75,"Campus map",34,ivory,true);
    canvas.key(x+panelWidth-66,y+30,40,"M");

    canvas.rounded(mapX-2,mapY-2,mapWidth+4,mapWidth+4,9,{.47f,.51f,.44f,1});
    canvas.rect(mapX,mapY,mapWidth,mapWidth,{.87f,.88f,.77f,1});
    for (int i = 24; i < worldSize; i += 24) {
        const auto p = point({i,i});
        const Color grid{.46f,.55f,.44f,.19f};
        canvas.rect(p.x,mapY,1,mapWidth,grid);
        canvas.rect(mapX,p.y,mapWidth,1,grid);
    }
    for (float radius : {20.f,28.f,36.f}) {
        for (int i = 0; i < 28; ++i) {
            const float angle = i*6.2831853f/28, next = (i+1)*6.2831853f/28;
            const auto at = [radius](float a) { return Point(78,87)+Point(std::cos(a)*1.18f,std::sin(a))*radius; };
            canvas.stroke(point(at(angle)),point(at(next)),1,{.42f,.50f,.33f,.19f});
        }
    }

    const auto riverVertices = canvas.vertices.size();
    for (std::size_t i = 1; i < river.size(); ++i)
        canvas.stroke(point(river[i-1]),point(river[i]),riverWidth * mapWidth/worldSize+4,{.50f,.68f,.65f,1});
    for (std::size_t i = 1; i < river.size(); ++i)
        canvas.stroke(point(river[i-1]),point(river[i]),riverWidth * mapWidth/worldSize,{.40f,.64f,.69f,1});
    for (std::size_t i = riverVertices; i < canvas.vertices.size(); ++i)
        canvas.vertices[i].point = glm::clamp(canvas.vertices[i].point,point({0,0}),point({worldSize,worldSize}));
    const auto road = [&](MapLine path, Color color) {
        auto a = glm::clamp(path.start,Point(3),Point(worldSize-4));
        auto b = glm::clamp(path.end,Point(3),Point(worldSize-4));
        canvas.stroke(point(a),point(b),path.width * mapWidth/worldSize,color);
    };
    for (const auto path : campusPaths)
        road(path,path.surface == Block::pavement ? Color(.63f,.64f,.59f,1) : Color(.97f,.95f,.83f,1));
    for (const auto path : hillPaths) road(path,{.97f,.95f,.83f,1});
    road(bridge,{.55f,.49f,.49f,1});

    const Color purple{.32f,.22f,.49f,1}, dark{.22f,.23f,.30f,1};
    const float menuX = x+582;
    canvas.text(menuX,y+119,"LANDMARKS",11,lilac,true);
    for (std::size_t i = 0; i < landmarks.size(); ++i) {
        const auto& l = landmarks[i];
        const auto a = point(l.low), b = point(glm::vec2(l.high)+1.f), c = (a+b)*.5f;
        const float row = y+144+24.f*i;
        const bool signature = i < 2;
        const Color footprint = signature ? Color(.64f,.53f,.73f,1) : Color(.67f,.68f,.59f,1);
        canvas.rounded(a.x,a.y,b.x-a.x,b.y-a.y,3,footprint);
        canvas.circle(c,11.f,purple);
        char number[5];
        std::snprintf(number,sizeof(number),"%02zu",i+1);
        canvas.centered(c.x,c.y+3.4f,number,10,ivory,true);
        canvas.rounded(menuX,row-8,25,19,5,{.24f,.18f,.34f,1});
        canvas.centered(menuX+12.5f,row+6,number,10,lilac,true);
        canvas.text(menuX+35,row+6,l.name,12.5f,ivory,signature);
        canvas.rect(menuX,row+14,277,1,{.62f,.53f,.72f,.21f});
    }
    canvas.text(mapX+24,mapY+32,"N",13,dark,true);
    canvas.triangle({mapX+22,mapY+54},{mapX+36,mapY+54},{mapX+29,mapY+38},purple);
    canvas.text(mapX+46,mapY+15,"WESTERN ROAD",9,dark,true);
    canvas.text(mapX+284,mapY+459,"THAMES RIVER",10,{.22f,.41f,.47f,1},true);
    const auto crossing = point({137,126});
    canvas.text(crossing.x+6,crossing.y+8,"UNIVERSITY BRIDGE",9,dark,true);
    const auto uc = point({72,100});
    canvas.text(uc.x,uc.y,"UC HILL",10,dark,true);
    const Color you{.97f,.69f,.26f,1};
    const auto player = point(glm::clamp(state.position,Point(4),Point(worldSize-4)));
    canvas.circle(player,9,dark);
    canvas.circle(player,6.5f,you);
    canvas.circle({menuX+7,y+582},6.5f,you);
    canvas.text(menuX+21,y+587,"Your location",12,ivory,true);
    canvas.text(menuX,y+614,"Schematic map of Western's campus core",11,muted);
    canvas.text(mapX,y+658,"WESTERN ROAD TO THE THAMES RIVER",9,lilac,true);
}

void toolbar(Canvas& canvas, float w, float h, const HudState& state) {
    constexpr float gap = 7, tile = 54, width = tile * 8 + gap * 7 + 18;
    const float x = (w - width) / 2, y = h - 93;
    const float labelWidth = Canvas::width(buildingPalette[state.slot].label, 12, true) + 75;
    canvas.card((w-labelWidth)/2,y-36,labelWidth,27,7);
    canvas.text((w-labelWidth)/2+11,y-18,buildingPalette[state.slot].label,12,ivory,true);
    canvas.text((w+labelWidth)/2-55,y-18,"MATERIAL",9,muted,true);
    canvas.card(x,y,width,68,12);
    for (std::size_t i = 0; i < buildingPalette.size(); ++i) {
        const float tx = x + 9 + (tile + gap) * i;
        const bool active = static_cast<int>(i) == state.slot;
        canvas.card(tx,y+7,tile,tile,8,active ? Color(.31f,.25f,.30f,1) : Color(.17f,.14f,.24f,1),
                    active ? accent : border);
        canvas.cube(tx+tile/2,y+30,.84f,blockColor(buildingPalette[i].block));
        const char digit = static_cast<char>('1' + i);
        canvas.text(tx+5,y+19,std::string_view(&digit,1),10,active ? accent : muted,true);
    }
}

} // namespace

struct Hud::Data {
    GLuint program = shaderProgram(vertex, fragment);
    GLint size = glGetUniformLocation(program,"size");
    GLuint vao = 0, buffer = 0, texture = 0;
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
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glUseProgram(program);
        glUniform1i(glGetUniformLocation(program,"atlas"),0);
    }

    ~Data() {
        glDeleteTextures(1,&texture);
        glDeleteBuffers(1,&buffer);
        glDeleteVertexArrays(1,&vao);
        glDeleteProgram(program);
    }
};

Hud::Hud() : data_(std::make_unique<Data>()) {}
Hud::~Hud() = default;

void Hud::draw(int width, int height, const HudState& state) {
    auto& d = *data_;
    auto& canvas = d.canvas;
    canvas.vertices.clear();
    const float scale = std::min(width / 1050.f, height / 840.f);
    const float w = width / scale, h = height / scale;
    branding(canvas,w);
    compass(canvas,w,state.heading);
    toolbar(canvas,w,h,state);
    if (!state.guide && !state.map) {
        const Color shadow{.03f,.08f,.09f,.52f};
        canvas.rect(w/2-7,h/2-1.5f,14,3,shadow);
        canvas.rect(w/2-1.5f,h/2-7,3,14,shadow);
        canvas.rect(w/2-6,h/2-.5f,12,1,ivory);
        canvas.rect(w/2-.5f,h/2-6,1,12,ivory);
    }
    canvas.card(28,h-65,state.editing ? 203 : 110,40,9);
    canvas.key(39,h-56,22,"E");
    canvas.text(71,h-40,state.editing ? "Build" : "Explore",12,ivory,true);
    if (state.editing) {
        canvas.rect(112,h-55,1,20,border);
        canvas.text(123,h-40,"LMB  -    RMB  +",11.5f,muted);
    }
    if (!state.toast.empty()) {
        const float toastWidth = Canvas::width(state.toast,13,true) + 36;
        canvas.card((w-toastWidth)/2,h-172,toastWidth,34,8);
        canvas.centered(w/2,h-150,state.toast,13,accent,true);
    }
    if (state.diagnostics) {
        char line[128];
        canvas.card(w-248,77,220,90,10);
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
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D,d.texture);
    glBindVertexArray(d.vao);
    glBindBuffer(GL_ARRAY_BUFFER,d.buffer);
    glBufferData(GL_ARRAY_BUFFER,canvas.vertices.size()*sizeof(HudVertex),canvas.vertices.data(),GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(canvas.vertices.size()));
    glDisable(GL_BLEND);
}

} // namespace ql
