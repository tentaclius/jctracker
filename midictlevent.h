#ifndef MIDICTLEVENT_H
#define MIDICTLEVENT_H

#include <iostream>

#include "events.h"
#include "midimessage.h"

/*******************************************************************************************/
/* A message to a midi controller. */
struct MidiCtlEvent : public Event
{
   enum {CTLTYPE_CONTROL, CTLTYPE_PITCHBEND} type;
   unsigned controller;
   unsigned value;
   unsigned initValue;
   unsigned step;
   double time;
   double delay;
   double delayDiv;

   MidiCtlEvent();

   /* Construct the control by parsing the string. */
   MidiCtlEvent(const std::string &str, unsigned clmn = 0);

   /* Generate a MIDI message that corresponds to the object. */
   MidiMessage midiMsg(jack_nframes_t time, unsigned value, unsigned channel, jack_port_t *port);
};

#endif
