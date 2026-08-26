#include "ui/SkiaRenderer.h"

#include <SDL3/SDL.h>
#include "include/core/SkCanvas.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkSurface.h"

#include <stdexcept>

namespace pavm {

SkiaRenderer::SkiaRenderer(SDL_Window* window) : window_(window) {
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        throw std::runtime_error(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
    }
    SDL_SetRenderVSync(renderer_, 1);
    syncWindowMetrics();
}

SkiaRenderer::~SkiaRenderer() {
    surface_.reset();
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
}

void SkiaRenderer::syncWindowMetrics() {
    int pixelWidth = 0;
    int pixelHeight = 0;
    if (!SDL_GetWindowSizeInPixels(window_, &pixelWidth, &pixelHeight)) {
        throw std::runtime_error(std::string("SDL_GetWindowSizeInPixels failed: ") + SDL_GetError());
    }
    float scale = SDL_GetWindowDisplayScale(window_);
    if (!(scale > 0.0f)) scale = 1.0f;
    resize(pixelWidth, pixelHeight, scale);
}

void SkiaRenderer::resize(int pixelWidth, int pixelHeight, float displayScale) {
    pixelWidth = pixelWidth < 1 ? 1 : pixelWidth;
    pixelHeight = pixelHeight < 1 ? 1 : pixelHeight;
    if (!(displayScale > 0.0f)) displayScale = 1.0f;
    const bool sizeChanged = pixelWidth != pixelWidth_ || pixelHeight != pixelHeight_;
    const bool scaleChanged = displayScale != displayScale_;
    if (!sizeChanged && !scaleChanged && texture_ && surface_) return;
    pixelWidth_ = pixelWidth;
    pixelHeight_ = pixelHeight;
    displayScale_ = displayScale;
    if (sizeChanged || !texture_ || !surface_) {
        if (texture_) {
            SDL_DestroyTexture(texture_);
            texture_ = nullptr;
        }
        const SkImageInfo info = SkImageInfo::Make(pixelWidth_, pixelHeight_, kBGRA_8888_SkColorType,
                                                   kPremul_SkAlphaType);
        surface_ = SkSurfaces::Raster(info);
        if (!surface_) throw std::runtime_error("Skia raster surface creation failed");
        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_BGRA32,
                                     SDL_TEXTUREACCESS_STREAMING, pixelWidth_, pixelHeight_);
        if (!texture_) throw std::runtime_error(std::string("SDL_CreateTexture failed: ") + SDL_GetError());
        SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_LINEAR);
    }
}

SkCanvas* SkiaRenderer::beginFrame() {
    if (!surface_) return nullptr;
    SkCanvas* canvas = surface_->getCanvas();
    canvas->resetMatrix();
    canvas->scale(displayScale_, displayScale_);
    return canvas;
}

void SkiaRenderer::windowToContent(float windowX, float windowY, float& contentX, float& contentY) const {
    float renderX = windowX;
    float renderY = windowY;
    if (renderer_) SDL_RenderCoordinatesFromWindow(renderer_, windowX, windowY, &renderX, &renderY);
    contentX = renderX / displayScale_;
    contentY = renderY / displayScale_;
}

void SkiaRenderer::present() {
    if (!surface_ || !texture_) return;
    SkPixmap pixels;
    if (!surface_->peekPixels(&pixels)) throw std::runtime_error("Skia surface pixels unavailable");
    if (!SDL_UpdateTexture(texture_, nullptr, pixels.addr(), static_cast<int>(pixels.rowBytes()))) {
        throw std::runtime_error(std::string("SDL_UpdateTexture failed: ") + SDL_GetError());
    }
    SDL_SetRenderDrawColor(renderer_, 15, 17, 22, 255);
    SDL_RenderClear(renderer_);
    SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
}

} // namespace pavm
