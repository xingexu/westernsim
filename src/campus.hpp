#pragma once

#include "world.hpp"

#include <glm/vec2.hpp>
#include <array>
#include <string_view>

namespace wsim {

enum class Building {
    university, middlesex, library, community, science, gothic, modern, residence, ontario, gallery, plaza
};

struct Landmark {
    std::string_view name;
    glm::ivec2 low, high;
    int height;
    Building type;
};

inline constexpr std::array<Landmark, 20> landmarks{{
    {"University College", {56,73}, {95,89}, 12, Building::university},
    {"Middlesex College", {109,70}, {131,86}, 11, Building::middlesex},
    {"Weldon Library", {33,78}, {48,94}, 14, Building::library},
    {"University Community Centre", {28,55}, {49,69}, 11, Building::community},
    {"Social Science Centre", {51,49}, {66,61}, 17, Building::modern},
    {"Natural Sciences Centre", {77,33}, {100,48}, 10, Building::science},
    {"Physics & Astronomy", {83,54}, {104,66}, 11, Building::gothic},
    {"Western Science Centre", {109,52}, {128,64}, 10, Building::science},
    {"Thames Hall", {57,101}, {75,112}, 10, Building::modern},
    {"Delaware Hall", {129,90}, {143,104}, 13, Building::residence},
    {"Alumni Hall", {35,116}, {52,132}, 11, Building::modern},
    {"Elgin Hall", {166,98}, {182,110}, 11, Building::residence},
    {"Medway Hall", {165,127}, {184,140}, 9, Building::gothic},
    {"Saugeen-Maitland Hall", {26,21}, {46,34}, 17, Building::residence},
    {"Huron University College", {2,66}, {12,81}, 9, Building::gothic},
    {"Ivey Business School", {2,114}, {12,133}, 12, Building::modern},
    {"Western Student Recreation Centre", {30,144}, {49,160}, 10, Building::modern},
    {"Ontario Hall", {1,155}, {11,175}, 17, Building::ontario},
    {"McIntosh Gallery", {68,51}, {80,61}, 7, Building::gallery},
    {"Concrete Beach", {29,70}, {50,76}, 0, Building::plaza}
}};

struct MapLine { glm::vec2 start, end; float width; Block surface; };

inline constexpr std::array<glm::vec2, 12> river{{
    {184,-8},{169,17},{155,32},{150,51},{155,73},{158,94},
    {151,115},{133,135},{109,151},{93,166},{85,184},{90,199}
}};
inline constexpr float riverWidth = 12.f;
inline constexpr MapLine bridge{{134,107},{174,125},7.f,Block::pavement};
inline constexpr std::array<MapLine, 24> campusPaths{{
    {{15,0},{18,69},6,Block::pavement}, {{18,69},{16,140},6,Block::pavement},
    {{16,140},{8,191},6,Block::pavement}, {{16,106},{58,117},6,Block::pavement},
    {{58,117},{99,117},6,Block::pavement}, {{99,117},{116,123},6,Block::pavement},
    {{116,123},{134,107},6,Block::pavement}, {{174,125},{192,124},7,Block::pavement},
    {{134,107},{135,76},5,Block::pavement}, {{135,76},{126,43},5,Block::pavement},
    {{126,43},{121,10},5,Block::pavement}, {{19,52},{54,44},5,Block::pavement},
    {{54,44},{126,43},5,Block::pavement}, {{47,101},{54,70},4,Block::concrete},
    {{54,70},{106,69},4,Block::concrete}, {{106,69},{111,100},4,Block::concrete},
    {{111,100},{134,107},4,Block::concrete},
    {{12,82},{17,82},4,Block::concrete}, {{12,134},{16,134},4,Block::concrete},
    {{14,160},{40,164},4,Block::concrete}, {{35,34},{35,49},4,Block::concrete},
    {{50,73},{67,66},3,Block::limestone}, {{67,66},{74,62},3,Block::limestone},
    {{126,114},{135,130},2,Block::limestone}
}};
inline constexpr std::array<MapLine, 4> hillPaths{{
    {{107,132},{98,118},5,Block::limestone},
    {{98,118},{83,110},5,Block::limestone},
    {{83,110},{76,98},5,Block::limestone},
    {{76,98},{76,91},7,Block::limestone}
}};
inline constexpr glm::ivec2 campusSpawn{104,129};

World makeCampus();
glm::vec3 campusClockCenter();

} // namespace wsim
