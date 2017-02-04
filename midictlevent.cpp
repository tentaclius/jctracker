#include "events.h"

#include <sstream>

#include "common.h"

MidiCtlEvent::MidiCtlEvent()
{
   type = CTLTYPE_CONTROL;
   column = 0;
   controller = 0;

   value = 0;
   initValue = (unsigned)-1;
   step = 1;

   time = 0;
   delay = 0;
   delayDiv = 1;
}

/*****************************************************************************************************/
/* Construct the control by parsing the string. */
MidiCtlEvent::MidiCtlEvent(const std::string &str, unsigned clmn)
{
   type = CTLTYPE_CONTROL;
   column = clmn;
   controller = 0;

   value = 0;
   initValue = (unsigned)-1;
   step = 1;

   time = 0;
   delay = 0;
   delayDiv = 1;

   if (str.front() != '$')
      throw 0;

   std::istringstream iss (str.substr(1));

   // Check if this is a special case of Pitch Bend.
   if (str.substr(0, 3) == "$pb")
   {
      type = CTLTYPE_PITCHBEND;
      iss.seekg(2);
   }
   else
   {
      if (!(iss >> controller))
         throw (int)iss.tellg();
   }

   // Read the initial value and throw the position in case of parsing error.
   if (iss.get() != '=' || !(iss >> initValue))
      throw (int)iss.tellg();

   // Skip "..".
   while (iss.peek() == '.')
      iss.get();

   // If there are no second value, set the value to the initial.
   if (!(iss >> value))
      value = initValue;

   // Skip "..".
   while (iss.peek() == '.')
      iss.get();

   // Try to read step value.
   iss.clear();
   iss >> step;

   char c;
   while ((c = iss.get()) != EOF)
   {
      switch (c)
      {
         case ':':
            iss >> time;
            break;
         case '+':
            iss >> delay;
            break;
         case '/':
            iss >> delayDiv;
            break;
      }
   }
}

/***************************************************/
/* Generate a MIDI message that corresponds to the object. */
MidiMessage MidiCtlEvent::midiMsg(jack_nframes_t time, unsigned value, unsigned channel, jack_port_t *port)
{
   unsigned b0, b1, b2;

   switch (type)
   {
      case CTLTYPE_PITCHBEND:
         b0 = MIDI_PITCH_BEND;
         b1 = 0b01111111 & value;
         b2 = 0b01111111 & (value >> 7);
         break;

      case CTLTYPE_CONTROL:
         b0 = MIDI_CONTROLLER;
         b1 = controller;
         b2 = value;
   }

   return MidiMessage(b0, b1, b2, time, channel, port);
}
