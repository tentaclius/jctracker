#ifndef NOTEEVENT_H
#define NOTEEVENT_H

#include <iostream>

#include <stdint.h>

#include "events.h"

/*******************************************************************************************/
/* A note to be played. */
struct NoteEvent : public Event
{
   unsigned pitch;      // The pitch of the note.
   unsigned volume;     // The volume.
   
   double   delay;      // Delay time for the note in microseconds.
   double   time;       // Time of the note playing in microseconds.
   double   partDelay;  // Delay in parts of the current note.
   double   partTime;   // Playing time in parts of the current note.
   double   partDiv;

   bool     natural;    // If the note is of natural tone.
   bool     endless;    // Do not send NOTE_OFF for this note.

   /* Parametrized constructor. */
   NoteEvent(unsigned n, unsigned v, uint64_t tm, uint64_t dl, unsigned col);

   /* Empty constructor. */
   NoteEvent();

   /* Constructor that parses the note from the string. */
   NoteEvent(const std::string &buf, unsigned aColumn = 0);

   /* Destructor. */
   ~NoteEvent();

   /* Set the parameters of the current midi message. */
   void set(unsigned n, unsigned v, uint64_t tm, uint64_t dl);

   /* Return a new instance of the same data. */
   NoteEvent* clone();

   /***************************************************/
   /* Virtual functions to start/stop the note. */
   void stop(JackEngine *jack, Sequencer *seq);
   ControlFlow execute(JackEngine *jack, Sequencer *seq);
};

#endif
