#include "renderer.hpp"
#include "shaders.hpp"

#include <glad/gl.h>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace ql {
namespace {

constexpr int shadowSize = 1536;
const glm::vec3 sun = glm::normalize(glm::vec3(.53f,.69f,.49f));
constexpr glm::vec3 rgb(unsigned hex) {
    return {((hex >> 16) & 255) / 255.f, ((hex >> 8) & 255) / 255.f, (hex & 255) / 255.f};
}
const std::array<glm::vec3,37> colors{
    rgb(0x000000), rgb(0x809d4c), rgb(0x91704f), rgb(0x75858a), rgb(0xdac68b),
    rgb(0x856045), rgb(0x496e47), rgb(0xddcbae), rgb(0xa5b655), rgb(0xe5d6b2),
    rgb(0xad6252), rgb(0xab8055), rgb(0x45998e), rgb(0xffd080),
    rgb(0xbca985), rgb(0x45565e), rgb(0x3e525f), rgb(0x777c7e), rgb(0xb2b1a3),
    rgb(0x4b8982), rgb(0xde9c45), rgb(0xba6042), rgb(0x665584),
    {}, {}, {}, {}, {}, {}, {},
    rgb(0xf3bd58), rgb(0x487a76), rgb(0x324e56), rgb(0xb97958), rgb(0x57433e),
    rgb(0xc56b4b), rgb(0xf3e9cb)
};

GLuint compile(GLenum type, const char* source) {
    const auto shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string log(std::max(length, 1), '\0');
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        glDeleteShader(shader);
        throw std::runtime_error("Shader compile failed: " + log);
    }
    return shader;
}

struct Program {
    GLuint id;
    Program(const char* vertex, const char* fragment) : id(shaderProgram(vertex, fragment)) {}
    ~Program() { glDeleteProgram(id); }
    GLint uniform(const char* name) const { return glGetUniformLocation(id, name); }
    void use() const { glUseProgram(id); }
};

void matrix(GLint location, const glm::mat4& m) {
    glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(m));
}

void vector(GLint location, glm::vec3 v) { glUniform3fv(location, 1, glm::value_ptr(v)); }

struct GpuMesh {
    GLuint vao = 0, vertices = 0, indices = 0;
    GLsizei count = 0;
    glm::vec3 origin{};

    GpuMesh() = default;
    GpuMesh(const GpuMesh&) = delete;
    GpuMesh& operator=(const GpuMesh&) = delete;
    GpuMesh(GpuMesh&& other) noexcept { *this = std::move(other); }
    GpuMesh& operator=(GpuMesh&& other) noexcept {
        std::swap(vao, other.vao);
        std::swap(vertices, other.vertices);
        std::swap(indices, other.indices);
        std::swap(count, other.count);
        std::swap(origin, other.origin);
        return *this;
    }
    ~GpuMesh() {
        glDeleteBuffers(1, &indices);
        glDeleteBuffers(1, &vertices);
        glDeleteVertexArrays(1, &vao);
    }
    void upload(const Mesh& mesh) {
        count = static_cast<GLsizei>(mesh.indices.size());
        if (count == 0) return;
        if (!vao) {
            glGenVertexArrays(1, &vao);
            glGenBuffers(1, &vertices);
            glGenBuffers(1, &indices);
        }
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vertices);
        glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(Vertex), mesh.vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(std::uint32_t),
                     mesh.indices.data(), GL_STATIC_DRAW);
        for (GLuint slot = 0; slot < 3; ++slot) glEnableVertexAttribArray(slot);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<void*>(offsetof(Vertex, position)));
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<void*>(offsetof(Vertex, normal)));
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<void*>(offsetof(Vertex, material)));
    }
    void draw() const {
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
    }
};

struct Frustum {
    std::array<glm::vec4,6> planes;
    explicit Frustum(const glm::mat4& transform) {
        const auto rows = glm::transpose(transform);
        for (int axis = 0; axis < 3; ++axis) {
            planes[2 * axis] = rows[3] + rows[axis];
            planes[2 * axis + 1] = rows[3] - rows[axis];
        }
    }
    bool intersects(glm::vec3 low, glm::vec3 high) const {
        for (const auto& plane : planes) {
            const glm::vec3 furthest{
                plane.x >= 0 ? high.x : low.x,
                plane.y >= 0 ? high.y : low.y,
                plane.z >= 0 ? high.z : low.z
            };
            if (glm::dot(glm::vec3(plane), furthest) + plane.w < 0) return false;
        }
        return true;
    }
};

struct Piece {
    glm::mat4 model;
    float material;
};

