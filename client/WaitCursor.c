/* Cooperative animation: no interrupts, timers, or allocation needed. */
#include <QuickDraw.h>
#include <Events.h>
#include <stdint.h>
#include "WaitCursor.h"
#include "WaitCursorFrames.h"

static short waitDepth;
static unsigned char phase;
static uint32_t frameStart;

void UpdateNetworkWaitCursor(void)
{
    uint32_t now, elapsed;
    if (!waitDepth) return;
    now = (uint32_t)TickCount();
    elapsed = now - frameStart; /* Unsigned subtraction survives clock wrap. */
    if (elapsed >= kWireCycleTicks) {
        elapsed %= kWireCycleTicks;
        frameStart = now - elapsed;
    }
    while (elapsed >= kWireDuration[phase]) {
        elapsed -= kWireDuration[phase];
        frameStart += kWireDuration[phase];
        phase = (phase + 1) % kWireSequenceLength;
    }
    /* Reassert it after event handlers that may have set the arrow/I-beam. */
    SetCursor(&kWireCursors[kWireSequence[phase]]);
}

void BeginNetworkWait(void)
{
    if (!waitDepth) {
        phase = 0;
        frameStart = (uint32_t)TickCount();
    }
    ++waitDepth;
    UpdateNetworkWaitCursor();
}

void EndNetworkWait(void)
{
    if (waitDepth > 0 && !--waitDepth) InitCursor();
}
