#include "hud.hpp"
#include "renderer.hpp"
#include "campus.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/common.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace wsim;
constexpr float pi = 3.14159265f;
constexpr float editReach = 8.f;
constexpr int benchmarkWarmup = 30, benchmarkFrames = 120;

struct Options {
    bool benchmark = false, screenshotMap = false, screenshotAvatar = false;
    std::filesystem::path screenshot, save = "world.qlw";
};

struct Glfw {
    Glfw() {
        glfwSetErrorCallback([](int, const char* error) { std::cerr << "GLFW: " << error << '\n'; });
        if (!glfwInit()) throw std::runtime_error("Could not initialize GLFW");
    }
    ~Glfw() { glfwTerminate(); }
};

struct Window {
    GLFWwindow* handle;
    Window(bool offscreen, int width, int height) {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
        glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,GLFW_TRUE);
        glfwWindowHint(GLFW_VISIBLE,offscreen ? GLFW_FALSE : GLFW_TRUE);
        glfwWindowHint(GLFW_SAMPLES,4);
        handle = glfwCreateWindow(width,height,"Western Sim",nullptr,nullptr);
        if (!handle) throw std::runtime_error("Could not create an OpenGL 3.3 window");
    }
    ~Window() { glfwDestroyWindow(handle); }
};

struct Orbit {
    float yaw = -.27f, pitch = .17f;

    glm::vec3 horizontal() const { return {std::sin(yaw), 0.f, -std::cos(yaw)}; }
    glm::vec3 right() const { return {std::cos(yaw), 0.f, std::sin(yaw)}; }

    Camera view(const World& world, glm::vec3 feet, float aspect) const {
        const auto direction = horizontal() * std::cos(pitch) + glm::vec3(0,std::sin(pitch),0);
        const auto focus = feet + glm::vec3(0,2.16f,0) + right() * .35f;
        float distance = 6.1f;
        if (const auto obstruction = raycast(world,focus,-direction,distance))
            distance = std::max(.12f, obstruction->distance - .22f);
        const auto eye = focus - direction * distance;
        return {eye,direction,glm::lookAt(eye,eye+direction,{0,1,0}),
                glm::perspective(68.f * pi / 180.f,aspect,.08f,275.f)};
    }
};

std::optional<Hit> buildTarget(const World& world, const Camera& camera, const Player& player) {
    const auto hit = raycast(world,camera.eye,camera.forward,editReach + 6.f);
    if (!hit) return std::nullopt;
    const auto position = camera.eye + camera.forward * (hit->distance + .015f);
    const auto eyes = player.feet + glm::vec3(0,1.40f,0);
    const auto reach = position - eyes;
    const auto distance = glm::length(reach);
    if (distance > editReach) return std::nullopt;
    const auto direct = raycast(world,eyes,reach,distance + .04f);
    return direct && direct->block == hit->block ? hit : std::nullopt;
}

void capture(const std::filesystem::path& path, int width, int height) {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT,1);
    glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
    std::ofstream out(path,std::ios::binary);
    out << "P6\n" << width << ' ' << height << "\n255\n";
    const auto stride = static_cast<std::size_t>(width) * 3;
    for (int y = height - 1; y >= 0; --y)
        out.write(reinterpret_cast<const char*>(pixels.data() + y * stride),stride);
    out.close();
    if (!out) throw std::runtime_error("Could not write screenshot");
}

void checkGraphics() {
    const GLenum error = glGetError();
    if (error != GL_NO_ERROR) throw std::runtime_error("OpenGL error " + std::to_string(error));
}

RenderStats drawFrame(Renderer& renderer, Hud& hud, int width, int height, const Camera& camera,
                      const Player& player, const std::optional<Hit>& hit, float time, HudState& state) {
    state.stats = renderer.draw(camera,player,hit,width,height,time);
    hud.draw(width,height,state);
    return state.stats;
}

