#pragma once

#include <Arduino.h>
#include <functional>
#include <driver/gpio.h>

class KY040Encoder
{
public:
    KY040Encoder(uint8_t clkPin, uint8_t dtPin, uint8_t swPin,
                 std::function<void(int)> onChange,
                 std::function<void()> onButtonClick,
                 std::function<void()> onLongPress = nullptr)
        : _clkPin(clkPin), _dtPin(dtPin), _swPin(swPin),
          _onChange(onChange), _onButtonClick(onButtonClick), _onLongPress(onLongPress),
          _state(0b11), _oldState(0b11), _sequenceStep(0), _direction(0),
          _pendingDelta(0), _buttonPending(false), _longPressPending(false),
          _lastButtonMs(0), _pressed(false), _pressStartMs(0), _longPressFired(false)
    {
    }

    void begin()
    {
        pinMode(_clkPin, INPUT_PULLUP);
        pinMode(_dtPin, INPUT_PULLUP);
        pinMode(_swPin, INPUT_PULLUP);

        gpio_install_isr_service(0); // no-op if already installed

        gpio_set_intr_type((gpio_num_t)_clkPin, GPIO_INTR_ANYEDGE);
        gpio_isr_handler_add((gpio_num_t)_clkPin, rotationISR, this);
        gpio_intr_enable((gpio_num_t)_clkPin);

        gpio_set_intr_type((gpio_num_t)_dtPin, GPIO_INTR_ANYEDGE);
        gpio_isr_handler_add((gpio_num_t)_dtPin, rotationISR, this);
        gpio_intr_enable((gpio_num_t)_dtPin);

        gpio_set_intr_type((gpio_num_t)_swPin, GPIO_INTR_ANYEDGE);
        gpio_isr_handler_add((gpio_num_t)_swPin, buttonISR, this);
        gpio_intr_enable((gpio_num_t)_swPin);
    }

    void loop()
    {
        int delta = 0;
        noInterrupts();
        delta = _pendingDelta;
        _pendingDelta = 0;
        bool btn = _buttonPending;
        _buttonPending = false;
        bool lng = _longPressPending;
        _longPressPending = false;
        bool pressed = _pressed;
        unsigned long pressStart = _pressStartMs;
        interrupts();

        // detect long press threshold while button is held
        if (pressed && !_longPressFired && _onLongPress &&
            (millis() - pressStart) >= 750)
        {
            _longPressFired = true;
            _onLongPress();
        }

        if (delta && _onChange)
            _onChange(delta);
        if (btn && _onButtonClick)
            _onButtonClick();
    }

private:
    static void IRAM_ATTR rotationISR(void *arg);
    static void IRAM_ATTR buttonISR(void *arg);
    void IRAM_ATTR handleRotationISR();
    void IRAM_ATTR handleButtonISR();

    const uint8_t _clkPin, _dtPin, _swPin;
    std::function<void(int)> _onChange;
    std::function<void()> _onButtonClick;
    std::function<void()> _onLongPress;

    volatile uint8_t _state;
    volatile uint8_t _oldState;
    volatile uint8_t _sequenceStep;
    volatile int8_t _direction;
    volatile int _pendingDelta;
    volatile bool _buttonPending;
    volatile bool _longPressPending;
    volatile unsigned long _lastButtonMs;
    volatile bool _pressed;
    volatile unsigned long _pressStartMs;
    bool _longPressFired;  // only accessed from loop(), no need for volatile
};
