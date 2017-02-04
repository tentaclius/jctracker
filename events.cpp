#include "events.h"

#include <stdio.h>
#include <assert.h>

/*****************************************************************************************************/
/* Event. */
ControlFlow Event::execute(JackEngine *jack, Sequencer *seq)
{
   return {false, false};
}
void Event::stop(JackEngine *jack, Sequencer *seq)
{
}

/*****************************************************************************************************/
/* SkipEvent. */
SkipEvent::SkipEvent(unsigned col)
{
   column = col;
}
SkipEvent::~SkipEvent() {}

/*****************************************************************************************************/
/* BarEvent. */
BarEvent::BarEvent(unsigned n, unsigned d) : nom(n), div(d)
{}
BarEvent::BarEvent(unsigned n, unsigned d, unsigned pitch) : nom(n), div(d)
{}
BarEvent::~BarEvent()
{}

/*****************************************************************************************************/
/* Tempo changing command. */
TempoEvent::TempoEvent(unsigned t) : tempo(t) {}
TempoEvent::~TempoEvent() {}

/*****************************************************************************************************/
/* PedalEvent. The previous note will not be muted. */
PedalEvent::PedalEvent(unsigned c, Event *anEvent)
{
   assert(anEvent != NULL);
   column = c;
   event = anEvent;
}
PedalEvent::~PedalEvent() {}

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

/*****************************************************************************************************/
/* A message to skip a number of turns. */
WaitEvent::WaitEvent(size_t aNumber) : number(aNumber) {}
