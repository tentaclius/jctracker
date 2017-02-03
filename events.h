#ifndef EVENTS_H
#define EVETNS_H

#include <list>

#include "jackengine.h"
#include "sequencer.h"


/*******************************************************************************************/
/* A structure to return from a virtual function of an Event. */
struct ControlFlow
{
   bool bTakesTime : 1;
   bool bSilencePrevious : 1;
   bool bNeedsStopping : 1;
};

/*******************************************************************************************/
/* A parent of all possible tracker events. */
struct Event
{
   unsigned column;
   virtual ~Event() {}

   virtual ControlFlow execute(JackEngine *jack, Sequencer *seq);
   virtual void stop(JackEngine *jack, Sequencer *seq);
};

/*******************************************************************************************/
/* Silent note or pause. */
struct SkipEvent : public Event
{
   SkipEvent(unsigned col);
   ~SkipEvent();
};

/*******************************************************************************************/
/* For visual separation and changing note size. */
struct BarEvent : public Event
{
   unsigned nom, div;
   
   BarEvent(unsigned n, unsigned d);
   BarEvent(unsigned n, unsigned d, unsigned pitch);
   ~BarEvent();
};

/*******************************************************************************************/
/* Tempo changing command. */
struct TempoEvent : public Event
{
   unsigned tempo;

   TempoEvent(unsigned t);
   ~TempoEvent();
};

/*******************************************************************************************/
/* The previous note will not be muted. */
struct PedalEvent : public Event
{
   Event *event;

   PedalEvent(unsigned c, Event *anEvent);

   ~PedalEvent();
};

/*******************************************************************************************/
/* Beginning of a loop. */
struct LoopEvent : public Event
{
   unsigned count;

   LoopEvent(unsigned n);
   LoopEvent();
};

/*******************************************************************************************/
/* End of the loop. */
struct EndLoopEvent : public Event
{};

/*******************************************************************************************/
/* Start of a nested pattern definition. */
struct SubpatternBeginEvent : public Event
{
   std::string name;

   SubpatternBeginEvent(std::string aName);
};

/*******************************************************************************************/
/* End of a nested pattern definition. */
struct SubpatternEndEvent : public Event
{};

struct SubpatternPlayEvent : public Event
{
   Sequencer *sequencer;

   SubpatternPlayEvent(Sequencer *aSequencer, unsigned aColumn);
};

/*******************************************************************************************/
/* Skip a number of turns. */
struct WaitEvent : public Event
{
   size_t number;

   WaitEvent(size_t aNumber) : number(aNumber);
};

typedef std::list<Event*> EventListT;


#endif
