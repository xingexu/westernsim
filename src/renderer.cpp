#include "renderer.hpp"
#include "avatar.hpp"
#include "campus.hpp"
#include "shaders.hpp"

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

namespace wsim {
namespace {

constexpr int shadowSize = 896;
const glm::vec3 sun = glm::normalize(glm::vec3(.58f,.72f,.38f));
constexpr glm::vec3 rgb(unsigned hex) {
    return {((hex >> 16) & 255) / 255.f, ((hex >> 8) & 255) / 255.f, (hex & 255) / 255.f};
}
const std::array<glm::vec3,static_cast<std::size_t>(Block::count)> colors{
    rgb(0x000000), rgb(0x91ad61), rgb(0x937259), rgb(0x879099), rgb(0xc5aa7d),
    rgb(0x76503c), rgb(0x426a47), rgb(0xd6c4a4), rgb(0x658b4f), rgb(0xdfcfac),
    rgb(0xb9765c), rgb(0xb68863), rgb(0x538a9b), rgb(0xffd590),
    rgb(0xc4ae94), rgb(0x61606d), rgb(0x678397), rgb(0x898388), rgb(0xccbeb1),
    rgb(0x568978), rgb(0xd3a65d), rgb(0xc48356), rgb(0x666387),
    rgb(0x857c9a), rgb(0x454050), rgb(0x42424d), rgb(0xb84251), rgb(0xf4e8cf), rgb(0xa977a9),
    {}, {}, {}, {}, {}, {},
    rgb(0xac7965), rgb(0xc9b89b), rgb(0xb7a99e), rgb(0xc9b499), rgb(0xc4b5a6),
    rgb(0xbca998), rgb(0xb47f66), rgb(0xad9a83), {}
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

struct PixelFrame {
    GLuint framebuffer = 0, color = 0, depth = 0;
    int width = 0, height = 0;

    PixelFrame() {
        glGenFramebuffers(1,&framebuffer);
        glGenTextures(1,&color);
        glGenRenderbuffers(1,&depth);
    }
    ~PixelFrame() {
        glDeleteFramebuffers(1,&framebuffer);
        glDeleteTextures(1,&color);
        glDeleteRenderbuffers(1,&depth);
    }
    void bind(int outputWidth, int outputHeight) {
        const auto area = static_cast<double>(outputWidth) * outputHeight;
        const auto scale = std::max(2,static_cast<int>(std::ceil(std::sqrt(area/165000.))));
        const int w = std::max(1,outputWidth/scale), h = std::max(1,outputHeight/scale);
        glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
        if (width != w || height != h) {
            width = w;
            height = h;
            glBindTexture(GL_TEXTURE_2D,color);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
            glBindRenderbuffer(GL_RENDERBUFFER,depth);
            glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,w,h);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                throw std::runtime_error("Could not create the pixel-art framebuffer");
        }
        glViewport(0,0,width,height);
    }
};

void matrix(GLint location, const glm::mat4& m) {
    glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(m));
}

void vector(GLint location, glm::vec3 v) { glUniform3fv(location, 1, glm::value_ptr(v)); }

struct GpuMesh {
    GLuint vao = 0, vertices = 0, indices = 0;
    GLsizei count = 0;
    glm::vec3 low{}, high{};

    GpuMesh() = default;
    GpuMesh(const GpuMesh&) = delete;
    GpuMesh& operator=(const GpuMesh&) = delete;
    GpuMesh(GpuMesh&& other) noexcept { *this = std::move(other); }
    GpuMesh& operator=(GpuMesh&& other) noexcept {
        std::swap(vao, other.vao);
        std::swap(vertices, other.vertices);
        std::swap(indices, other.indices);
        std::swap(count, other.count);
        std::swap(low, other.low);
        std::swap(high, other.high);
        return *this;
    }
    ~GpuMesh() {
        glDeleteBuffers(1, &indices);
        glDeleteBuffers(1, &vertices);
        glDeleteVertexArrays(1, &vao);
    }
    template<typename V>
    bool uploadBuffers(const std::vector<V>& data, const std::vector<std::uint32_t>& elements) {
        count = static_cast<GLsizei>(elements.size());
        if (count == 0) return false;
        if (!vao) {
            glGenVertexArrays(1, &vao);
            glGenBuffers(1, &vertices);
            glGenBuffers(1, &indices);
        }
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vertices);
        glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(V), data.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, elements.size() * sizeof(std::uint32_t),
                     elements.data(), GL_STATIC_DRAW);
        return true;
    }
    void upload(const Mesh& mesh) {
        if (!uploadBuffers(mesh.vertices,mesh.indices)) return;
        low = high = mesh.vertices.front().position;
        for (const auto& vertex : mesh.vertices) {
            low = glm::min(low,vertex.position);
            high = glm::max(high,vertex.position);
        }
        for (GLuint slot = 0; slot < 3; ++slot) glEnableVertexAttribArray(slot);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<void*>(offsetof(Vertex, position)));
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<void*>(offsetof(Vertex, normal)));
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<void*>(offsetof(Vertex, material)));
    }
    void upload(const AvatarMesh& mesh) {
        if (!uploadBuffers(mesh.vertices,mesh.indices)) return;
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(AvatarVertex),
                              reinterpret_cast<void*>(offsetof(AvatarVertex,position)));
        glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(AvatarVertex),
                              reinterpret_cast<void*>(offsetof(AvatarVertex,color)));
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