void benchmark(GLFWwindow* window, Renderer& renderer, Hud& hud, const World& world, int width, int height) {
    using Clock = std::chrono::steady_clock;
    GLint samples = 0;
    glGetIntegerv(GL_SAMPLES,&samples);
    std::cout << "Renderer: " << glGetString(GL_RENDERER) << '\n'
              << "Driver: " << glGetString(GL_VERSION) << '\n'
              << "World: " << world.edge() << " x " << worldHeight << " x " << world.edge()
              << " | " << width << " x " << height << " native, " << samples << " MSAA samples | "
              << benchmarkWarmup << " warm-up + "
              << benchmarkFrames << " measured frames per mode | VSync off\n";
    Player player;
    player.feet = world.spawn();
    player.heading = -Orbit{}.yaw;
    for (const auto mode : {Meshing::exposed, Meshing::greedy}) {
        renderer.rebuild(world,mode);
        double submittedChunks = 0, submittedTriangles = 0;
        RenderStats stats;
        Clock::time_point start;
        for (int frame = -benchmarkWarmup; frame < benchmarkFrames; ++frame) {
            if (frame == 0) {
                glFinish();
                start = Clock::now();
            }
            const float t = std::max(frame,0) / static_cast<float>(benchmarkFrames - 1);
            Orbit orbit;
            orbit.yaw += .48f * std::sin(t * 2.f * pi);
            const auto camera = orbit.view(world,player.feet,float(width)/height);
            HudState state;
            state.heading = orbit.yaw * 180.f / pi;
            state.editing = true;
            stats = drawFrame(renderer,hud,width,height,camera,player,{},t * 3.f,state);
            glfwSwapBuffers(window);
            glfwPollEvents();
            if (frame >= 0) {
                submittedChunks += stats.chunks;
                submittedTriangles += stats.triangles;
            }
        }
        glFinish();
        const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
        checkGraphics();
        std::cout << (mode == Meshing::exposed ? "Exposed" : "Greedy") << ": "
                  << stats.totalTriangles << " triangles across " << stats.totalChunks
                  << " mesh-bearing chunks | " << std::fixed << std::setprecision(1)
                  << submittedChunks / benchmarkFrames << " avg chunks and "
                  << submittedTriangles / benchmarkFrames << " avg triangles in camera pass | "
                  << benchmarkFrames / seconds << " FPS ("
                  << seconds * 1000 / benchmarkFrames << " ms/frame)\n";
    }
}

class Game {
public:
    Game(GLFWwindow* window, Renderer& renderer, Hud& hud, const std::filesystem::path& path)
        : window_(window), renderer_(renderer), hud_(hud), savePath_(path) {
        resetPlayer();
        renderer_.rebuild(world_,state_.meshing);
        hud_.setCampusMap(world_);
        glfwSetWindowUserPointer(window_,this);
        glfwSetKeyCallback(window_,[](GLFWwindow* window,int key,int,int action,int) {
            if (action == GLFW_PRESS && key >= 0 && key <= GLFW_KEY_LAST) game(window).keys_[key] = true;
        });
        glfwSetCursorPosCallback(window_,[](GLFWwindow* window,double x,double y) {
            auto& self = game(window);
            if (self.captured_ && !self.overlay() && self.cursorKnown_) {
                self.orbit_.yaw = std::remainder(self.orbit_.yaw + float(x-self.mouseX_) * .0026f,2*pi);
                self.orbit_.pitch = std::clamp(self.orbit_.pitch - float(y-self.mouseY_) * .0026f,-1.33f,1.14f);
            }
            self.mouseX_ = x;
            self.mouseY_ = y;
            self.cursorKnown_ = true;
        });
        glfwSetMouseButtonCallback(window_,[](GLFWwindow* window,int button,int action,int) {
            auto& self = game(window);
            if (action != GLFW_PRESS || self.overlay()) return;
            if (!self.captured_) self.cursor(true);
            else if (button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_RIGHT)
                self.pendingEdits_.push_back(button);
        });
        glfwSetScrollCallback(window_,[](GLFWwindow* window,double,double y) {
            auto& self = game(window);
            if (!self.overlay() && y != 0) self.state_.slot = (self.state_.slot + (y < 0 ? 1 : 7)) % 8;
        });
        glfwSetWindowFocusCallback(window_,[](GLFWwindow* window,int focused) {
            auto& self = game(window);
            self.cursorKnown_ = false;
            if (!focused) self.cursor(false);
        });
        state_.editing = true;
        cursor(true);
    }

