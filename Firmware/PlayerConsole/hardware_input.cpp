#include "hardware_input.h"

#include <Arduino.h>
#include <Button.h>
#include <esp_timer.h>
#include <soc/soc.h>
#include <soc/gpio_reg.h>
#include <freertos/FreeRTOS.h>
#include <new>

#include "app_config.h"
#include "rotary_input_filter.h"

namespace {

constexpr uint8_t kQueueCapacity = 32;
InputEvent queue[kQueueCapacity] = {};
uint8_t head = 0;
uint8_t tail = 0;
uint8_t count = 0;
esp_timer_handle_t rotaryTimer = nullptr;
Button *button = nullptr;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
gridopoly::player_console::RotaryQuadratureDecoder rotaryDecoder;
HardwareInputDiagnostic diagnostics[64]{};
uint8_t diagnosticHead = 0, diagnosticTail = 0, diagnosticCount = 0;
uint32_t diagnosticDropped = 0;
uint8_t lastRawPhases = 3;

void enqueueLocked(const InputEvent &event)
{
    if (event.kind == InputKind::Rotate && count > 0) {
        const uint8_t newest = static_cast<uint8_t>((tail + kQueueCapacity - 1) % kQueueCapacity);
        const bool sameDirection =
            (queue[newest].delta > 0 && event.delta > 0) ||
            (queue[newest].delta < 0 && event.delta < 0);
        if (queue[newest].kind == InputKind::Rotate && sameDirection) {
            const int32_t combined = static_cast<int32_t>(queue[newest].delta) + event.delta;
            if (combined >= INT16_MIN && combined <= INT16_MAX) {
                queue[newest].delta = static_cast<int16_t>(combined);
                return;
            }
        }
    }
    if (count >= kQueueCapacity) return;
    queue[tail] = event;
    tail = static_cast<uint8_t>((tail + 1) % kQueueCapacity);
    ++count;
}

void enqueue(const InputEvent &event)
{
    portENTER_CRITICAL(&mux);
    enqueueLocked(event);
    portEXIT_CRITICAL(&mux);
}

uint8_t readRotaryPhases()
{
    static_assert(kKnobPinA < 32 && kKnobPinB < 32, "Joint GPIO register sample");
    const uint32_t pins = REG_READ(GPIO_IN_REG);
    return static_cast<uint8_t>((((pins >> kKnobPinA) & 1U) << 1U) |
                                 ((pins >> kKnobPinB) & 1U));
}

void sampleRotary(void *)
{
    const uint32_t nowMs = millis();
    const uint8_t phases = readRotaryPhases();
    portENTER_CRITICAL(&mux);
    const uint32_t beforeInvalid = rotaryDecoder.invalidTransitions();
    const int8_t step = rotaryDecoder.sample(phases, nowMs);
    if (step != 0) enqueueLocked(InputEvent{InputKind::Rotate, step, nowMs});
    if (phases != lastRawPhases || step != 0 ||
        beforeInvalid != rotaryDecoder.invalidTransitions()) {
        if (diagnosticCount < 64) {
            diagnostics[diagnosticTail] = HardwareInputDiagnostic{
                nowMs, rotaryDecoder.invalidTransitions(), diagnosticDropped,
                phases, rotaryDecoder.stablePhases(), step, count};
            diagnosticTail = static_cast<uint8_t>((diagnosticTail + 1U) % 64U);
            ++diagnosticCount;
        } else ++diagnosticDropped;
    }
    lastRawPhases = phases;
    portEXIT_CRITICAL(&mux);
}
void onDown(void *, void *) { enqueue(InputEvent{InputKind::ButtonDown, 0, millis()}); }
void onUp(void *, void *) { enqueue(InputEvent{InputKind::ButtonUp, 0, millis()}); }

} // namespace

bool hardwareInputBegin()
{
    if (rotaryTimer != nullptr || button != nullptr)
        return rotaryTimer != nullptr && button != nullptr;
    pinMode(kKnobPinA, INPUT_PULLUP);
    pinMode(kKnobPinB, INPUT_PULLUP);
    lastRawPhases = readRotaryPhases();
    rotaryDecoder.reset(lastRawPhases, millis());
    button = new (std::nothrow) Button(kButtonPin, false);
    if (button == nullptr) return false;
    esp_timer_create_args_t args{};
    args.callback = sampleRotary;
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "gridopoly_rotary";
    args.skip_unhandled_events = true;
    if (esp_timer_create(&args, &rotaryTimer) != ESP_OK) return false;
    if (esp_timer_start_periodic(rotaryTimer, 1000) != ESP_OK) {
        esp_timer_delete(rotaryTimer);
        rotaryTimer = nullptr;
        return false;
    }
    button->attachPressDownEventCb(onDown, nullptr);
    button->attachPressUpEventCb(onUp, nullptr);
    return true;
}

bool hardwareInputPoll(InputEvent &event)
{
    portENTER_CRITICAL(&mux);
    if (count == 0) {
        portEXIT_CRITICAL(&mux);
        return false;
    }
    event = queue[head];
    if (event.kind == InputKind::Rotate &&
        (event.delta > 1 || event.delta < -1)) {
        const int16_t step = event.delta > 0 ? 1 : -1;
        event.delta = step;
        queue[head].delta = static_cast<int16_t>(queue[head].delta - step);
    } else {
        head = static_cast<uint8_t>((head + 1) % kQueueCapacity);
        --count;
    }
    portEXIT_CRITICAL(&mux);
    return true;
}

#if GRIDOPOLY_SELF_TEST == 1
void hardwareInputTestReset()
{
    portENTER_CRITICAL(&mux);
    head = tail = count = 0;
    rotaryDecoder.reset(lastRawPhases, millis());
    diagnosticHead = diagnosticTail = diagnosticCount = 0;
    diagnosticDropped = 0;
    portEXIT_CRITICAL(&mux);
}

void hardwareInputTestEnqueue(const InputEvent &event) { enqueue(event); }
#endif

bool hardwareInputPollDiagnostic(HardwareInputDiagnostic &diagnostic)
{
    portENTER_CRITICAL(&mux);
    if (diagnosticCount == 0) {
        portEXIT_CRITICAL(&mux);
        return false;
    }
    diagnostic = diagnostics[diagnosticHead];
    diagnosticHead = static_cast<uint8_t>((diagnosticHead + 1U) % 64U);
    --diagnosticCount;
    portEXIT_CRITICAL(&mux);
    return true;
}

void hardwareInputClearDiagnostics()
{
    portENTER_CRITICAL(&mux);
    diagnosticHead = diagnosticTail = diagnosticCount = 0;
    diagnosticDropped = 0;
    portEXIT_CRITICAL(&mux);
}
