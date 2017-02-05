#include "events.h"

#include <sstream>

#include "common.h"
#include "jackengine.h"
#include "sequencer.h"

/*****************************************************************************************************/
/* Constructor. */
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

/*****************************************************************************************************/
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

/*****************************************************************************************************/
/* Virtual function to schedule NOTE ON. */
ControlFlow MidiCtlEvent::execute(JackEngine *jack, Sequencer *seq)
{
   ControlFlow ret = {true, true, false};

   PortMap pm = seq->getPortMap(column);

   if (initValue == (unsigned)-1 || time == 0 || value == initValue)
   {
      // This is a control message to the midi. Generate single event.
      jack->queueMidiEvent(midiMsg(
               seq->getCurrentTime() + (jack->msToNframes(60 * 1000 / seq->getTempo() / seq->getQuant()) * delay / delayDiv),
               value,
               pm.channel, pm.port));
   }
   else
   {
      // This is ramp. Need to generate a bunch of messages.
      unsigned timeStep = (jack->msToNframes(60 * 1000 / seq->getTempo() / seq->getQuant()) * time / delayDiv)
         / abs((int)initValue - value);
      for (unsigned i = initValue;
            (value > initValue) ? (i < value) : (i > value);
            i += (value > initValue ? step : - step))
      {
         jack->queueMidiEvent(midiMsg(
                  seq->getCurrentTime() + (jack->msToNframes(60 * 1000 / seq->getTempo() / seq->getQuant()) * delay / delayDiv)
                  + timeStep * abs((int)initValue - i),
                  i,
                  pm.channel, pm.port));
      }
      jack->queueMidiEvent(midiMsg(
               seq->getCurrentTime() + (jack->msToNframes(60 * 1000 / seq->getTempo() / seq->getQuant()) * delay / delayDiv)
               + timeStep * abs((int)initValue - value),
               value,
               pm.channel, pm.port));
   }

   return ret;
}
