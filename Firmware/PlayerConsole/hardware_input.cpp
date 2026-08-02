#include "hardware_input.h"

#include <Arduino.h>
#include <Button.h>
#include <ESP_Knob.h>
#include <freertos/FreeRTOS.h>
#include <new>

#include "app_config.h"

namespace {

constexpr uint8_t kQueueCapacity = 32;
InputEvent queue[kQueueCapacity] = {};
uint8_t head = 0;
uint8_t tail = 0;
uint8_t count = 0;
ESP_Knob *knob = nullptr;
Button *button = nullptr;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

void enqueueLocked(const InputEvent &event)
{
    if (event.kind == InputKind::Rotate && count > 0) {
        const uint8_t newest = static_cast<uint8_t>((tail + kQueueCapacity - 1) % kQueueCapacity);
        if (queue[newest].kind == InputKind::Rotate) {
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

// Reverse the electrical direction so the focus follows the physical bezel
// motion in the installed six-o'clock orientation.
void onLeft(int, void *) { enqueue(InputEvent{InputKind::Rotate, 1, millis()}); }
void onRight(int, void *) { enqueue(InputEvent{InputKind::Rotate, -1, millis()}); }
void onDown(void *, void *) { enqueue(InputEvent{InputKind::ButtonDown, 0, millis()}); }
void onUp(void *, void *) { enqueue(InputEvent{InputKind::ButtonUp, 0, millis()}); }

} // namespace

bool hardwareInputBegin()
{
    if (knob != nullptr || button != nullptr) return knob != nullptr && button != nullptr;
    knob = new (std::nothrow) ESP_Knob(kKnobPinA, kKnobPinB);
    button = new (std::nothrow) Button(kButtonPin, false);
    if (knob == nullptr || button == nullptr) return false;
    knob->begin();
    knob->attachLeftEventCallback(onLeft);
    knob->attachRightEventCallback(onRight);
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
    head = static_cast<uint8_t>((head + 1) % kQueueCapacity);
    --count;
    portEXIT_CRITICAL(&mux);
    return true;
}

#if GRIDOPOLY_SELF_TEST == 1
void hardwareInputTestReset()
{
    portENTER_CRITICAL(&mux);
    head = tail = count = 0;
    portEXIT_CRITICAL(&mux);
}

void hardwareInputTestEnqueue(const InputEvent &event) { enqueue(event); }
#endif
