#pragma once

typedef enum _FIRST_CLICK_PHASE {
    FirstClickNative,
    FirstClickWaitingForLift,
    FirstClickReleased,
    FirstClickPressed,
    FirstClickFault
} FIRST_CLICK_PHASE;

static __inline FIRST_CLICK_PHASE
FirstClickNextPhase(FIRST_CLICK_PHASE Phase, unsigned char Threshold,
    unsigned char Pressure, int HasContact)
{
    if (!HasContact) return FirstClickReleased;
    if (Phase == FirstClickReleased && Pressure >= Threshold) return FirstClickPressed;
    if (Phase == FirstClickPressed && Pressure <= (Threshold * 2) / 3) return FirstClickReleased;
    return Phase;
}
