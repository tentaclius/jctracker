#include "events.h"

#include <stdio.h>
#include <assert.h>
#include <algorithm>

#include "sequencer.h"

/*****************************************************************************************************/
/* Event. */
ControlFlow Event::execute(JackEngine *jack, Sequencer *seq)
{
   return {false, false, false};
}
void Event::stop(JackEngine *jack, Sequencer *seq)
{
}
void Event::sustain(JackEngine *jack, Sequencer *seq)
{
}

/*****************************************************************************************************/
/* SkipEvent. */
SkipEvent::SkipEvent(unsigned col)
{
   column = col;
}
SkipEvent::~SkipEvent() {}

ControlFlow SkipEvent::execute(JackEngine *jack, Sequencer *seq)
{
   trace("skip event col%x\n", column);
   return {true, true, false};
}

/*****************************************************************************************************/
/* BarEvent. */
BarEvent::BarEvent(unsigned n, unsigned d) : nom(n), div(d)
{}
BarEvent::BarEvent(unsigned n, unsigned d, unsigned pitch) : nom(n), div(d)
{}
BarEvent::~BarEvent()
{}

ControlFlow BarEvent::execute(JackEngine *jack, Sequencer *seq)
{
   if (nom > 0)
      seq->setQuant(nom);

   return {false, false, false};
}

/*****************************************************************************************************/
/* Tempo changing command. */
TempoEvent::TempoEvent(unsigned t) : tempo(t) {}
TempoEvent::~TempoEvent() {}

ControlFlow TempoEvent::execute(JackEngine *jack, Sequencer *seq)
{
   seq->setTempo(tempo);
   return {false, false, false};
}

/*****************************************************************************************************/
/* PedalEvent. The previous note will not be muted. */
PedalEvent::PedalEvent(unsigned c, Event *anEvent)
{
   assert(anEvent != NULL);
   column = c;
   event = anEvent;
}
PedalEvent::~PedalEvent() {}

ControlFlow PedalEvent::execute(JackEngine *jack, Sequencer *seq)
{
   trace("pedal event col%x\n", column);
   event->sustain(jack, seq);
   return {true, false, false};
}

/*****************************************************************************************************/
/* Beginning of a loop. */
LoopEvent::LoopEvent(unsigned n)
{
   count = n;
}
LoopEvent::LoopEvent()
{
   count = (unsigned)-1;
}

/*****************************************************************************************************/
/* SubpatternBeginEvent. Start of a nested pattern definition. */
SubpatternBeginEvent::SubpatternBeginEvent(std::string aName)
{
   name = aName;
}

/*****************************************************************************************************/
/* Plays a nested pattern. */
SubpatternPlayEvent::SubpatternPlayEvent(Sequencer *aSequencer, unsigned aColumn)
{
   assert(aSequencer != NULL);
   sequencer = aSequencer;
   column = aColumn;
}

ControlFlow SubpatternPlayEvent::execute(JackEngine *jack, Sequencer *seq)
{
   trace("subpattern play col%x\n", column);
   sequencer->setCurrentTime(seq->getCurrentTime());
   sequencer->initPosition();
   sequencer->playNextLine();
   return {true, true, true};
}

void SubpatternPlayEvent::stop(JackEngine *jack, Sequencer *seq)
{
   trace("subpattern stop col%x\n", column);
   sequencer->setCurrentTime(seq->getCurrentTime());
   sequencer->silence();
}

void SubpatternPlayEvent::sustain(JackEngine *jack, Sequencer *seq)
{
   trace("subpattern sustain col%x\n", column);
   sequencer->setCurrentTime(seq->getCurrentTime());
   sequencer->playNextLine();
}

/*****************************************************************************************************/
/* A message to skip a number of turns. */
WaitEvent::WaitEvent(size_t aNumber) : number(aNumber) {}

ControlFlow WaitEvent::execute(JackEngine *jack, Sequencer *seq)
{
   std::vector<EventListT> activeNotesVec (seq->getActiveNotes());

   for (unsigned i = 0; i < number; i ++)
      for (EventListT chEvents : activeNotesVec)
         for (Event *e : chEvents)
         {
            e->sustain(jack, seq);
            seq->advanceTime(jack->msToNframes(60 * 1000 / seq->getTempo() / seq->getQuant()));
         }

   return {false, false, false};
}
