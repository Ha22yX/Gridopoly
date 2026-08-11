#include "ui_handwriting.h"
#include "ui_handwriting_neural_model.h"
#include "ui_handwriting_templates.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <float.h>
#include <limits.h>
#include <string.h>

namespace {

constexpr uint16_t kCanvasWidth = 324;
constexpr uint16_t kCanvasHeight = 220;
constexpr uint16_t kPointCapacity = 1024;
constexpr uint8_t kTemplateRasterWidth = 28;
constexpr uint8_t kTemplateRasterHeight = 40;
constexpr uint8_t kNeuralRasterWidth = 28;
constexpr uint8_t kNeuralRasterHeight = 28;
constexpr uint8_t kNeuralRasterInset = 2;
constexpr float kNeuralConfidentMargin = 0.45f;

lv_obj_t *activeCanvas = nullptr;
void *canvasBuffer = nullptr;
UiHandwritingSample points[kPointCapacity]{};
uint16_t pointCount = 0;
uint32_t lastCanvasTouchMs = 0;
uint32_t inkRgb = 0;
uint32_t canvasBackgroundRgb = 0;
bool canvasTouchedLastPoll = false;

// Recognition runs on the UI task. Reuse one workspace so the higher-detail
// raster cannot consume several kilobytes of that task's stack.
struct RecognitionWorkspace {
    uint8_t drawn[kTemplateRasterHeight][kTemplateRasterWidth];
    uint8_t model[kTemplateRasterHeight][kTemplateRasterWidth];
    uint8_t drawnDistance[kTemplateRasterHeight][kTemplateRasterWidth];
    uint8_t modelDistance[kTemplateRasterHeight][kTemplateRasterWidth];
    uint8_t neuralInput[kHandwritingNeuralInputSize];
    float neuralHidden[kHandwritingNeuralHiddenSize];
    float neuralLogits[kHandwritingNeuralOutputSize];
};

RecognitionWorkspace recognitionWorkspace{};

// Five-by-seven uppercase glyphs retained as the low-confidence fallback for
// unusual stroke geometry that leaves the neural output ambiguous.
constexpr uint8_t kGlyphRows[26][7] = {
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, // A
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // B
    {0x0F,0x10,0x10,0x10,0x10,0x10,0x0F}, // C
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, // D
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // E
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // F
    {0x0F,0x10,0x10,0x13,0x11,0x11,0x0F}, // G
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // H
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F}, // I
    {0x07,0x02,0x02,0x02,0x12,0x12,0x0C}, // J
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // K
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // L
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, // M
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, // N
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // O
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // P
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, // Q
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // R
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, // S
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // T
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // U
    {0x11,0x11,0x11,0x11,0x0A,0x0A,0x04}, // V
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, // W
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // X
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, // Y
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // Z
};

void clearCapture()
{
    pointCount = 0;
    lastCanvasTouchMs = 0;
    canvasTouchedLastPoll = false;
}

void setRaster(
    uint8_t (&raster)[kTemplateRasterHeight][kTemplateRasterWidth], int x, int y)
{
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            const int px = x + ox;
            const int py = y + oy;
            if (px >= 0 && px < kTemplateRasterWidth &&
                py >= 0 && py < kTemplateRasterHeight) {
                raster[py][px] = 1;
            }
        }
    }
}

