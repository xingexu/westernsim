#pragma once

namespace wsim {

class PixelFrame {
public:
    PixelFrame();
    ~PixelFrame();
    PixelFrame(const PixelFrame&) = delete;
    PixelFrame& operator=(const PixelFrame&) = delete;

    void resize(int width, int height);
    void startHud() const;
    int sceneWidth() const { return sceneWidth_; }
    int sceneHeight() const { return sceneHeight_; }
    int hudWidth() const { return width_; }
    int hudHeight() const { return height_; }
    unsigned sceneTarget() const { return framebuffer_; }

private:
    unsigned framebuffer_ = 0, color_ = 0, depth_ = 0;
    int sceneWidth_ = 0, sceneHeight_ = 0;
    int width_ = 0, height_ = 0;
};

} // namespace wsim
