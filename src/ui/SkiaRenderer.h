#pragma once

#include "include/core/SkRefCnt.h"

#include <memory>

struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Window;
class SkCanvas;
class SkSurface;

namespace pavm {

class SkiaRenderer {
public:
    explicit SkiaRenderer(SDL_Window* window);
    ~SkiaRenderer();
    SkiaRenderer(const SkiaRenderer&) = delete;
    SkiaRenderer& operator=(const SkiaRenderer&) = delete;

    void syncWindowMetrics();
    void resize(int pixelWidth, int pixelHeight, float displayScale);
    SkCanvas* beginFrame();
    void present();
    [[nodiscard]] int width() const { return pixelWidth_; }
    [[nodiscard]] int height() const { return pixelHeight_; }
    [[nodiscard]] float displayScale() const { return displayScale_; }
    [[nodiscard]] float contentWidth() const { return static_cast<float>(pixelWidth_) / displayScale_; }
    [[nodiscard]] float contentHeight() const { return static_cast<float>(pixelHeight_) / displayScale_; }
    void windowToContent(float windowX, float windowY, float& contentX, float& contentY) const;

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    sk_sp<SkSurface> surface_;
    int pixelWidth_ = 0;
    int pixelHeight_ = 0;
    float displayScale_ = 1.0f;
};

} // namespace pavm