    void run() {
        double previous = glfwGetTime();
        while (!glfwWindowShouldClose(window_)) {
            glfwPollEvents();
            const double now = glfwGetTime();
            const double elapsed = std::clamp(now - previous,0.,.1);
            previous = now;
            handleKeys();
            if (!overlay()) simulate(elapsed);
            int width, height;
            glfwGetFramebufferSize(window_,&width,&height);
            if (width == 0 || height == 0) {
                glfwWaitEventsTimeout(.1);
                continue;
            }
            const auto camera = orbit_.view(world_,player_.feet,float(width)/height);
            auto hit = state_.editing && !overlay() ? buildTarget(world_,camera,player_) : std::nullopt;
            for (const auto button : pendingEdits_) {
                if (hit) edit(*hit,button);
                hit = state_.editing && !overlay() ? buildTarget(world_,camera,player_) : std::nullopt;
            }
            pendingEdits_.clear();
            state_.heading = orbit_.yaw * 180.f / pi;
            state_.position = {player_.feet.x,player_.feet.z};
            if (now >= toastUntil_) state_.toast = {};
            fpsTime_ += elapsed;
            ++fpsFrames_;
            if (fpsTime_ >= .35) {
                state_.fps = static_cast<float>(fpsFrames_ / fpsTime_);
                fpsTime_ = 0;
                fpsFrames_ = 0;
            }
            drawFrame(renderer_,hud_,width,height,camera,player_,hit,float(now),state_);
            glfwSwapBuffers(window_);
        }
    }

private:
    GLFWwindow* window_;
    Renderer& renderer_;
    Hud& hud_;
    std::filesystem::path savePath_;
    World world_ = makeCampus();
    Player player_;
    Orbit orbit_;
    HudState state_;
    std::array<bool,GLFW_KEY_LAST+1> keys_{};
    std::vector<int> pendingEdits_;
    bool captured_ = false, cursorKnown_ = false, jumpQueued_ = false;
    double mouseX_ = 0, mouseY_ = 0, accumulator_ = 0, toastUntil_ = 0;
    double fpsTime_ = 0;
    int fpsFrames_ = 0;
    std::string toast_;

    static Game& game(GLFWwindow* window) {
        return *static_cast<Game*>(glfwGetWindowUserPointer(window));
    }
    bool held(int key) const { return glfwGetKey(window_,key) == GLFW_PRESS; }
    bool overlay() const { return state_.guide || state_.map; }

    void cursor(bool capture) {
        captured_ = capture;
        cursorKnown_ = false;
        glfwSetInputMode(window_,GLFW_CURSOR,capture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        if (glfwRawMouseMotionSupported())
            glfwSetInputMode(window_,GLFW_RAW_MOUSE_MOTION,capture ? GLFW_TRUE : GLFW_FALSE);
    }

    void resetPlayer() {
        player_ = {};
        player_.feet = world_.spawn();
        orbit_ = {};
        player_.heading = -orbit_.yaw;
        accumulator_ = 0;
        jumpQueued_ = false;
    }

    void announce(std::string message) {
        toast_ = std::move(message);
        state_.toast = toast_;
        toastUntil_ = glfwGetTime() + 2.7;
    }

    void handleKeys() {
        if (keys_[GLFW_KEY_ESCAPE]) {
            state_.guide = state_.map = false;
            cursor(!captured_);
        }
        if (keys_[GLFW_KEY_TAB]) {
            state_.guide = !state_.guide;
            state_.map = false;
            cursor(!overlay());
            pendingEdits_.clear();
            jumpQueued_ = false;
        }
        if (keys_[GLFW_KEY_M]) {
            state_.map = !state_.map;
            state_.guide = false;
            cursor(!overlay());
            pendingEdits_.clear();
            jumpQueued_ = false;
        }
        if (!overlay()) {
            if (keys_[GLFW_KEY_E]) {
                state_.editing = !state_.editing;
                pendingEdits_.clear();
            }
            if (keys_[GLFW_KEY_F1]) state_.diagnostics = !state_.diagnostics;
            if (keys_[GLFW_KEY_F2]) {
                state_.meshing = state_.meshing == Meshing::greedy ? Meshing::exposed : Meshing::greedy;
                renderer_.rebuild(world_,state_.meshing);
                announce(state_.meshing == Meshing::greedy ? "Greedy mesh" : "Exposed faces");
            }
            if (keys_[GLFW_KEY_F5]) {
                std::string error;
                announce(world_.save(savePath_,error) ? "Campus saved" : error);
            }
            if (keys_[GLFW_KEY_F9]) {
                std::string error;
                if (auto saved = World::load(savePath_,error)) {
                    world_ = std::move(*saved);
                    renderer_.rebuild(world_,state_.meshing);
                    resetPlayer();
                    announce("Campus loaded");
                } else {
                    announce(error);
                }
            }
            if (keys_[GLFW_KEY_R]) resetPlayer();
            for (int i = 0; i < 8; ++i)
                if (keys_[GLFW_KEY_1+i]) state_.slot = i;
            if (keys_[GLFW_KEY_SPACE] && captured_) jumpQueued_ = true;
        }
        keys_.fill(false);
    }

    void simulate(double elapsed) {
        glm::vec3 wish{};
        if (captured_) {
            wish = orbit_.horizontal() * float(held(GLFW_KEY_W) - held(GLFW_KEY_S)) +
                   orbit_.right() * float(held(GLFW_KEY_D) - held(GLFW_KEY_A));
        }
        accumulator_ += elapsed;
        while (accumulator_ >= physicsStep) {
            const bool inWater = world_.get(glm::ivec3(glm::floor(player_.feet + glm::vec3(0,.5f,0)))) == Block::water;
            const bool jump = jumpQueued_ || (captured_ && inWater && held(GLFW_KEY_SPACE));
            player_.step(world_,wish,held(GLFW_KEY_LEFT_SHIFT) || held(GLFW_KEY_RIGHT_SHIFT),jump);
            jumpQueued_ = false;
            accumulator_ -= physicsStep;
        }
        if (player_.feet.y < -18.f) {
            resetPlayer();
            announce("Returned to UC Hill");
        }
    }

    void edit(const Hit& hit, int button) {
        auto at = hit.block;
        auto block = Block::air;
        if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            if (hit.normal == glm::ivec3(0)) return;
            at += hit.normal;
            const auto current = world_.get(at);
            if ((current != Block::air && current != Block::water) || player_.overlaps(at)) return;
            block = buildingPalette[state_.slot].block;
        }
        if (world_.set(at,block)) renderer_.update(world_,at);
    }
};

