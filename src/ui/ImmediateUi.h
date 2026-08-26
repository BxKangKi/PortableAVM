#pragma once

#include "include/core/SkRefCnt.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class SkCanvas;
class SkFontMgr;
class SkTypeface;

namespace pavm {

struct UiInput {
    float mouseX = 0;
    float mouseY = 0;
    bool mouseDown = false;
    bool mousePressed = false;
    bool mouseReleased = false;
    float wheelY = 0;
    std::string text;
    bool backspace = false;
    bool deleteKey = false;
    bool moveLeft = false;
    bool moveRight = false;
    bool moveHome = false;
    bool moveEnd = false;
    bool enter = false;
    bool escape = false;
    bool copy = false;
    bool selectAll = false;
};

class ImmediateUi {
public:
    ImmediateUi();
    ~ImmediateUi();

    void begin(SkCanvas* canvas, const UiInput& input,
               float x, float y, float width, float height);
    void end();

    void heading(const std::string& text);
    void label(const std::string& text, float fontSize = 14.0f);
    void muted(const std::string& text);
    void warning(const std::string& text);
    void success(const std::string& text);
    void separator();
    void spacer(float height = 8.0f);

    bool button(const std::string& id, const std::string& text,
                bool enabled = true, float height = 34.0f);
    bool compactTextItem(const std::string& id, const std::string& text, bool selected);
    bool checkbox(const std::string& id, const std::string& text, bool& value);
    bool sliderInt(const std::string& id, const std::string& text,
                   int& value, int minimum, int maximum, int step = 1);
    bool cycle(const std::string& id, const std::string& text,
               std::string& value, const std::vector<std::string>& options);
    bool textField(const std::string& id, const std::string& text,
                   std::string& value, const std::string& placeholder = {});
    void logView(const std::vector<std::string>& lines, float height = 260.0f);

    [[nodiscard]] float contentHeight() const;
    [[nodiscard]] float scrollOffset() const { return scroll_; }
    void setScrollOffset(float value) { scroll_ = value; }

private:
    struct Rect { float x, y, w, h; };
    bool hovered(const Rect& rect) const;
    void drawText(const std::string& text, float x, float baseline,
                  float size, unsigned int color, bool bold = false);
    float textWidth(const std::string& text, float size, bool bold = false) const;
    void wrappedText(const std::string& text, float size, unsigned int color);
    void fillRoundRect(const Rect& rect, float radius, unsigned int color);
    void strokeRoundRect(const Rect& rect, float radius, unsigned int color, float strokeWidth = 1.0f);
    Rect nextRect(float height);
    unsigned long long idHash(const std::string& id) const;

    SkCanvas* canvas_ = nullptr;
    UiInput input_{};
    Rect panel_{};
    float cursorY_ = 0;
    float scroll_ = 0;
    float maximumScroll_ = 0;
    bool scrollbarDragging_ = false;
    float scrollbarGrabOffset_ = 0.0f;
    unsigned long long focused_ = 0;
    unsigned long long active_ = 0;
    std::unordered_map<unsigned long long, std::size_t> caretByField_;
    bool logFocused_ = false;
    bool logSelecting_ = false;
    std::size_t logSelectionAnchor_ = 0;
    std::size_t logSelectionCaret_ = 0;
    sk_sp<SkFontMgr> fontManager_;
    sk_sp<SkTypeface> regular_;
    sk_sp<SkTypeface> bold_;
};

} // namespace pavm
