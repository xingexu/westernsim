#include "hud.hpp"
#include "renderer.hpp"
#include "campus.hpp"
#include "remote_avatar.hpp"

#include "graphics.hpp"
#include <GLFW/glfw3.h>
#ifdef __EMSCRIPTEN__
#include <GLFW/emscripten_glfw3.h>
#include <emscripten.h>
#endif
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

#ifdef __EMSCRIPTEN__
EM_JS(void, showBrowserError, (const char* message), { Module.westernSimFailed(UTF8ToString(message)); });
EM_JS(void, showBrowserReady, (), { Module.westernSimReady(); });
EM_JS(void, sendBrowserPose, (float x,float y,float z,float heading,float stride,float gait), {
    if (Module.westernSimSendPose) Module.westernSimSendPose(x,y,z,heading,stride,gait);
});
#endif

namespace {

using namespace wsim;
constexpr float pi = 3.14159265f;
#ifdef __EMSCRIPTEN__
constexpr int diagnosticsKey = GLFW_KEY_I;
#else
constexpr int diagnosticsKey = GLFW_KEY_F1;
constexpr int benchmarkWarmup = 30, benchmarkFrames = 120;

struct Options {
    bool benchmark = false, screenshotMap = false, screenshotAvatar = false;
    std::filesystem::path screenshot;
};
#endif

struct Glfw {
    Glfw() {
        glfwSetErrorCallback([](int, const char* error) { std::cerr << "GLFW: " << error << '\n'; });
        if (!glfwInit()) throw std::runtime_error("Could not initialize GLFW");
    }
    ~Glfw() { glfwTerminate(); }
};

struct Window {
    GLFWwindow* handle;
#ifdef __EMSCRIPTEN__
    Window(int width, int height) {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,2);
        glfwWindowHint(GLFW_SCALE_FRAMEBUFFER,GLFW_FALSE);
        emscripten::glfw3::SetNextWindowCanvasSelector("#canvas");
#else
    Window(bool offscreen, int width, int height) {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
        glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,GLFW_TRUE);
        glfwWindowHint(GLFW_VISIBLE,offscreen ? GLFW_FALSE : GLFW_TRUE);
#endif
        handle = glfwCreateWindow(width,height,"Western Sim",nullptr,nullptr);
#ifdef __EMSCRIPTEN__
        if (!handle) throw std::runtime_error("Could not start WebGL 2. Check your browser's graphics settings.");
        emscripten::glfw3::MakeCanvasResizable(handle,"window");
#else
        if (!handle) throw std::runtime_error("Could not create an OpenGL 3.3 window");
#endif
    }
    ~Window() { glfwDestroyWindow(handle); }
};

struct Orbit {
    float yaw = -1.11f, pitch = .17f;
    bool overhead = false;

    glm::vec3 horizontal() const { return {std::sin(yaw), 0.f, -std::cos(yaw)}; }
    glm::vec3 right() const { return {std::cos(yaw), 0.f, std::sin(yaw)}; }

    Camera view(const World& world, glm::vec3 feet, float aspect) const {
        const float elevation = overhead ? -1.36f : pitch;
        const auto direction = horizontal() * std::cos(elevation) + glm::vec3(0,std::sin(elevation),0);
        const auto focus = feet + glm::vec3(0,playerHeight + .1f,0) + right() * .14f;
        float distance = overhead ? 48.f : 3.65f;
        if (const auto obstruction = raycast(world,focus,-direction,distance))
            distance = std::max(.12f, obstruction->distance - .22f);
        const auto eye = focus - direction * distance;
        const auto forward = overhead ? glm::normalize(direction + horizontal()*.25f) : direction;
        return {eye,forward,glm::lookAt(eye,eye+forward,{0,1,0}),
                glm::perspective(62.f * pi / 180.f,aspect,.055f,220.f)};
    }
};

#ifndef __EMSCRIPTEN__
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
#endif

void checkGraphics() {
    const GLenum error = glGetError();
    if (error != GL_NO_ERROR) throw std::runtime_error("OpenGL error " + std::to_string(error));
}

