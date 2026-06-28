#include "KY040Encoder.h"

void IRAM_ATTR KY040Encoder::rotationISR(void *arg)
{
    static_cast<KY040Encoder *>(arg)->handleRotationISR();
}

void IRAM_ATTR KY040Encoder::buttonISR(void *arg)
{
    static_cast<KY040Encoder *>(arg)->handleButtonISR();
}

// CW sequence:  01 → 00 → 10 → 11
// CCW sequence: 10 → 00 → 01 → 11
void IRAM_ATTR KY040Encoder::handleRotationISR()
{
    uint8_t newState = (digitalRead(_clkPin) << 1) | digitalRead(_dtPin);
    if (newState == _oldState)
        return;

    if (_sequenceStep == 0)
    {
        if (newState == 0b01)
        {
            _direction = 1;
            _sequenceStep = 1;
        }
        else if (newState == 0b10)
        {
            _direction = -1;
            _sequenceStep = 1;
        }
    }
    else
    {
        uint8_t expected;
        if (_direction == 1)
        {
            switch (_sequenceStep)
            {
            case 1:
                expected = 0b00;
                break;
            case 2:
                expected = 0b10;
                break;
            case 3:
                expected = 0b11;
                break;
            default:
                expected = 0xFF;
                break;
            }
        }
        else
        {
            switch (_sequenceStep)
            {
            case 1:
                expected = 0b00;
                break;
            case 2:
                expected = 0b01;
                break;
            case 3:
                expected = 0b11;
                break;
            default:
                expected = 0xFF;
                break;
            }
        }
        if (newState == expected)
        {
            _sequenceStep++;
            if (_sequenceStep == 4)
            {
                _pendingDelta += _direction;
                _sequenceStep = 0;
                _direction = 0;
            }
        }
        else if (newState == 0b11)
        {
            _sequenceStep = 0;
            _direction = 0;
        }
    }
    _oldState = newState;
}

void IRAM_ATTR KY040Encoder::handleButtonISR()
{
    unsigned long now = millis();
    if (digitalRead(_swPin) == LOW)
    {
        if (!_pressed && (now - _lastButtonMs > 100))
        {
            _pressed = true;
            _pressStartMs = now;
        }
    }
    else
    {
        if (_pressed && !_longPressFired)
            _buttonPending = true;
        _pressed = false;
        _longPressFired = false;
        _lastButtonMs = now;
    }
}