Options options(int argc, char** argv) {
    Options result;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--benchmark") result.benchmark = true;
        else if ((arg == "--screenshot" || arg == "--screenshot-map" || arg == "--screenshot-avatar")
                 && i + 1 < argc && result.screenshot.empty()) {
            result.screenshot = argv[++i];
            result.screenshotMap = arg == "--screenshot-map";
            result.screenshotAvatar = arg == "--screenshot-avatar";
        }
        else if (arg == "--save" && i + 1 < argc) result.save = argv[++i];
        else throw std::runtime_error("Invalid argument: " + arg + " (see --help)");
    }
    if (result.benchmark && !result.screenshot.empty())
        throw std::runtime_error("Choose either --benchmark or --screenshot");
    return result;
}

} // namespace

int main(int argc,char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--help") {
            std::cout << "Western Sim [--save FILE] [--benchmark | --screenshot FILE.ppm | "
                         "--screenshot-map FILE.ppm | --screenshot-avatar FILE.ppm]\n";
            return 0;
        }
        const auto args = options(argc,argv);
        const bool offscreen = args.benchmark || !args.screenshot.empty();
        const Glfw glfw;
        Window window(offscreen,args.benchmark ? 1280 : offscreen ? 1600 : 1440,
                      args.benchmark ? 720 : offscreen ? 900 : 810);
        glfwMakeContextCurrent(window.handle);
        if (!gladLoadGL(glfwGetProcAddress) || !GLAD_GL_VERSION_3_3)
            throw std::runtime_error("The OpenGL 3.3 core API is unavailable");
        glfwSwapInterval(offscreen ? 0 : 1);
        Renderer renderer;
        Hud hud;

        if (!offscreen) {
            glfwSetWindowSizeLimits(window.handle, 800, 450, GLFW_DONT_CARE, GLFW_DONT_CARE);
            Game game(window.handle,renderer,hud,args.save);
            game.run();
        } else {
            int width,height;
            glfwGetFramebufferSize(window.handle,&width,&height);
            const auto world = makeCampus();
            if (args.benchmark) benchmark(window.handle,renderer,hud,world,width,height);
            else {
                renderer.rebuild(world,Meshing::greedy);
                Player player;
                player.feet = world.spawn();
                Orbit orbit;
                player.heading = -orbit.yaw;
                auto camera = orbit.view(world,player.feet,float(width)/height);
                if (args.screenshotAvatar) {
                    player.heading = pi - orbit.yaw + .21f;
                    const auto target = player.feet + glm::vec3(0,.88f,0);
                    const auto eye = target - orbit.horizontal()*2.75f + glm::vec3(0,.23f,0);
                    camera = {eye,glm::normalize(target-eye),glm::lookAt(eye,target,{0,1,0}),
                              glm::perspective(44.f*pi/180.f,float(width)/height,.08f,275.f)};
                    renderer.draw(camera,player,{},width,height,1.2f);
                } else {
                    HudState state;
                    state.editing = true;
                    state.map = args.screenshotMap;
                    state.position = {player.feet.x,player.feet.z};
                    state.heading = orbit.yaw * 180.f / pi;
                    if (state.map) hud.setCampusMap(world);
                    drawFrame(renderer,hud,width,height,camera,player,{},1.2f,state);
                }
                capture(args.screenshot,width,height);
                checkGraphics();
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "Western Sim: " << error.what() << '\n';
        return 1;
    }
}