void rasterLine(
                uint8_t (&raster)[kTemplateRasterHeight][kTemplateRasterWidth],
                int x0, int y0, int x1, int y1)
{
    const int dx = abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
        setRaster(raster, x0, y0);
        if (x0 == x1 && y0 == y1) break;
        const int twiceError = error * 2;
        if (twiceError >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twiceError <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void buildDistanceMap(
    const uint8_t (&raster)[kTemplateRasterHeight][kTemplateRasterWidth],
    uint8_t (&distance)[kTemplateRasterHeight][kTemplateRasterWidth])
{
    constexpr uint8_t kFar = kTemplateRasterWidth + kTemplateRasterHeight;
    for (uint8_t y = 0; y < kTemplateRasterHeight; ++y) {
        for (uint8_t x = 0; x < kTemplateRasterWidth; ++x) {
            distance[y][x] = raster[y][x] != 0 ? 0 : kFar;
        }
    }
    for (uint8_t y = 0; y < kTemplateRasterHeight; ++y) {
        for (uint8_t x = 0; x < kTemplateRasterWidth; ++x) {
            uint8_t value = distance[y][x];
            if (x != 0 && distance[y][x - 1] + 1 < value) {
                value = static_cast<uint8_t>(distance[y][x - 1] + 1);
            }
            if (y != 0 && distance[y - 1][x] + 1 < value) {
                value = static_cast<uint8_t>(distance[y - 1][x] + 1);
            }
            distance[y][x] = value;
        }
    }
    for (int y = kTemplateRasterHeight - 1; y >= 0; --y) {
        for (int x = kTemplateRasterWidth - 1; x >= 0; --x) {
            uint8_t value = distance[y][x];
            if (x + 1 < kTemplateRasterWidth) {
                const uint8_t candidate = static_cast<uint8_t>(distance[y][x + 1] + 1);
                if (candidate < value) value = candidate;
            }
            if (y + 1 < kTemplateRasterHeight) {
                const uint8_t candidate = static_cast<uint8_t>(distance[y + 1][x] + 1);
                if (candidate < value) value = candidate;
            }
            distance[y][x] = value;
        }
    }
}

uint32_t directedDistanceScore(
    const uint8_t (&raster)[kTemplateRasterHeight][kTemplateRasterWidth],
    const uint8_t (&distance)[kTemplateRasterHeight][kTemplateRasterWidth])
{
    uint32_t score = 0;
    uint16_t active = 0;
    for (uint8_t y = 0; y < kTemplateRasterHeight; ++y) {
        for (uint8_t x = 0; x < kTemplateRasterWidth; ++x) {
            if (raster[y][x] == 0) continue;
            ++active;
            const uint32_t value = distance[y][x];
            score += value * value;
        }
    }
    return active == 0 ? UINT32_MAX / 4u : score * 256u / active;
}

uint32_t rasterDistanceScore(
    const uint8_t (&drawn)[kTemplateRasterHeight][kTemplateRasterWidth],
    const uint8_t (&model)[kTemplateRasterHeight][kTemplateRasterWidth])
{
    buildDistanceMap(drawn, recognitionWorkspace.drawnDistance);
    buildDistanceMap(model, recognitionWorkspace.modelDistance);
    return directedDistanceScore(drawn, recognitionWorkspace.modelDistance) +
           directedDistanceScore(model, recognitionWorkspace.drawnDistance);
}

struct DirectionProfile {
    uint32_t horizontal = 0;
    uint32_t vertical = 0;
    uint32_t diagonal = 0;
};

DirectionProfile sampleDirections(const UiHandwritingSample *samples,
                                  uint16_t sampleCount, int width, int height)
{
    DirectionProfile result{};
    for (uint16_t index = 1; index < sampleCount; ++index) {
        if (!samples[index].connectsPrevious) continue;
        const uint32_t dx = static_cast<uint32_t>(
            abs(samples[index].x - samples[index - 1].x)) * 1024u /
            static_cast<uint32_t>(max(1, width));
        const uint32_t dy = static_cast<uint32_t>(
            abs(samples[index].y - samples[index - 1].y)) * 1024u /
            static_cast<uint32_t>(max(1, height));
        const uint32_t length = max(dx, dy);
        // A K leg is often steep but still diagonal. Only near-axis movement
        // counts as horizontal/vertical; everything else preserves diagonal
        // evidence for pairs such as R/K and T/Y.
        if (dx > dy * 4u) {
            result.horizontal += length;
        } else if (dy > dx * 4u) {
            result.vertical += length;
        } else {
            result.diagonal += length;
        }
    }
    return result;
}

void rasterGeneratedTemplate(uint8_t variant, uint8_t glyph,
                             uint8_t (&model)[kTemplateRasterHeight]
                                                   [kTemplateRasterWidth])
{
    for (uint8_t y = 0; y < kTemplateRasterHeight; ++y) {
        const uint32_t row = kHandwritingTemplates[variant][glyph][y];
        for (uint8_t x = 0; x < kTemplateRasterWidth; ++x) {
            model[y][x] = (row & (1u << x)) != 0 ? 1 : 0;
        }
    }
}

void rasterLegacyTemplate(uint8_t glyph,
                          uint8_t (&model)[kTemplateRasterHeight]
                                                [kTemplateRasterWidth])
{
    for (uint8_t row = 0; row < 7; ++row) {
        for (uint8_t column = 0; column < 5; ++column) {
            if ((kGlyphRows[glyph][row] & (1u << (4u - column))) == 0) continue;
            const int x = 2 + column * (kTemplateRasterWidth - 5) / 4;
            const int y = 2 + row * (kTemplateRasterHeight - 5) / 6;
            setRaster(model, x, y);
        }
    }
}

char recognizeSamplesTemplate(const UiHandwritingSample *samples,
                              uint16_t sampleCount)
{
    if (samples == nullptr || sampleCount < 4) return '\0';
    int16_t minX = INT16_MAX, minY = INT16_MAX, maxX = INT16_MIN, maxY = INT16_MIN;
    for (uint16_t index = 0; index < sampleCount; ++index) {
        minX = min(minX, samples[index].x);
        minY = min(minY, samples[index].y);
        maxX = max(maxX, samples[index].x);
        maxY = max(maxY, samples[index].y);
    }
    const int width = max(1, maxX - minX);
    const int height = max(1, maxY - minY);
    memset(recognitionWorkspace.drawn, 0, sizeof(recognitionWorkspace.drawn));
    int previousX = 0;
    int previousY = 0;
    for (uint16_t index = 0; index < sampleCount; ++index) {
        const int x = 1 + (samples[index].x - minX) *
                            (kTemplateRasterWidth - 3) / width;
        const int y = 1 + (samples[index].y - minY) *
                            (kTemplateRasterHeight - 3) / height;
        if (index != 0 && samples[index].connectsPrevious) {
            rasterLine(recognitionWorkspace.drawn, previousX, previousY, x, y);
        } else {
            setRaster(recognitionWorkspace.drawn, x, y);
        }
        previousX = x;
        previousY = y;
    }

    uint32_t scores[26]{};
    uint32_t bestScore = UINT32_MAX;
    uint8_t bestGlyph = 0;
    for (uint8_t glyph = 0; glyph < 26; ++glyph) {
        memset(recognitionWorkspace.model, 0, sizeof(recognitionWorkspace.model));
        rasterLegacyTemplate(glyph, recognitionWorkspace.model);
        uint32_t glyphScore = rasterDistanceScore(
            recognitionWorkspace.drawn, recognitionWorkspace.model);
        for (uint8_t variant = 0; variant < kHandwritingTemplateVariantCount;
             ++variant) {
            memset(recognitionWorkspace.model, 0, sizeof(recognitionWorkspace.model));
            rasterGeneratedTemplate(variant, glyph, recognitionWorkspace.model);
            glyphScore = min(
                glyphScore,
                rasterDistanceScore(recognitionWorkspace.drawn,
                                    recognitionWorkspace.model));
        }
        scores[glyph] = glyphScore;
        if (glyphScore < bestScore) {
            bestScore = glyphScore;
            bestGlyph = glyph;
        }
    }

    const DirectionProfile directions = sampleDirections(samples, sampleCount, width, height);
    const auto closeAlternative = [&](uint8_t glyph) {
        const uint32_t allowance = bestScore * 3u > 1800u
            ? bestScore * 3u : 1800u;
        return scores[glyph] <= bestScore + allowance;
    };
    constexpr uint8_t kGlyphK = 'K' - 'A';
    constexpr uint8_t kGlyphR = 'R' - 'A';
    const bool krCandidateIsCompetitive =
        closeAlternative(scores[kGlyphK] <= scores[kGlyphR] ? kGlyphK : kGlyphR);
    if ((bestGlyph == kGlyphK || bestGlyph == kGlyphR ||
         krCandidateIsCompetitive) &&
        krCandidateIsCompetitive) {
        bestGlyph = directions.horizontal * 2u >= directions.diagonal
            ? kGlyphR : kGlyphK;
    }
    constexpr uint8_t kGlyphT = 'T' - 'A';
    constexpr uint8_t kGlyphY = 'Y' - 'A';
    if ((bestGlyph == kGlyphT || bestGlyph == kGlyphY) &&
        closeAlternative(bestGlyph == kGlyphT ? kGlyphY : kGlyphT)) {
        bestGlyph = directions.diagonal * 2u > directions.horizontal
            ? kGlyphY : kGlyphT;
    }
    return static_cast<char>('A' + bestGlyph);
}

void setNeuralPixel(int x, int y)
{
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            const int px = x + ox;
            const int py = y + oy;
            if (px < 0 || px >= kNeuralRasterWidth ||
                py < 0 || py >= kNeuralRasterHeight) continue;
            recognitionWorkspace.neuralInput[py * kNeuralRasterWidth + px] = 1;
        }
    }
}