std::array<Piece,15> avatar(const Player& player) {
    const glm::mat4 identity{1.f};
    const float swing = std::sin(player.stride) * player.gait * .55f;
    const auto root = glm::rotate(glm::translate(identity, player.feet), player.heading, {0,1,0});
    auto box = [&](float mat, glm::vec3 center, glm::vec3 size) {
        return Piece{glm::scale(glm::translate(root, center - size * .5f), size), mat};
    };
    auto limb = [&](float mat, glm::vec3 pivot, glm::vec3 size, float angle, float drop) {
        auto model = glm::rotate(glm::translate(root, pivot), angle, {1,0,0});
        return Piece{glm::scale(glm::translate(model, {-size.x/2, -drop, -size.z/2}), size), mat};
    };
    return {{
        box(31, {0,.84f,0}, {.49f,.54f,.30f}),
        box(33, {0,1.29f,0}, {.35f,.36f,.33f}),
        box(34, {0,1.34f,.141f}, {.35f,.27f,.063f}),
        box(30, {0,1.48f,-.025f}, {.40f,.115f,.42f}),
        box(30, {0,1.412f,-.061f}, {.46f,.044f,.53f}),
        box(35, {0,1.106f,.006f}, {.39f,.08f,.35f}),
        box(34, {0,.87f,.188f}, {.37f,.39f,.154f}),
        box(35, {0,.944f,.277f}, {.33f,.145f,.039f}),
        box(36, {0,.931f,.302f}, {.082f,.081f,.014f}),
        limb(31, {-.33f,1.055f,0}, {.16f,.39f,.18f}, -swing, .37f),
        limb(31, { .33f,1.055f,0}, {.16f,.39f,.18f}, swing, .37f),
        limb(33, {-.33f,1.055f,0}, {.146f,.11f,.165f}, -swing, .456f),
        limb(33, { .33f,1.055f,0}, {.146f,.11f,.165f}, swing, .456f),
        limb(32, {-.13f,.57f,-.025f}, {.193f,.57f,.235f}, swing, .57f),
        limb(32, { .13f,.57f,-.025f}, {.193f,.57f,.235f}, -swing, .57f)
    }};
}

} // namespace

unsigned shaderProgram(const char* vertex, const char* fragment) {
    const GLuint vs = compile(GL_VERTEX_SHADER, vertex);
    GLuint fs;
    try {
        fs = compile(GL_FRAGMENT_SHADER, fragment);
    } catch (...) {
        glDeleteShader(vs);
        throw;
    }
    const auto program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string log(std::max(length, 1), '\0');
        glGetProgramInfoLog(program, length, nullptr, log.data());
        glDeleteProgram(program);
        throw std::runtime_error("Shader link failed: " + log);
    }
    return program;
}

glm::vec3 blockColor(Block block) { return colors[static_cast<std::size_t>(block)]; }

struct Renderer::Data {
    Program world{shaders::worldVertex, shaders::worldFragment};
    Program sky{shaders::skyVertex, shaders::skyFragment};
    Program depth{shaders::depthVertex, shaders::depthFragment};
    Program line{shaders::lineVertex, shaders::lineFragment};
    const GLint worldView = world.uniform("viewProjection"), worldModel = world.uniform("model");
    const GLint worldLight = world.uniform("lightMatrix"), worldEye = world.uniform("eye");
    const GLint worldTile = world.uniform("materialOverride"), worldTime = world.uniform("time");
    const GLint skyInverse = sky.uniform("inverseViewProjection"), skyEye = sky.uniform("eye");
    const GLint skyTime = sky.uniform("time"), depthModel = depth.uniform("model");
    const GLint depthLight = depth.uniform("lightMatrix"), lineMatrix = line.uniform("transform");
    GLuint emptyVao = 0, lineVao = 0, lineBuffer = 0;
    GLuint shadowBuffer = 0, shadowMap = 0;
    std::vector<GpuMesh> chunks;
    GpuMesh cube;
    Meshing mode = Meshing::greedy;
    glm::mat4 lightMatrix{1.f};
    std::vector<std::size_t> shadowChunks;
    RenderStats totals;

