#include "midimessage.h"

#include <string.h>

/***************************************************/
/* Construct the message with the given midi data. */
MidiMessage::MidiMessage(int b0, int b1, int b2, jack_nframes_t tm, unsigned channel, jack_port_t *p)
{
   if (b0 >= 0x80 && b0 <= 0xEF)
   {
      b0 &= 0xF0;
      b0 += channel;       // Channel.
   }

   if (b1 == -1)
   {
      len = 1;
      data[0] = b0;
   }
   else if (b2 == -1)
   {
      len = 2;
      data[0] = b0;
      data[1] = b1;
   }
   else
   {
      len = 3;
      data[0] = b0;
      data[1] = b1;
      data[2] = b2;
   }

   time = tm;
   port = p;
}

/***************************************************/
/* Create the message with null data */
MidiMessage::MidiMessage()
{
   memset(data, 0, 3);
   len = 0;
   time = 0;
}
