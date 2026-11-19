#include "pixel_frame.hpp"

#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace wsim {

PixelFrame::PixelFrame() {
    glGenFramebuffers(1,&framebuffer_);
    glGenTextures(1,&color_);
    glGenRenderbuffers(1,&depth_);
}

PixelFrame::~PixelFrame() {
    glDeleteFramebuffers(1,&framebuffer_);
    glDeleteTextures(1,&color_);
    glDeleteRenderbuffers(1,&depth_);
}

void PixelFrame::resize(int width, int height) {
    width_ = width;
    height_ = height;
    const int sceneScale = std::max(1,static_cast<int>(std::round(height/210.f)));
    const int w = std::max(1,width/sceneScale), h = std::max(1,height/sceneScale);
    if (sceneWidth_ == w && sceneHeight_ == h) return;
    sceneWidth_ = w;
    sceneHeight_ = h;

    glBindFramebuffer(GL_FRAMEBUFFER,framebuffer_);
    glBindTexture(GL_TEXTURE_2D,color_);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color_,0);
    glBindRenderbuffer(GL_RENDERBUFFER,depth_);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,w,h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth_);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Could not create the pixel framebuffer");
    glBindFramebuffer(GL_FRAMEBUFFER,0);
}

void PixelFrame::startHud() const {
    glBindFramebuffer(GL_READ_FRAMEBUFFER,framebuffer_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
    glBlitFramebuffer(0,0,sceneWidth_,sceneHeight_,0,0,width_,height_,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glViewport(0,0,width_,height_);
}

} // namespace wsim