void neuralLine(int x0, int y0, int x1, int y1)
{
    const int dx = abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
        setNeuralPixel(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        const int twiceError = error * 2;
        if (twiceError >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twiceError <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

bool rasterNeuralInput(const UiHandwritingSample *samples, uint16_t sampleCount)
{
    if (samples == nullptr || sampleCount < 4) return false;
    int16_t minX = INT16_MAX, minY = INT16_MAX;
    int16_t maxX = INT16_MIN, maxY = INT16_MIN;
    for (uint16_t index = 0; index < sampleCount; ++index) {
        minX = min(minX, samples[index].x);
        minY = min(minY, samples[index].y);
        maxX = max(maxX, samples[index].x);
        maxY = max(maxY, samples[index].y);
    }
    const int width = max(1, maxX - minX);
    const int height = max(1, maxY - minY);
    constexpr int kUsable = kNeuralRasterWidth - kNeuralRasterInset * 2;
    int scaledWidth = kUsable;
    int scaledHeight = kUsable;
    if (width >= height) {
        scaledHeight = max(1, height * kUsable / width);
    } else {
        scaledWidth = max(1, width * kUsable / height);
    }
    const int offsetX = (kNeuralRasterWidth - scaledWidth) / 2;
    const int offsetY = (kNeuralRasterHeight - scaledHeight) / 2;
    memset(recognitionWorkspace.neuralInput, 0,
           sizeof(recognitionWorkspace.neuralInput));
    int previousX = 0;
    int previousY = 0;
    for (uint16_t index = 0; index < sampleCount; ++index) {
        const int x = offsetX + (samples[index].x - minX) *
                                    max(1, scaledWidth - 1) / width;
        const int y = offsetY + (samples[index].y - minY) *
                                    max(1, scaledHeight - 1) / height;
        if (index != 0 && samples[index].connectsPrevious) {
            neuralLine(previousX, previousY, x, y);
        } else {
            setNeuralPixel(x, y);
        }
        previousX = x;
        previousY = y;
    }
    return true;
}

struct NeuralRecognition {
    uint8_t glyph = 0;
    float margin = 0.0f;
};

NeuralRecognition inferNeuralModel()
{
    for (uint16_t hidden = 0; hidden < kHandwritingNeuralHiddenSize; ++hidden) {
        const int8_t *weights = kHandwritingNeuralHiddenWeights +
            hidden * kHandwritingNeuralInputSize;
        int32_t sum = 0;
        for (uint16_t input = 0; input < kHandwritingNeuralInputSize; ++input) {
            if (recognitionWorkspace.neuralInput[input] != 0) {
                sum += weights[input];
            }
        }
        const float value = static_cast<float>(sum) *
                                kHandwritingNeuralHiddenScales[hidden] +
                            kHandwritingNeuralHiddenBiases[hidden];
        recognitionWorkspace.neuralHidden[hidden] = max(0.0f, value);
    }

    uint8_t best = 0;
    float bestLogit = -FLT_MAX;
    float secondLogit = -FLT_MAX;
    for (uint8_t glyph = 0; glyph < kHandwritingNeuralOutputSize; ++glyph) {
        const int8_t *weights = kHandwritingNeuralOutputWeights +
            glyph * kHandwritingNeuralHiddenSize;
        float sum = 0.0f;
        for (uint16_t hidden = 0; hidden < kHandwritingNeuralHiddenSize; ++hidden) {
            sum += static_cast<float>(weights[hidden]) *
                   recognitionWorkspace.neuralHidden[hidden];
        }
        const float logit = sum * kHandwritingNeuralOutputScales[glyph] +
                            kHandwritingNeuralOutputBiases[glyph];
        recognitionWorkspace.neuralLogits[glyph] = logit;
        if (logit > bestLogit) {
            secondLogit = bestLogit;
            bestLogit = logit;
            best = glyph;
        } else if (logit > secondLogit) {
            secondLogit = logit;
        }
    }
    return NeuralRecognition{best, bestLogit - secondLogit};
}

char recognizeSamples(const UiHandwritingSample *samples, uint16_t sampleCount)
{
    if (!rasterNeuralInput(samples, sampleCount)) return '\0';
    const NeuralRecognition neural = inferNeuralModel();
    if (neural.margin >= kNeuralConfidentMargin) {
        return static_cast<char>('A' + neural.glyph);
    }
    return recognizeSamplesTemplate(samples, sampleCount);
}

bool pointInsideCanvas(lv_point_t point, int16_t &x, int16_t &y)
{
    if (activeCanvas == nullptr) return false;
    lv_area_t coordinates{};
    lv_obj_get_coords(activeCanvas, &coordinates);
    x = static_cast<int16_t>(point.x - coordinates.x1);
    y = static_cast<int16_t>(point.y - coordinates.y1);
    return x >= 0 && y >= 0 && x < kCanvasWidth && y < kCanvasHeight;
}

void appendPhysicalSample(int16_t x, int16_t y, uint32_t nowMs,
                          bool continuingPhysicalContact)
{
    if (activeCanvas == nullptr || pointCount >= kPointCapacity) return;
    const bool connect = pointCount != 0 &&
                         uiHandwritingSamplesShouldConnect(
                             continuingPhysicalContact,
                             points[pointCount - 1].sampledAtMs, nowMs);
    if (connect && points[pointCount - 1].x == x && points[pointCount - 1].y == y) {
        points[pointCount - 1].sampledAtMs = nowMs;
        return;
    }

    const UiHandwritingSample previous =
        pointCount == 0 ? UiHandwritingSample{} : points[pointCount - 1];
    points[pointCount++] = UiHandwritingSample{x, y, nowMs, connect};

    if (connect) {
        const lv_point_t segment[2] = {
            {previous.x, previous.y},
            {x, y},
        };
        lv_draw_line_dsc_t line{};
        lv_draw_line_dsc_init(&line);
        line.color = lv_color_hex(inkRgb);
        line.opa = LV_OPA_COVER;
        line.width = 5;
        line.round_start = 1;
        line.round_end = 1;
        lv_canvas_draw_line(activeCanvas, segment, 2, &line);
        return;
    }

    const int16_t left = x > 2 ? x - 2 : 0;
    const int16_t top = y > 2 ? y - 2 : 0;
    const int16_t right = x + 2 < kCanvasWidth ? x + 2 : kCanvasWidth - 1;
    const int16_t bottom = y + 2 < kCanvasHeight ? y + 2 : kCanvasHeight - 1;
    lv_draw_rect_dsc_t dot{};
    lv_draw_rect_dsc_init(&dot);
    dot.bg_color = lv_color_hex(inkRgb);
    dot.bg_opa = LV_OPA_COVER;
    dot.radius = LV_RADIUS_CIRCLE;
    lv_canvas_draw_rect(activeCanvas, left, top, right - left + 1, bottom - top + 1, &dot);
}

void canvasDeleteEvent(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_DELETE) return;
    if (lv_event_get_target(event) == activeCanvas) activeCanvas = nullptr;
    if (canvasBuffer != nullptr) {
        heap_caps_free(canvasBuffer);
        canvasBuffer = nullptr;
    }
    clearCapture();
}

} // namespace

lv_obj_t *uiHandwritingCreate(lv_obj_t *parent, UiRect rect, uint32_t background,
                              uint32_t border, uint32_t ink)
{
    uiHandwritingReset();
    if (parent == nullptr || rect.w != kCanvasWidth || rect.h != kCanvasHeight) return nullptr;
    const size_t bytes = LV_CANVAS_BUF_SIZE_TRUE_COLOR(kCanvasWidth, kCanvasHeight);
    canvasBuffer = heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (canvasBuffer == nullptr) canvasBuffer = heap_caps_calloc(1, bytes, MALLOC_CAP_8BIT);
    if (canvasBuffer == nullptr) return nullptr;
    activeCanvas = lv_canvas_create(parent);
    if (activeCanvas == nullptr) {
        heap_caps_free(canvasBuffer);
        canvasBuffer = nullptr;
        return nullptr;
    }
    lv_canvas_set_buffer(activeCanvas, canvasBuffer, kCanvasWidth, kCanvasHeight,
                         LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(activeCanvas, lv_color_hex(background), LV_OPA_COVER);
    lv_obj_set_pos(activeCanvas, rect.x, rect.y);
    lv_obj_set_size(activeCanvas, rect.w, rect.h);
    lv_obj_set_style_border_width(activeCanvas, 2, 0);
    lv_obj_set_style_border_color(activeCanvas, lv_color_hex(border), 0);
    lv_obj_set_style_radius(activeCanvas, 8, 0);
    lv_obj_add_flag(activeCanvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(activeCanvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(activeCanvas, canvasDeleteEvent, LV_EVENT_DELETE, nullptr);
    inkRgb = ink;
    canvasBackgroundRgb = background;
    return activeCanvas;
}

bool uiHandwritingPoll(char &character, uint32_t nowMs)
{
    bool touchingCanvas = false;
    for (lv_indev_t *input = lv_indev_get_next(nullptr); input != nullptr;
         input = lv_indev_get_next(input)) {
        if (lv_indev_get_type(input) != LV_INDEV_TYPE_POINTER ||
            input->proc.state != LV_INDEV_STATE_PRESSED) continue;
        lv_point_t point{};
        lv_indev_get_point(input, &point);
        int16_t x = 0;
        int16_t y = 0;
        if (!pointInsideCanvas(point, x, y)) continue;
        touchingCanvas = true;
        lastCanvasTouchMs = nowMs;
        appendPhysicalSample(x, y, nowMs, canvasTouchedLastPoll);
    }
    canvasTouchedLastPoll = touchingCanvas;
    if (!uiHandwritingRecognitionDue(touchingCanvas, pointCount,
                                     lastCanvasTouchMs, nowMs)) return false;
    character = uiHandwritingRecognizeSamples(points, pointCount);
    clearCapture();
    if (activeCanvas != nullptr) {
        lv_canvas_fill_bg(activeCanvas, lv_color_hex(canvasBackgroundRgb), LV_OPA_COVER);
    }
    return character != '\0';
}

bool uiHandwritingSamplesShouldConnect(uint32_t previousSampleMs,
                                       uint32_t currentSampleMs)
{
    return currentSampleMs - previousSampleMs <= kHandwritingStrokeJoinMs;
}

bool uiHandwritingSamplesShouldConnect(bool continuingPhysicalContact,
                                       uint32_t previousSampleMs,
                                       uint32_t currentSampleMs)
{
    return continuingPhysicalContact &&
           uiHandwritingSamplesShouldConnect(previousSampleMs, currentSampleMs);
}

char uiHandwritingRecognizeSamples(const UiHandwritingSample *samples,
                                   uint16_t sampleCount)
{
    return recognizeSamples(samples, sampleCount);
}

float uiHandwritingNeuralTestAccuracy()
{
    return kHandwritingNeuralTestAccuracy;
}

bool uiHandwritingRecognitionDue(bool touchingCanvas, uint16_t sampledPointCount,
                                 uint32_t lastActivityMs, uint32_t nowMs)
{
    return !touchingCanvas && sampledPointCount != 0 &&
           nowMs - lastActivityMs >= kHandwritingRecognitionIdleMs;
}

void uiHandwritingReset()
{
    activeCanvas = nullptr;
    if (canvasBuffer != nullptr) {
        heap_caps_free(canvasBuffer);
        canvasBuffer = nullptr;
    }
    clearCapture();
}
