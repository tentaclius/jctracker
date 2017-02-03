#ifndef MIDIMESSAGE_H
#define MIDIMESSAGE_H

#include <jack/jack.h>
#include <jack/midiport.h>

/* struct MidiMessage */
struct MidiMessage
{
   jack_port_t    *port;
   jack_nframes_t  time;
   int             len;

   unsigned char   data[3];

   /* Construct the message with the given midi data. */
   MidiMessage(int b0, int b1, int b2, jack_nframes_t tm, unsigned channel = 0, jack_port_t *p = NULL);

   /* Create the message with null data */
   MidiMessage();
};

#endif
