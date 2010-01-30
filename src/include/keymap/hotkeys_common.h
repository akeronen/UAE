 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Default hotkey mapping
  *
  * Copyright 2004-2006 Richard Drummond
  */

#ifndef HOTKEY_MODIFIER
#define HOTKEY_MODIFIER   RAWKEY_F12
#endif

#undef DEFAULT_HOTKEYSEQ2
#undef DEFAULT_HOTKEYSEQ3

#ifndef HOTKEY_MODIFIER2
#define DEFAULT_HOTKEYSEQ2(key2, event)       MAKE_HOTKEYSEQ (HOTKEY_MODIFIER, (key2), -1, -1,    event)
#define DEFAULT_HOTKEYSEQ3(key2, key3, event) MAKE_HOTKEYSEQ (HOTKEY_MODIFIER, (key2),(key3), -1, event)
#else
#define DEFAULT_HOTKEYSEQ2(key2, event)       MAKE_HOTKEYSEQ (HOTKEY_MODIFIER, HOTKEY_MODIFIER2, (key2), -1, event)
#define DEFAULT_HOTKEYSEQ3(key2, key3, event) MAKE_HOTKEYSEQ (HOTKEY_MODIFIER, HOTKEY_MODIFIER2, (key2), (key3), event)
#endif

#define DEFAULT_HOTKEYS \
\
     DEFAULT_HOTKEYSEQ2 (RAWKEY_Q,            INPUTEVENT_SPC_QUIT)}, \
    {DEFAULT_HOTKEYSEQ2 (RAWKEY_R,            INPUTEVENT_SPC_SOFTRESET)}, \
    {DEFAULT_HOTKEYSEQ3 (RAWKEY_LEFT_SHIFT, RAWKEY_R, INPUTEVENT_SPC_HARDRESET)}, \
    {DEFAULT_HOTKEYSEQ2 (RAWKEY_D,            INPUTEVENT_SPC_ENTERDEBUGGER)}, \
    {DEFAULT_HOTKEYSEQ2 (RAWKEY_S,            INPUTEVENT_SPC_TOGGLEFULLSCREEN)}, \
    {DEFAULT_HOTKEYSEQ2 (RAWKEY_G,            INPUTEVENT_SPC_TOGGLEMOUSEGRAB)}, \
    {DEFAULT_HOTKEYSEQ2 (RAWKEY_I,            INPUTEVENT_SPC_INHIBITSCREEN)}, \
    {DEFAULT_HOTKEYSEQ2 (RAWKEY_P,            INPUTEVENT_SPC_SCREENSHOT)}, \
    {DEFAULT_HOTKEYSEQ2 (RAWKEY_A,            INPUTEVENT_SPC_SWITCHINTERPOL)}, \
    {DEFAULT_HOTKEYSEQ2 (RAWKEY_NUMPAD_PLUS,  INPUTEVENT_SPC_INCREASE_REFRESHRATE)}, \
    {DEFAULT_HOTKEYSEQ2 (RAWKEY_NUMPAD_MINUS, INPUTEVENT_SPC_DECREASE_REFRESHRATE)}, \
\
    {DEFAULT_HOTKEYSEQ2 (RAWKEY_F1,                    INPUTEVENT_SPC_FLOPPY0)}, \
    {DEFAULT_HOTKEYSEQ2 (RAWKEY_F2,                    INPUTEVENT_SPC_FLOPPY1)}, \
    {DEFAULT_HOTKEYSEQ2 (RAWKEY_F3,                    INPUTEVENT_SPC_FLOPPY2)}, \
    {DEFAULT_HOTKEYSEQ2 (RAWKEY_F4,                    INPUTEVENT_SPC_FLOPPY3)}, \
    {DEFAULT_HOTKEYSEQ3 (RAWKEY_LEFT_SHIFT, RAWKEY_F1, INPUTEVENT_SPC_EFLOPPY0)}, \
    {DEFAULT_HOTKEYSEQ3 (RAWKEY_LEFT_SHIFT, RAWKEY_F2, INPUTEVENT_SPC_EFLOPPY1)}, \
    {DEFAULT_HOTKEYSEQ3 (RAWKEY_LEFT_SHIFT, RAWKEY_F3, INPUTEVENT_SPC_EFLOPPY2)}, \
    {DEFAULT_HOTKEYSEQ3 (RAWKEY_LEFT_SHIFT, RAWKEY_F4, INPUTEVENT_SPC_EFLOPPY3)}, \
    {DEFAULT_HOTKEYSEQ2 (RAWKEY_ENTER,                 INPUTEVENT_SPC_ENTERGUI)}, \
    {DEFAULT_HOTKEYSEQ2 (RAWKEY_F,                     INPUTEVENT_SPC_FREEZEBUTTON)}, \
\
    {DEFAULT_HOTKEYSEQ2 (RAWKEY_F5,                    INPUTEVENT_SPC_STATERESTOREDIALOG)}, \
    {DEFAULT_HOTKEYSEQ3 (RAWKEY_LEFT_SHIFT, RAWKEY_F5, INPUTEVENT_SPC_STATESAVEDIALOG)}, \
\
    {DEFAULT_HOTKEYSEQ2 (RAWKEY_RIGHT_SHIFT,           INPUTEVENT_SPC_STATERESTORE)}, \
    {DEFAULT_HOTKEYSEQ3 (RAWKEY_NUMPAD_0, RAWKEY_NUMPAD_0, INPUTEVENT_SPC_STATESAVE)
