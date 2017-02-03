#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>


bool gPlaying = false;

#define MIDI_NOTE_ON                   0x90
#define MIDI_NOTE_OFF                  0x80
#define MIDI_PROGRAM_CHANGE            0xC0
#define MIDI_CONTROLLER                0xB0
#define MIDI_RESET                     0xFF
#define MIDI_HOLD_PEDAL                64
#define MIDI_ALL_SOUND_OFF             0x7B
#define MIDI_ALL_MIDI_CONTROLLERS_OFF  121
#define MIDI_ALL_NOTES_OFF             123
#define MIDI_BANK_SELECT_MSB           0
#define MIDI_BANK_SELECT_LSB           32
#define MIDI_PITCH_BEND                0xE0

/*******************************************************************************************/
/* TRACE */
#ifdef DEBUG
#define trace(...) {TRACE(__FILE__, __LINE__,  __VA_ARGS__);}
void TRACE(const char *file, int line, const char *fmt, ...)
{
   va_list argP;
   va_start(argP, fmt);

   if (fmt)
   {
      fprintf(stderr, "TRACE(%s:%d): ", file, line);
      vfprintf(stderr, fmt, argP);
   }

   va_end(argP);
}
#else
#define trace(...) {}
#endif

#endif