RenderStats drawFrame(Renderer& renderer, Hud& hud, int width, int height, const World& world, const Camera& camera,
                      const Player& player, float time, HudState& state, const std::vector<RigidBox>& boxes = {},
                      const Water* water = nullptr, const std::optional<Player>& peer = {}) {
    state.stats = renderer.draw(camera,player,width,height,time,boxes,water,peer);
    hud.draw(width,height,world,camera,state);
    return state.stats;
}

#ifndef __EMSCRIPTEN__
void benchmark(GLFWwindow* window, Renderer& renderer, Hud& hud, const World& world, int width, int height) {
    using Clock = std::chrono::steady_clock;
    std::cout << "Renderer: " << glGetString(GL_RENDERER) << '\n'
              << "Driver: " << glGetString(GL_VERSION) << '\n'
              << "World: " << world.edge() << " x " << worldHeight << " x " << world.edge()
              << " | " << width << " x " << height << " pixel-art output | "
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
            state.position = {player.feet.x,player.feet.z};
            state.heading = orbit.yaw * 180.f / pi;
            state.meshing = mode;
            stats = drawFrame(renderer,hud,width,height,world,camera,player,t * 3.f,state);
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
#endif

class Game {
public:
    Game(GLFWwindow* window, Renderer& renderer, Hud& hud)
        : window_(window), renderer_(renderer), hud_(hud) {
        resetPlayer();
        renderer_.rebuild(world_,Meshing::greedy);
        glfwSetWindowUserPointer(window_,this);
        glfwSetKeyCallback(window_,[](GLFWwindow* window,int key,int,int action,int) {
#ifdef __EMSCRIPTEN__
            if (!game(window).captured_) return;
#endif
            if (action == GLFW_PRESS && key >= 0 && key <= GLFW_KEY_LAST) game(window).keys_[key] = true;
        });
#ifndef __EMSCRIPTEN__
        glfwSetCursorPosCallback(window_,[](GLFWwindow* window,double x,double y) {
            auto& self = game(window);
            if (self.cursorKnown_) self.look(float(x-self.mouseX_),float(y-self.mouseY_));
            self.mouseX_ = x;
            self.mouseY_ = y;
            self.cursorKnown_ = true;
        });
#endif
        glfwSetMouseButtonCallback(window_,[](GLFWwindow* window,int button,int action,int) {
            auto& self = game(window);
            if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS &&
                !self.captured_ && !self.overlay()) self.cursor(true);
        });
        glfwSetWindowFocusCallback(window_,[](GLFWwindow* window,int focused) {
            auto& self = game(window);
#ifndef __EMSCRIPTEN__
            self.cursorKnown_ = false;
#endif
            if (!focused) self.cursor(false);
        });
#ifndef __EMSCRIPTEN__
        cursor(true);
#endif
        previous_ = glfwGetTime();
    }

    void look(float dx, float dy) {
        if (!captured_ || overlay()) return;
        orbit_.yaw = std::remainder(orbit_.yaw + dx * .0026f,2*pi);
        if (!orbit_.overhead)
            orbit_.pitch = std::clamp(orbit_.pitch - dy * .0026f,-1.33f,1.14f);
    }

#ifdef __EMSCRIPTEN__
    void requestCapture() { cursor(true); }
    void receivePeer(glm::vec3 feet,float heading,float stride,float gait) {
        Player pose;
        pose.feet = feet;
        pose.heading = heading;
        pose.stride = stride;
        pose.gait = gait;
        peer_.receive(pose,glfwGetTime());
    }
    void disconnectPeer() { peer_.clear(); }
#else
    void run() {
        while (!glfwWindowShouldClose(window_)) {
            if (!frame()) glfwWaitEventsTimeout(.1);
        }
    }
#endif

    bool frame() {
        glfwPollEvents();
#ifdef __EMSCRIPTEN__
        const bool locked = glfwGetInputMode(window_,GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
        if (locked != captured_) {
            captured_ = locked;
            if (!locked) {
                state_.guide = state_.map = false;
                jumpQueued_ = false;
                keys_.fill(false);
            }
        }
#endif
        const double now = glfwGetTime();
        const double elapsed = std::clamp(now - previous_,0.,.1);
        previous_ = now;
        handleKeys();
        if (!overlay()) simulate(elapsed);
#ifdef __EMSCRIPTEN__
        const auto p = player_.feet;
        sendBrowserPose(p.x,p.y,p.z,std::remainder(player_.heading,2*pi),player_.stride,player_.gait);
#endif
        int width, height;
        glfwGetFramebufferSize(window_,&width,&height);
        if (width == 0 || height == 0) return false;
        const auto camera = orbit_.view(world_,player_.feet,float(width)/height);
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
        drawFrame(renderer_,hud_,width,height,world_,camera,player_,float(now),state_,boxes_.bodies(),&water_,peer_.pose(now));
#ifndef __EMSCRIPTEN__
        glfwSwapBuffers(window_);
#endif
        return true;
    }

private:
    GLFWwindow* window_;
    Renderer& renderer_;
    Hud& hud_;
    const World world_ = makeCampus();
    Water water_{world_};
    Player player_;
    RigidBoxes boxes_;
    RemoteAvatar peer_;
    Orbit orbit_;
    HudState state_;
    std::array<bool,GLFW_KEY_LAST+1> keys_{};
    bool captured_ = false, jumpQueued_ = false;
    bool mapReady_ = false;
#ifndef __EMSCRIPTEN__
    bool cursorKnown_ = false;
    double mouseX_ = 0, mouseY_ = 0;
#endif
    double accumulator_ = 0, toastUntil_ = 0;
    double wakeCooldown_ = 0;
    double fpsTime_ = 0, previous_ = 0;
    int fpsFrames_ = 0;
    std::string toast_;

    static Game& game(GLFWwindow* window) {
        return *static_cast<Game*>(glfwGetWindowUserPointer(window));
    }
    bool held(int key) const { return glfwGetKey(window_,key) == GLFW_PRESS; }
    bool overlay() const { return state_.guide || state_.map; }

    void cursor(bool capture) {
#ifndef __EMSCRIPTEN__
        captured_ = capture;
        cursorKnown_ = false;
#endif
        glfwSetInputMode(window_,GLFW_CURSOR,capture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
#ifndef __EMSCRIPTEN__
        if (glfwRawMouseMotionSupported())
            glfwSetInputMode(window_,GLFW_RAW_MOUSE_MOTION,capture ? GLFW_TRUE : GLFW_FALSE);
#endif
    }

    void resetPlayer() {
        player_ = {};
        player_.feet = world_.spawn();
        orbit_ = {};
        player_.heading = -orbit_.yaw;
        accumulator_ = 0;
        wakeCooldown_ = 0;
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
#ifndef __EMSCRIPTEN__
            cursor(!captured_);
#endif
        }
        if (keys_[GLFW_KEY_TAB]) {
            state_.guide = !state_.guide;
            state_.map = false;
#ifndef __EMSCRIPTEN__
            cursor(!overlay());
#endif
            jumpQueued_ = false;
        }
        if (keys_[GLFW_KEY_M]) {
            state_.map = !state_.map;
            state_.guide = false;
            if (state_.map && !mapReady_) {
                hud_.setCampusMap(world_);
                mapReady_ = true;
            }
#ifndef __EMSCRIPTEN__
            cursor(!overlay());
#endif
            jumpQueued_ = false;
        }
        if (!overlay()) {
            if (keys_[diagnosticsKey]) state_.diagnostics = !state_.diagnostics;
            if (keys_[GLFW_KEY_R]) resetPlayer();
            if (keys_[GLFW_KEY_C]) orbit_.overhead = !orbit_.overhead;
            if (keys_[GLFW_KEY_SPACE] && captured_) jumpQueued_ = true;
            if (keys_[GLFW_KEY_B] && captured_) {
                const auto origin = player_.feet+glm::vec3(0,1.1f,0);
                const auto direction = orbit_.horizontal();
                if (raycast(world_,origin,direction,1.6f) || !boxes_.spawn(world_,origin+direction*1.6f))
                    announce("Need clear space for a box");
            }
        }
        keys_.fill(false);
    }

    void simulate(double elapsed) {
        glm::vec3 wish{};
        if (captured_) {
            const bool forward = held(GLFW_KEY_UP) || held(GLFW_KEY_W);
            const bool back = held(GLFW_KEY_DOWN) || held(GLFW_KEY_S);
            const bool right = held(GLFW_KEY_RIGHT) || held(GLFW_KEY_D);
            const bool left = held(GLFW_KEY_LEFT) || held(GLFW_KEY_A);
            wish = orbit_.horizontal() * float(forward - back) + orbit_.right() * float(right - left);
        }
        accumulator_ += elapsed;
        while (accumulator_ >= physicsStep) {
            const bool inWater = world_.get(glm::ivec3(glm::floor(player_.feet + glm::vec3(0,.25f,0)))) == Block::water;
            const bool jump = jumpQueued_ || (captured_ && inWater && held(GLFW_KEY_SPACE));
            const auto previousFeet = player_.feet;
            const float verticalSpeed = player_.velocity.y;
            player_.step(world_,wish,held(GLFW_KEY_LEFT_SHIFT) || held(GLFW_KEY_RIGHT_SHIFT),jump);
            boxes_.step(world_,player_,previousFeet);
            wakeCooldown_ -= physicsStep;
            if (wakeCooldown_ <= 0. &&
                (glm::distance(player_.feet,previousFeet) > .018f || jump) &&
                water_.splash(player_.feet,verticalSpeed < -3.f ? .13f : .055f))
                wakeCooldown_ = .115;
            water_.step();
            jumpQueued_ = false;
            accumulator_ -= physicsStep;
        }
        if (player_.feet.y < -18.f) {
            resetPlayer();
            announce("Returned to UC Hill");
        }
    }

};

#ifdef __EMSCRIPTEN__
Game* browserGame = nullptr;

void browserError(const char* message) {
    std::cerr << "Western Sim: " << message << '\n';
    showBrowserError(message);
}
#else
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
        else throw std::runtime_error("Invalid argument: " + arg + " (see --help)");
    }
    if (result.benchmark && !result.screenshot.empty())
        throw std::runtime_error("Choose either --benchmark or --screenshot");
    return result;
}
#endif

} // namespace