    Data() {
        World sample(chunkSize);
        sample.set({0,0,0}, Block::limestone);
        cube.upload(meshChunk(sample, sample.chunks().front()));
        world.use();
        vector(world.uniform("lightDirection"), sun);
        glUniform3fv(world.uniform("palette[0]"), static_cast<GLsizei>(colors.size()),
                     glm::value_ptr(colors.front()));
        glUniform1i(world.uniform("shadowMap"), 0);
        sky.use();
        vector(sky.uniform("lightDirection"), sun);

        glGenVertexArrays(1, &emptyVao);
        glGenVertexArrays(1, &lineVao);
        glGenBuffers(1, &lineBuffer);
        glBindVertexArray(lineVao);
        glBindBuffer(GL_ARRAY_BUFFER, lineBuffer);
        constexpr std::array<float,72> edges{
            0,0,0, 1,0,0, 1,0,0, 1,0,1, 1,0,1, 0,0,1, 0,0,1, 0,0,0,
            0,1,0, 1,1,0, 1,1,0, 1,1,1, 1,1,1, 0,1,1, 0,1,1, 0,1,0,
            0,0,0, 0,1,0, 1,0,0, 1,1,0, 1,0,1, 1,1,1, 0,0,1, 0,1,1
        };
        glBufferData(GL_ARRAY_BUFFER, sizeof(edges), edges.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);

        glGenTextures(1, &shadowMap);
        glBindTexture(GL_TEXTURE_2D, shadowMap);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, shadowSize, shadowSize, 0,
                     GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        constexpr float border[]{1,1,1,1};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
        glGenFramebuffers(1, &shadowBuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, shadowBuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowMap, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Could not create the shadow framebuffer");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    ~Data() {
        glDeleteFramebuffers(1, &shadowBuffer);
        glDeleteTextures(1, &shadowMap);
        glDeleteVertexArrays(1, &emptyVao);
        glDeleteVertexArrays(1, &lineVao);
        glDeleteBuffers(1, &lineBuffer);
    }

    void recount() {
        totals = {};
        shadowChunks.clear();
        const Frustum light{lightMatrix};
        for (std::size_t i = 0; i < chunks.size(); ++i) {
            const auto& chunk = chunks[i];
            if (!chunk.count) continue;
            ++totals.totalChunks;
            totals.totalTriangles += chunk.count / 3;
            if (light.intersects(chunk.origin, chunk.origin + glm::vec3(chunkSize)))
                shadowChunks.push_back(i);
        }
    }
};

Renderer::Renderer() : data_(std::make_unique<Data>()) {}
Renderer::~Renderer() = default;

void Renderer::rebuild(const World& world, Meshing mode) {
    auto& d = *data_;
    d.mode = mode;
    const float range = world.edge() * .69f;
    const glm::vec3 middle{world.edge() * .5f, 15.f, world.edge() * .5f};
    d.lightMatrix = glm::ortho(-range, range, -range, range, 1.f, 390.f) *
                    glm::lookAt(middle + sun * 170.f, middle, {0,1,0});
    d.chunks.resize(world.chunks().size());
    for (std::size_t i = 0; i < d.chunks.size(); ++i) {
        const auto& chunk = world.chunks()[i];
        d.chunks[i].origin = chunk.origin;
        d.chunks[i].upload(meshChunk(world, chunk, mode));
    }
    d.recount();
}

void Renderer::update(const World& world, glm::ivec3 edited) {
    auto& d = *data_;
    for (const auto i : world.affectedChunks(edited))
        d.chunks[i].upload(meshChunk(world, world.chunks()[i], d.mode));
    d.recount();
}

RenderStats Renderer::draw(const Camera& camera, const Player& player, const std::optional<Hit>& selected,
                           int width, int height, float time) {
    auto& d = *data_;
    const glm::mat4 identity{1.f};
    const auto viewProjection = camera.projection * camera.view;
    const auto pieces = avatar(player);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, d.shadowBuffer);
    glViewport(0, 0, shadowSize, shadowSize);
    glClear(GL_DEPTH_BUFFER_BIT);
    d.depth.use();
    matrix(d.depthLight, d.lightMatrix);
    matrix(d.depthModel, identity);
    for (const auto i : d.shadowChunks) d.chunks[i].draw();
    for (const auto& piece : pieces) {
        matrix(d.depthModel, piece.model);
        d.cube.draw();
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    d.sky.use();
    matrix(d.skyInverse, glm::inverse(viewProjection));
    vector(d.skyEye, camera.eye);
    glUniform1f(d.skyTime, time);
    glBindVertexArray(d.emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    d.world.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, d.shadowMap);
    matrix(d.worldView, viewProjection);
    matrix(d.worldLight, d.lightMatrix);
    matrix(d.worldModel, identity);
    vector(d.worldEye, camera.eye);
    glUniform1f(d.worldTime, time);
    glUniform1f(d.worldTile, -1.f);

    const Frustum frustum{viewProjection};
    RenderStats stats = d.totals;
    for (const auto& chunk : d.chunks) {
        if (!chunk.count || !frustum.intersects(chunk.origin, chunk.origin + glm::vec3(chunkSize)))
            continue;
        chunk.draw();
        ++stats.chunks;
        stats.triangles += chunk.count / 3;
    }
    for (const auto& piece : pieces) {
        matrix(d.worldModel, piece.model);
        glUniform1f(d.worldTile, piece.material);
        d.cube.draw();
    }
    if (selected) {
        d.line.use();
        const auto model = glm::scale(glm::translate(identity, glm::vec3(selected->block) - .005f),
                                      glm::vec3(1.01f));
        matrix(d.lineMatrix, viewProjection * model);
        glBindVertexArray(d.lineVao);
        glDrawArrays(GL_LINES, 0, 24);
    }
    return stats;
}

} // namespace ql