struct Renderer::Data {
    Program world{shaders::worldVertex, shaders::worldFragment};
    Program sky{shaders::skyVertex, shaders::skyFragment};
    Program depth{shaders::depthVertex, shaders::depthFragment};
    Program avatar{shaders::avatarVertex, shaders::avatarFragment};
    Program present{shaders::skyVertex, shaders::pixelFragment};
    const GLint worldView = world.uniform("viewProjection"), worldModel = world.uniform("model");
    const GLint worldLight = world.uniform("lightMatrix"), worldEye = world.uniform("eye");
    const GLint worldTime = world.uniform("time");
    const GLint skyInverse = sky.uniform("inverseViewProjection"), skyEye = sky.uniform("eye");
    const GLint skyTime = sky.uniform("time"), depthModel = depth.uniform("model");
    const GLint depthLight = depth.uniform("lightMatrix");
    const GLint avatarTransform = avatar.uniform("transform");
    GLuint emptyVao = 0;
    GLuint shadowBuffer = 0, shadowMap = 0;
    std::vector<GpuMesh> chunks;
    std::array<GpuMesh,avatarPartCount> avatarMeshes;
    PixelFrame pixels;
    RenderStats totals;

    Data() {
        for (std::size_t i = 0; i < avatarMeshes.size(); ++i)
            avatarMeshes[i].upload(westernAvatarMesh(static_cast<AvatarPart>(i)));
        world.use();
        vector(world.uniform("lightDirection"), sun);
        glUniform3fv(world.uniform("palette[0]"), static_cast<GLsizei>(colors.size()),
                     glm::value_ptr(colors.front()));
        glUniform1i(world.uniform("shadowMap"), 0);
        sky.use();
        vector(sky.uniform("lightDirection"), sun);
        present.use();
        glUniform1i(present.uniform("scene"),0);

        glGenVertexArrays(1, &emptyVao);
        glGenTextures(1, &shadowMap);
        glBindTexture(GL_TEXTURE_2D, shadowMap);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, shadowSize, shadowSize, 0,
                     GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &shadowBuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, shadowBuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowMap, 0);
        constexpr GLenum noColor = GL_NONE;
        glDrawBuffers(1,&noColor);
        glReadBuffer(GL_NONE);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Could not create the shadow framebuffer");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    ~Data() {
        glDeleteFramebuffers(1, &shadowBuffer);
        glDeleteTextures(1, &shadowMap);
        glDeleteVertexArrays(1, &emptyVao);
    }

    void recount() {
        totals = {};
        for (const auto& chunk : chunks) {
            if (!chunk.count) continue;
            ++totals.totalChunks;
            totals.totalTriangles += chunk.count / 3;
        }
    }
};

Renderer::Renderer() : data_(std::make_unique<Data>()) {}
Renderer::~Renderer() = default;

void Renderer::rebuild(const World& world, Meshing mode) {
    auto& d = *data_;
    d.world.use();
    vector(d.world.uniform("clockCenter"),campusClockCenter(world));
    d.chunks.resize(world.chunks().size());
    for (std::size_t i = 0; i < d.chunks.size(); ++i) {
        const auto& chunk = world.chunks()[i];
        d.chunks[i].upload(meshChunk(world, chunk, mode));
    }
    d.recount();
}

RenderStats Renderer::draw(const Camera& camera, const Player& player, int width, int height, float time) {
    auto& d = *data_;
    const glm::mat4 identity{1.f};
    const auto viewProjection = camera.projection * camera.view;
    const auto pose = westernAvatarPose(player);
    const glm::vec3 lightFocus = glm::floor(player.feet * 4.f) * .25f;
    const auto lightMatrix = glm::ortho(-42.f,42.f,-42.f,42.f,1.f,140.f) *
                             glm::lookAt(lightFocus+sun*70.f,lightFocus,{0,1,0});
    const Frustum light{lightMatrix};

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, d.shadowBuffer);
    glViewport(0, 0, shadowSize, shadowSize);
    glClear(GL_DEPTH_BUFFER_BIT);
    d.depth.use();
    matrix(d.depthLight, lightMatrix);
    matrix(d.depthModel, identity);
    for (const auto& chunk : d.chunks)
        if (chunk.count && light.intersects(chunk.low,chunk.high)) chunk.draw();
    for (std::size_t i = 0; i < pose.size(); ++i) {
        matrix(d.depthModel, pose[i]);
        d.avatarMeshes[i].draw();
    }

    d.pixels.bind(width,height);
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
    matrix(d.worldLight, lightMatrix);
    matrix(d.worldModel, identity);
    vector(d.worldEye, camera.eye);
    glUniform1f(d.worldTime, time);

    const Frustum frustum{viewProjection};
    RenderStats stats = d.totals;
    for (const auto& chunk : d.chunks) {
        if (!chunk.count || !frustum.intersects(chunk.low,chunk.high))
            continue;
        chunk.draw();
        ++stats.chunks;
        stats.triangles += chunk.count / 3;
    }
    d.avatar.use();
    for (std::size_t i = 0; i < pose.size(); ++i) {
        matrix(d.avatarTransform, viewProjection*pose[i]);
        d.avatarMeshes[i].draw();
    }
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glViewport(0,0,width,height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    d.present.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D,d.pixels.color);
    glBindVertexArray(d.emptyVao);
    glDrawArrays(GL_TRIANGLES,0,3);
    return stats;
}

} // namespace wsim