#ifdef __EMSCRIPTEN__
extern "C" {
EMSCRIPTEN_KEEPALIVE void westernsim_capture() {
    if (browserGame) browserGame->requestCapture();
}
EMSCRIPTEN_KEEPALIVE void westernsim_look(float dx, float dy) {
    if (browserGame) browserGame->look(dx,dy);
}
EMSCRIPTEN_KEEPALIVE void westernsim_remote(float x,float y,float z,float heading,float stride,float gait) {
    if (browserGame) browserGame->receivePeer({x,y,z},heading,stride,gait);
}
EMSCRIPTEN_KEEPALIVE void westernsim_remote_clear() {
    if (browserGame) browserGame->disconnectPeer();
}
}

int main() {
    try {
        static const Glfw glfw;
        static Window window(1280,720);
        glfwMakeContextCurrent(window.handle);
        static Renderer renderer;
        static Hud hud;
        static Game game(window.handle,renderer,hud);
        browserGame = &game;
        game.frame();
        checkGraphics();
        showBrowserReady();
        emscripten_set_main_loop([] {
            try {
                browserGame->frame();
            } catch (const std::exception& error) {
                emscripten_cancel_main_loop();
                browserError(error.what());
            }
        },0,false);
    } catch (const std::exception& error) {
        browserError(error.what());
        return 1;
    }
}
#else
int main(int argc,char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--help") {
            std::cout << "Western Sim [--benchmark | --screenshot FILE.ppm | "
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
            Game game(window.handle,renderer,hud);
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
                    const auto target = player.feet + glm::vec3(0,playerHeight*.53f,0);
                    const auto eye = target - orbit.horizontal()*(1.38f*avatarScale) +
                                     glm::vec3(0,.115f*avatarScale,0);
                    camera = {eye,glm::normalize(target-eye),glm::lookAt(eye,target,{0,1,0}),
                              glm::perspective(44.f*pi/180.f,float(width)/height,.08f,275.f)};
                    renderer.draw(camera,player,width,height,1.2f);
                } else {
                    HudState state;
                    state.map = args.screenshotMap;
                    state.position = {player.feet.x,player.feet.z};
                    state.heading = orbit.yaw * 180.f / pi;
                    if (state.map) hud.setCampusMap(world);
                    drawFrame(renderer,hud,width,height,world,camera,player,1.2f,state);
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
#endif
