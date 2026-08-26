#include "ui/ImmediateUi.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"
#include <SDL3/SDL.h>
#ifdef _WIN32
#include "include/ports/SkTypeface_win.h"
#else
#include "include/ports/SkFontMgr_fontconfig.h"
#endif

#include <algorithm>
#include <cmath>
#include <sstream>

namespace pavm {
namespace {

constexpr SkColor kText = SkColorSetARGB(255, 235, 238, 244);
constexpr SkColor kMuted = SkColorSetARGB(255, 151, 160, 177);
constexpr SkColor kPanel = SkColorSetARGB(255, 24, 28, 36);
constexpr SkColor kControl = SkColorSetARGB(255, 39, 45, 57);
constexpr SkColor kControlHover = SkColorSetARGB(255, 50, 58, 73);
constexpr SkColor kAccent = SkColorSetARGB(255, 72, 128, 255);
constexpr SkColor kAccentHover = SkColorSetARGB(255, 91, 144, 255);
constexpr SkColor kBorder = SkColorSetARGB(255, 68, 76, 94);
constexpr SkColor kDisabled = SkColorSetARGB(255, 61, 65, 75);
constexpr SkColor kWarning = SkColorSetARGB(255, 255, 191, 77);
constexpr SkColor kSuccess = SkColorSetARGB(255, 83, 209, 139);
constexpr float kPadding = 18.0f;
constexpr float kGap = 8.0f;

std::size_t previousUtf8Boundary(const std::string& value, std::size_t offset) {
    if (offset == 0) return 0;
    offset = std::min(offset, value.size());
    --offset;
    while (offset > 0 && (static_cast<unsigned char>(value[offset]) & 0xC0U) == 0x80U) --offset;
    return offset;
}

std::size_t nextUtf8Boundary(const std::string& value, std::size_t offset) {
    offset = std::min(offset, value.size());
    if (offset >= value.size()) return value.size();
    ++offset;
    while (offset < value.size() && (static_cast<unsigned char>(value[offset]) & 0xC0U) == 0x80U) ++offset;
    return offset;
}

} // namespace

ImmediateUi::ImmediateUi() {
#ifdef _WIN32
    fontManager_ = SkFontMgr_New_DirectWrite();
#else
    fontManager_ = SkFontMgr_New_FontConfig(nullptr);
#endif
    if (fontManager_) {
#ifdef _WIN32
        regular_ = fontManager_->matchFamilyStyle("Malgun Gothic", SkFontStyle::Normal());
        bold_ = fontManager_->matchFamilyStyle("Malgun Gothic", SkFontStyle::Bold());
#else
        regular_ = fontManager_->matchFamilyStyle("Noto Sans CJK KR", SkFontStyle::Normal());
        bold_ = fontManager_->matchFamilyStyle("Noto Sans CJK KR", SkFontStyle::Bold());
#endif
        if (!regular_) regular_ = fontManager_->matchFamilyStyle(nullptr, SkFontStyle::Normal());
        if (!bold_) bold_ = fontManager_->matchFamilyStyle(nullptr, SkFontStyle::Bold());
    }
}

ImmediateUi::~ImmediateUi() = default;

void ImmediateUi::begin(SkCanvas* canvas, const UiInput& input,
                        float x, float y, float width, float height) {
    canvas_ = canvas;
    input_ = input;
    panel_ = {x, y, width, height};
    const float scrollbarWidth = 11.0f;
    const float trackX = x + width - scrollbarWidth;
    if (maximumScroll_ > 0.0f) {
        const float viewport = std::max(1.0f, height);
        const float document = viewport + maximumScroll_;
        const float thumbHeight = std::clamp(viewport * viewport / document, 34.0f, viewport);
        const float travel = std::max(1.0f, viewport - thumbHeight);
        const float thumbY = y + (scroll_ / maximumScroll_) * travel;
        const bool overThumb = input_.mouseX >= trackX && input_.mouseX <= x + width &&
                               input_.mouseY >= thumbY && input_.mouseY <= thumbY + thumbHeight;
        const bool overTrack = input_.mouseX >= trackX && input_.mouseX <= x + width &&
                               input_.mouseY >= y && input_.mouseY <= y + height;
        if (overThumb && input_.mousePressed) {
            scrollbarDragging_ = true;
            scrollbarGrabOffset_ = input_.mouseY - thumbY;
        } else if (overTrack && input_.mousePressed && !overThumb) {
            const float centered = std::clamp(input_.mouseY - y - thumbHeight * 0.5f, 0.0f, travel);
            scroll_ = (centered / travel) * maximumScroll_;
            scrollbarDragging_ = true;
            scrollbarGrabOffset_ = thumbHeight * 0.5f;
        }
        if (scrollbarDragging_ && input_.mouseDown) {
            const float next = std::clamp(input_.mouseY - y - scrollbarGrabOffset_, 0.0f, travel);
            scroll_ = (next / travel) * maximumScroll_;
        }
        if (input_.mouseReleased) scrollbarDragging_ = false;
    } else {
        scrollbarDragging_ = false;
    }
    const bool pointerInsidePanel = input_.mouseX >= panel_.x && input_.mouseX <= panel_.x + panel_.w &&
                                    input_.mouseY >= panel_.y && input_.mouseY <= panel_.y + panel_.h;
    if (pointerInsidePanel && input_.wheelY != 0 && !scrollbarDragging_) {
        scroll_ -= input_.wheelY * 78.0f;
    }
    scroll_ = std::clamp(scroll_, 0.0f, maximumScroll_);
    cursorY_ = y + kPadding;
    SkPaint paint;
    paint.setColor(kPanel);
    paint.setAntiAlias(true);
    canvas_->drawRect(SkRect::MakeXYWH(x, y, width, height), paint);
    canvas_->save();
    canvas_->clipRect(SkRect::MakeXYWH(x, y, width, height));
    canvas_->translate(0, -scroll_);
}

void ImmediateUi::end() {
    const float documentBottom = cursorY_ + kPadding;
    maximumScroll_ = std::max(0.0f, documentBottom - (panel_.y + panel_.h));
    scroll_ = std::clamp(scroll_, 0.0f, maximumScroll_);
    if (canvas_) {
        canvas_->restore();
        if (maximumScroll_ > 0.0f) {
            const float trackWidth = 7.0f;
            const float trackX = panel_.x + panel_.w - trackWidth - 2.0f;
            const float viewport = std::max(1.0f, panel_.h);
            const float document = viewport + maximumScroll_;
            const float thumbHeight = std::clamp(viewport * viewport / document, 34.0f, viewport);
            const float travel = std::max(1.0f, viewport - thumbHeight);
            const float thumbY = panel_.y + (scroll_ / maximumScroll_) * travel;
            SkPaint track;
            track.setAntiAlias(true);
            track.setColor(SkColorSetARGB(120, 45, 51, 64));
            canvas_->drawRoundRect(SkRect::MakeXYWH(trackX, panel_.y + 2.0f, trackWidth,
                                                    std::max(1.0f, panel_.h - 4.0f)),
                                   3.5f, 3.5f, track);
            SkPaint thumb;
            thumb.setAntiAlias(true);
            thumb.setColor(scrollbarDragging_ ? kAccentHover : SkColorSetARGB(230, 112, 122, 145));
            canvas_->drawRoundRect(SkRect::MakeXYWH(trackX, thumbY, trackWidth, thumbHeight),
                                   3.5f, 3.5f, thumb);
        }
    }
    canvas_ = nullptr;
    input_.text.clear();
}

void ImmediateUi::heading(const std::string& text) {
    const Rect rect = nextRect(30.0f);
    drawText(text, rect.x, rect.y + 22.0f, 20.0f, kText, true);
}

void ImmediateUi::label(const std::string& text, float fontSize) {
    wrappedText(text, fontSize, kText);
}

void ImmediateUi::muted(const std::string& text) { wrappedText(text, 12.5f, kMuted); }
void ImmediateUi::warning(const std::string& text) { wrappedText(text, 13.0f, kWarning); }
void ImmediateUi::success(const std::string& text) { wrappedText(text, 13.0f, kSuccess); }

void ImmediateUi::separator() {
    const Rect rect = nextRect(12.0f);
    SkPaint paint;
    paint.setColor(kBorder);
    canvas_->drawRect(SkRect::MakeXYWH(rect.x, rect.y + 5.0f, rect.w, 1.0f), paint);
}

void ImmediateUi::spacer(float height) { cursorY_ += height; }

bool ImmediateUi::button(const std::string& id, const std::string& text, bool enabled, float height) {
    const Rect rect = nextRect(height);
    const bool over = enabled && hovered(rect);
    fillRoundRect(rect, 7.0f, enabled ? (over ? kAccentHover : kAccent) : kDisabled);

    // Buttons can become narrow when the emulator sidebar is collapsed or when
    // Windows display scaling is high. Shrink the label first and always clip it
    // to the button bounds so text never paints outside the control.
    float fontSize = 14.0f;
    const float available = std::max(1.0f, rect.w - 16.0f);
    while (fontSize > 10.0f && textWidth(text, fontSize, true) > available) {
        fontSize -= 0.5f;
    }
    const float width = textWidth(text, fontSize, true);
    canvas_->save();
    canvas_->clipRect(SkRect::MakeXYWH(rect.x + 4.0f, rect.y + 2.0f,
                                       std::max(1.0f, rect.w - 8.0f),
                                       std::max(1.0f, rect.h - 4.0f)));
    drawText(text, rect.x + (rect.w - width) * 0.5f,
             rect.y + height * 0.5f + fontSize * 0.35f,
             fontSize, enabled ? SK_ColorWHITE : kMuted, true);
    canvas_->restore();
    return enabled && over && input_.mouseReleased;
}

bool ImmediateUi::compactTextItem(const std::string& id, const std::string& text, bool selected) {
    (void)id;
    const Rect rect = nextRect(38.0f);
    const bool over = hovered(rect);
    if (selected || over) fillRoundRect(rect, 6.0f, selected ? kAccent : kControlHover);
    const float tw = textWidth(text, 12.5f, selected);
    drawText(text, rect.x + (rect.w - tw) * 0.5f, rect.y + 25.0f, 12.5f, selected ? SK_ColorWHITE : kMuted, selected);
    return over && input_.mouseReleased;
}

bool ImmediateUi::checkbox(const std::string& id, const std::string& text, bool& value) {
    const Rect rect = nextRect(30.0f);
    const Rect box{rect.x, rect.y + 4.0f, 21.0f, 21.0f};
    const bool over = hovered(rect);
    fillRoundRect(box, 5.0f, value ? kAccent : (over ? kControlHover : kControl));
    strokeRoundRect(box, 5.0f, value ? kAccent : kBorder);
    if (value) {
        SkPaint paint;
        paint.setColor(SK_ColorWHITE);
        paint.setStrokeWidth(2.2f);
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setAntiAlias(true);
        SkPath path;
        path.moveTo(box.x + 5, box.y + 11);
        path.lineTo(box.x + 9, box.y + 15);
        path.lineTo(box.x + 17, box.y + 7);
        canvas_->drawPath(path, paint);
    }
    drawText(text, box.x + 31.0f, rect.y + 21.0f, 13.5f, over ? kText : kMuted);
    if (over && input_.mouseReleased) {
        value = !value;
        return true;
    }
    return false;
}

bool ImmediateUi::sliderInt(const std::string& id, const std::string& text,
                            int& value, int minimum, int maximum, int step) {
    const Rect labelRect = nextRect(20.0f);
    drawText(text + ": " + std::to_string(value), labelRect.x, labelRect.y + 15.0f, 13.0f, kMuted);
    const Rect rect = nextRect(26.0f);
    const unsigned long long hash = idHash(id);
    const bool over = hovered(rect);
    if (over && input_.mousePressed) active_ = hash;
    if (input_.mouseReleased && active_ == hash) active_ = 0;
    bool changed = false;
    if (active_ == hash && input_.mouseDown) {
        const float fraction = std::clamp((input_.mouseX - rect.x) / rect.w, 0.0f, 1.0f);
        int next = minimum + static_cast<int>(std::round((maximum - minimum) * fraction));
        if (step > 1) next = minimum + ((next - minimum + step / 2) / step) * step;
        next = std::clamp(next, minimum, maximum);
        changed = next != value;
        value = next;
    }
    const float fraction = maximum == minimum ? 0.0f :
        static_cast<float>(value - minimum) / static_cast<float>(maximum - minimum);
    const Rect track{rect.x, rect.y + 10.0f, rect.w, 6.0f};
    fillRoundRect(track, 3.0f, kControl);
    fillRoundRect({track.x, track.y, track.w * fraction, track.h}, 3.0f, kAccent);
    fillRoundRect({track.x + track.w * fraction - 7.0f, rect.y + 6.0f, 14.0f, 14.0f}, 7.0f,
                  over || active_ == hash ? kAccentHover : kAccent);
    return changed;
}

bool ImmediateUi::cycle(const std::string& id, const std::string& text,
                        std::string& value, const std::vector<std::string>& options) {
    const Rect labelRect = nextRect(20.0f);
    drawText(text, labelRect.x, labelRect.y + 15.0f, 13.0f, kMuted);
    const Rect rect = nextRect(34.0f);
    fillRoundRect(rect, 7.0f, kControl);
    strokeRoundRect(rect, 7.0f, kBorder);
    const Rect left{rect.x, rect.y, 38.0f, rect.h};
    const Rect right{rect.x + rect.w - 38.0f, rect.y, 38.0f, rect.h};
    if (hovered(left)) fillRoundRect(left, 7.0f, kControlHover);
    if (hovered(right)) fillRoundRect(right, 7.0f, kControlHover);
    drawText("<", left.x + 15.0f, left.y + 23.0f, 16.0f, kText, true);
    drawText(">", right.x + 14.0f, right.y + 23.0f, 16.0f, kText, true);
    const float valueWidth = textWidth(value, 13.5f);
    drawText(value, rect.x + (rect.w - valueWidth) * 0.5f, rect.y + 23.0f, 13.5f, kText);
    if (options.empty()) return false;
    auto it = std::find(options.begin(), options.end(), value);
    std::size_t index = it == options.end() ? 0 : static_cast<std::size_t>(it - options.begin());
    if (hovered(left) && input_.mouseReleased) {
        index = index == 0 ? options.size() - 1 : index - 1;
        value = options[index];
        return true;
    }
    if (hovered(right) && input_.mouseReleased) {
        index = (index + 1) % options.size();
        value = options[index];
        return true;
    }
    return false;
}

bool ImmediateUi::textField(const std::string& id, const std::string& text,
                            std::string& value, const std::string& placeholder) {
    const Rect labelRect = nextRect(20.0f);
    drawText(text, labelRect.x, labelRect.y + 15.0f, 13.0f, kMuted);
    const Rect rect = nextRect(36.0f);
    const unsigned long long hash = idHash(id);
    const bool over = hovered(rect);
    std::size_t& caret = caretByField_[hash];
    if (caret > value.size()) caret = value.size();

    const float available = std::max(1.0f, rect.w - 22.0f);
    auto visibleStartForCaret = [&]() {
        std::size_t start = 0;
        while (start < caret && textWidth(value.substr(start, caret - start), 13.0f) > available) {
            start = nextUtf8Boundary(value, start);
        }
        return start;
    };

    if (over && input_.mouseReleased) {
        focused_ = hash;
        const std::size_t viewStart = visibleStartForCaret();
        const float targetX = std::clamp(input_.mouseX - (rect.x + 10.0f), 0.0f, available);
        std::size_t best = viewStart;
        float bestDistance = std::abs(targetX);
        std::size_t pos = viewStart;
        while (pos < value.size()) {
            const std::size_t next = nextUtf8Boundary(value, pos);
            const float x = textWidth(value.substr(viewStart, next - viewStart), 13.0f);
            const float distance = std::abs(targetX - x);
            if (distance <= bestDistance) {
                best = next;
                bestDistance = distance;
            }
            if (x > targetX && distance > bestDistance) break;
            pos = next;
            if (x > available) break;
        }
        caret = best;
    }
    if (input_.mouseReleased && !over && focused_ == hash) focused_ = 0;

    bool changed = false;
    if (focused_ == hash) {
        if (input_.escape || input_.enter) focused_ = 0;
        if (input_.moveHome) caret = 0;
        if (input_.moveEnd) caret = value.size();
        if (input_.moveLeft) caret = previousUtf8Boundary(value, caret);
        if (input_.moveRight) caret = nextUtf8Boundary(value, caret);
        if (input_.backspace && caret > 0) {
            const std::size_t previous = previousUtf8Boundary(value, caret);
            value.erase(previous, caret - previous);
            caret = previous;
            changed = true;
        }
        if (input_.deleteKey && caret < value.size()) {
            const std::size_t next = nextUtf8Boundary(value, caret);
            value.erase(caret, next - caret);
            changed = true;
        }
        if (!input_.text.empty() && value.size() + input_.text.size() < 2048) {
            value.insert(caret, input_.text);
            caret += input_.text.size();
            changed = true;
        }
    }

    fillRoundRect(rect, 7.0f, focused_ == hash ? kControlHover : kControl);
    strokeRoundRect(rect, 7.0f, focused_ == hash ? kAccent : kBorder,
                    focused_ == hash ? 1.6f : 1.0f);

    if (value.empty()) {
        std::string display = placeholder;
        while (!display.empty() && textWidth(display, 13.0f) > available) {
            display.erase(0, nextUtf8Boundary(display, 0));
        }
        drawText(display, rect.x + 10.0f, rect.y + 24.0f, 13.0f, kMuted);
        if (focused_ == hash) {
            SkPaint paint;
            paint.setColor(kText);
            canvas_->drawRect(SkRect::MakeXYWH(rect.x + 11.0f, rect.y + 8.0f, 1.0f, 20.0f), paint);
        }
        return changed;
    }

    std::size_t viewStart = visibleStartForCaret();
    std::size_t viewEnd = value.size();
    while (viewEnd > caret && textWidth(value.substr(viewStart, viewEnd - viewStart), 13.0f) > available) {
        viewEnd = previousUtf8Boundary(value, viewEnd);
    }
    while (viewEnd < value.size()) {
        const std::size_t next = nextUtf8Boundary(value, viewEnd);
        if (textWidth(value.substr(viewStart, next - viewStart), 13.0f) > available) break;
        viewEnd = next;
    }
    const std::string display = value.substr(viewStart, viewEnd - viewStart);
    drawText(display, rect.x + 10.0f, rect.y + 24.0f, 13.0f, kText);
    if (focused_ == hash) {
        const float cursorX = rect.x + 10.0f + textWidth(value.substr(viewStart, caret - viewStart), 13.0f);
        SkPaint paint;
        paint.setColor(kText);
        canvas_->drawRect(SkRect::MakeXYWH(std::min(cursorX, rect.x + rect.w - 10.0f), rect.y + 8.0f, 1.0f, 20.0f), paint);
    }
    return changed;
}

void ImmediateUi::logView(const std::vector<std::string>& lines, float height) {
    const Rect rect = nextRect(height);
    fillRoundRect(rect, 7.0f, SkColorSetARGB(255, 14, 16, 21));
    strokeRoundRect(rect, 7.0f, logFocused_ ? kAccent : kBorder, logFocused_ ? 1.4f : 1.0f);

    std::string joined;
    std::vector<std::size_t> starts;
    starts.reserve(lines.size());
    for (std::size_t i = 0; i < lines.size(); ++i) {
        starts.push_back(joined.size());
        joined += lines[i];
        if (i + 1 < lines.size()) joined.push_back('\n');
    }
    logSelectionAnchor_ = std::min(logSelectionAnchor_, joined.size());
    logSelectionCaret_ = std::min(logSelectionCaret_, joined.size());

    const int maximumLines = std::max(1, static_cast<int>((rect.h - 14.0f) / 16.0f));
    const std::size_t firstLine = lines.size() > static_cast<std::size_t>(maximumLines)
                                    ? lines.size() - static_cast<std::size_t>(maximumLines) : 0;

    auto byteAtPointer = [&](float pointerX, float pointerY) -> std::size_t {
        if (lines.empty()) return 0;
        const float docY = pointerY + scroll_;
        int row = static_cast<int>(std::floor((docY - (rect.y + 7.0f)) / 16.0f));
        row = std::clamp(row, 0, maximumLines - 1);
        std::size_t lineIndex = std::min(lines.size() - 1, firstLine + static_cast<std::size_t>(row));
        const std::string& line = lines[lineIndex];
        const float localX = std::max(0.0f, pointerX - (rect.x + 10.0f));
        std::size_t offset = 0;
        while (offset < line.size()) {
            const std::size_t next = nextUtf8Boundary(line, offset);
            const float left = textWidth(line.substr(0, offset), 11.0f);
            const float right = textWidth(line.substr(0, next), 11.0f);
            if (localX < (left + right) * 0.5f) break;
            offset = next;
        }
        return starts[lineIndex] + offset;
    };

    const bool over = hovered(rect);
    if (over && input_.mousePressed) {
        logFocused_ = true;
        logSelecting_ = true;
        logSelectionAnchor_ = byteAtPointer(input_.mouseX, input_.mouseY);
        logSelectionCaret_ = logSelectionAnchor_;
    } else if (input_.mousePressed && !over) {
        logFocused_ = false;
    }
    if (logSelecting_ && input_.mouseDown) {
        logSelectionCaret_ = byteAtPointer(input_.mouseX, input_.mouseY);
    }
    if (input_.mouseReleased) logSelecting_ = false;

    if (logFocused_ && input_.selectAll) {
        logSelectionAnchor_ = 0;
        logSelectionCaret_ = joined.size();
    }
    if (logFocused_ && input_.copy) {
        const std::size_t a = std::min(logSelectionAnchor_, logSelectionCaret_);
        const std::size_t b = std::max(logSelectionAnchor_, logSelectionCaret_);
        if (b > a) SDL_SetClipboardText(joined.substr(a, b - a).c_str());
    }

    const std::size_t selectionStart = std::min(logSelectionAnchor_, logSelectionCaret_);
    const std::size_t selectionEnd = std::max(logSelectionAnchor_, logSelectionCaret_);
    canvas_->save();
    canvas_->clipRect(SkRect::MakeXYWH(rect.x + 7, rect.y + 7, rect.w - 14, rect.h - 14));
    float y = rect.y + 19.0f;
    for (std::size_t i = firstLine; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        const std::size_t lineStart = starts[i];
        const std::size_t lineEnd = lineStart + line.size();
        const std::size_t a = std::max(selectionStart, lineStart);
        const std::size_t b = std::min(selectionEnd, lineEnd);
        if (b > a) {
            const float x1 = rect.x + 10.0f + textWidth(line.substr(0, a - lineStart), 11.0f);
            const float x2 = rect.x + 10.0f + textWidth(line.substr(0, b - lineStart), 11.0f);
            SkPaint selection;
            selection.setColor(SkColorSetARGB(190, 55, 92, 165));
            canvas_->drawRect(SkRect::MakeXYWH(x1, y - 12.0f, std::max(1.0f, x2 - x1), 15.0f), selection);
        }
        drawText(line, rect.x + 10.0f, y, 11.0f, kMuted);
        y += 16.0f;
    }
    canvas_->restore();
}

float ImmediateUi::contentHeight() const { return cursorY_ - panel_.y; }

bool ImmediateUi::hovered(const Rect& rect) const {
    const float translatedY = input_.mouseY + scroll_;
    return input_.mouseX >= rect.x && input_.mouseX <= rect.x + rect.w &&
           translatedY >= rect.y && translatedY <= rect.y + rect.h &&
           input_.mouseX >= panel_.x && input_.mouseX <= panel_.x + panel_.w &&
           input_.mouseY >= panel_.y && input_.mouseY <= panel_.y + panel_.h;
}

void ImmediateUi::drawText(const std::string& text, float x, float baseline,
                           float size, unsigned int color, bool useBold) {
    if (text.empty()) return;
    SkPaint paint;
    paint.setColor(static_cast<SkColor>(color));
    paint.setAntiAlias(true);
    SkFont font(useBold ? bold_ : regular_, size);
    font.setEdging(SkFont::Edging::kAntiAlias);
    canvas_->drawString(text.c_str(), x, baseline, font, paint);
}

float ImmediateUi::textWidth(const std::string& text, float size, bool useBold) const {
    SkFont font(useBold ? bold_ : regular_, size);
    return font.measureText(text.data(), text.size(), SkTextEncoding::kUTF8);
}

void ImmediateUi::wrappedText(const std::string& text, float size, unsigned int color) {
    const float maximum = panel_.w - 2.0f * kPadding;
    std::istringstream input(text);
    std::string paragraph;
    bool firstParagraph = true;
    while (std::getline(input, paragraph)) {
        if (!firstParagraph) cursorY_ += 3.0f;
        firstParagraph = false;
        std::istringstream words(paragraph);
        std::string word;
        std::string line;
        while (words >> word) {
            const std::string candidate = line.empty() ? word : line + " " + word;
            if (!line.empty() && textWidth(candidate, size) > maximum) {
                const Rect rect = nextRect(size + 6.0f);
                drawText(line, rect.x, rect.y + size + 1.0f, size, color);
                line = word;
            } else {
                line = candidate;
            }
        }
        if (line.empty() && paragraph.empty()) {
            cursorY_ += size + 4.0f;
        } else if (!line.empty()) {
            const Rect rect = nextRect(size + 6.0f);
            drawText(line, rect.x, rect.y + size + 1.0f, size, color);
        }
    }
}

void ImmediateUi::fillRoundRect(const Rect& rect, float radius, unsigned int color) {
    SkPaint paint;
    paint.setColor(static_cast<SkColor>(color));
    paint.setAntiAlias(true);
    canvas_->drawRoundRect(SkRect::MakeXYWH(rect.x, rect.y, rect.w, rect.h), radius, radius, paint);
}

void ImmediateUi::strokeRoundRect(const Rect& rect, float radius, unsigned int color, float strokeWidth) {
    SkPaint paint;
    paint.setColor(static_cast<SkColor>(color));
    paint.setAntiAlias(true);
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(strokeWidth);
    canvas_->drawRoundRect(SkRect::MakeXYWH(rect.x, rect.y, rect.w, rect.h), radius, radius, paint);
}

ImmediateUi::Rect ImmediateUi::nextRect(float height) {
    const Rect rect{panel_.x + kPadding, cursorY_, panel_.w - 2.0f * kPadding, height};
    cursorY_ += height + kGap;
    return rect;
}

unsigned long long ImmediateUi::idHash(const std::string& id) const {
    return static_cast<unsigned long long>(std::hash<std::string>{}(id));
}

} // namespace pavm
