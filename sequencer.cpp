#include <algorithm>

#include "sequencer.h"
#include "parser.h"
#include "events.h"
#include "noteevent.h"
#include "midictlevent.h"

/*****************************************************************************************************/
/* Constructor. */
Sequencer::Sequencer(JackEngine *j)
{
   mJack = j;
   mCurrentPos = 0;
   mCurrentTime = mJack->currentFrameTime();
   mTempo = 100;
   mQuantSize = 4;
   mParser = new Parser(&mSubSeqMap);
}

/*****************************************************************************************************/
/* Destructor. */
Sequencer::~Sequencer()
{
   delete mParser;
}

/*****************************************************************************************************/
/* Read a pattern from a stringstream. */
void Sequencer::readFromStream(std::istream &ss)
{
   std::string line;

   while (std::getline(ss, line))
   {
      try
      {
         EventListT lst = mParser->parseLine(line);

         // Continue if the event list is empty.
         if (lst.empty())
            continue;

         // Check wether this is a beginning of a nested sequence.
         {
            SubpatternBeginEvent *e = dynamic_cast<SubpatternBeginEvent*>(lst.front());
            if (e != NULL)
            {
               Sequencer *seq = new Sequencer(mJack);
               seq->readFromStream(ss);
               mSubSeqMap[e->name] = seq;
               continue;
            }
         }

         // Check if the end of a nested sequence.
         {
            SubpatternEndEvent *e = dynamic_cast<SubpatternEndEvent*>(lst.front());
            if (e != NULL)
               break;
         }

         // A lower level event. Leave it for runtime.
         mSong.push_back(lst);
      }
      catch (int e)
      {
         std::cerr << "Cannot parse line: " << line << std::endl;
      }
   }
}

/*****************************************************************************************************/
/* Play one line and increment the internal position. */
bool Sequencer::playNextLine(jack_nframes_t aCurrentTime)
{
   std::vector<EventListT> nextActives (mActiveNotesVec.size());

   bool bAdvanceTime = false;

   if (aCurrentTime != 0)
      mCurrentTime = aCurrentTime;

   trace("current time: %llu\n", (long long unsigned)mCurrentTime);

   while (!bAdvanceTime)
   {
      EventListT eventLst = getNextLine();
      if (eventLst.empty())
         return false;

      // Start new notes. Loop through the event list.
      for (EventListT::iterator jt = eventLst.begin(); jt != eventLst.end(); jt ++)
      {
         Event *event = *jt;

         // Execute the event.
         ControlFlow type = event->execute(mJack, this);

         // Expand active notes vector if the channel number is bigger.
         if ((type.bNeedsStopping || type.bSilencePrevious) && event->column >= nextActives.size())
         {
            mActiveNotesVec.resize(event->column + 1);
            nextActives.resize(event->column + 1);
         }

         // If the event needs to be stopped at the next line, add it to the list.
         if (type.bNeedsStopping)
            nextActives[event->column].push_back(event);

         // Stop previous note(s) on this channel.
         if (type.bSilencePrevious)
         {
            for (Event *e : mActiveNotesVec[event->column])
               e->stop(mJack, this);
            mActiveNotesVec[event->column].clear();
         }

         bAdvanceTime |= type.bTakesTime;
      }

      // Merge active note lists.
      for (size_t i = 0; i < mActiveNotesVec.size(); i ++)
      {
         mActiveNotesVec[i].insert(mActiveNotesVec[i].end(),
               nextActives[i].begin(), nextActives[i].end());
      }

      // Advance the current time.
      if (bAdvanceTime)
         mCurrentTime += mJack->msToNframes(60 * 1000 / mTempo / mQuantSize);
   }

   return true;
}
/*{
   if (aCurrentTime != 0)
      mCurrentTime = aCurrentTime;

   EventListT eventLst = getNextLine();
   if (eventLst.empty())
      return false;

   // The time should be advanced if there is a time taking event in the list.
   bool bAdvanceTime = false;

   // Check if we have a special command.
   {  // Tempo change.
      TempoEvent *e = dynamic_cast<TempoEvent*>(eventLst.front());
      if (e != NULL)
      {
         mTempo = e->tempo;
         return playNextLine();
      }
   }

   {  // Note's size change.
      BarEvent *e = dynamic_cast<BarEvent*>(eventLst.front());
      if (e != NULL)
      {
         if (e->nom > 0)
            mQuantSize = e->nom;
         return playNextLine();
      }
   }

   {  // Skip n turns.
      WaitEvent *e = dynamic_cast<WaitEvent*>(eventLst.front());
      if (e != NULL)
      {
         // Collect a list of Subpatterns.
         std::list<SubpatternPlayEvent*> patternList;

         for (std::vector<EventListT>::iterator channelIt = mActiveNotesVec.begin();
               channelIt != mActiveNotesVec.end();
               channelIt ++)
         {
            EventListT eventList = *channelIt;
            for (EventListT::iterator eventIt = eventList.begin();
                  eventIt != eventList.end();
                  eventIt ++)
            {
               SubpatternPlayEvent *p = dynamic_cast<SubpatternPlayEvent*>(*eventIt);
               if (p != NULL)
                  patternList.push_back(p);
            }
         }

         // Execute the subpatterns n times.
         for (unsigned i = 0; i < e->number; i ++)
         {
            for_each(patternList.begin(), patternList.end(), [&](SubpatternPlayEvent *p) {
                  p->sequencer->playNextLine(mCurrentTime); });

            mCurrentTime += mJack->msToNframes(60 * 1000 / mTempo / mQuantSize);
         }
         return playNextLine();
      }
   }

   // Start new notes. Loop through the event list.
   for (EventListT::iterator jt = eventLst.begin(); jt != eventLst.end(); jt ++)
   {
      unsigned stopChannel = (unsigned)-1;
      EventListT nextActives;

      //===================================
      // Check what kinf of event we have.
      {  // A regular note.
         NoteEvent *e = dynamic_cast<NoteEvent*>(*jt);
         if (e != NULL)
         {
            bAdvanceTime = true;

            // Mark the column on which we need to mute previous notes.
            stopChannel = e->column;

            // Queue the note on event.
            mJack->queueMidiEvent(MIDI_NOTE_ON, e->pitch, e->volume,
                  mCurrentTime + mJack->msToNframes(e->delay)
                  + (e->partDiv != 0 ? (mJack->msToNframes(60 * 1000 / mTempo / mQuantSize) * e->partDelay / e->partDiv) : 0)
                  + e->column,
                  mParser->getPortMap(e->column).channel, mParser->getPortMap(e->column).port);

            if (!e->endless)
            {
               if (e->time == 0 && e->partTime == 0)
                  // If the note does not have specific sound time, turn it off at the next cycle.
                  nextActives.push_back(*jt);
               else
                  // If the note has specific time, schedule the off event right now.
                  mJack->queueMidiEvent(MIDI_NOTE_OFF, e->pitch, e->volume,
                        mCurrentTime + mJack->msToNframes(e->delay)
                        + (e->partDiv != 0 ? (mJack->msToNframes(60 * 1000 / mTempo / mQuantSize)
                              * e->partDelay / e->partDiv) : 0)
                        + mJack->msToNframes(e->time)
                        + (e->partDiv != 0 ? (mJack->msToNframes(60 * 1000 / mTempo / mQuantSize)
                              * e->partTime / e->partDiv) : 0) - 2,
                        mParser->getPortMap(e->column).channel, mParser->getPortMap(e->column).port);
            }
         }
      }

      {  // Midi control.
         MidiCtlEvent *e = dynamic_cast<MidiCtlEvent*>(*jt);
         if (e != NULL)
         {
            stopChannel = e->column;

            if (e->initValue == (unsigned)-1 || e->time == 0 || e->value == e->initValue)
            {
               // This is a control message to the midi. Generate single event.
               mJack->queueMidiEvent(e->midiMsg(
                        mCurrentTime + (mJack->msToNframes(60 * 1000 / mTempo / mQuantSize) * e->delay / e->delayDiv),
                        e->value,
                        mParser->getPortMap(e->column).channel, mParser->getPortMap(e->column).port));
            }
            else
            {
               // This is ramp. Need to generate a bunch of messages.
               unsigned timeStep = (mJack->msToNframes(60 * 1000 / mTempo / mQuantSize) * e->time / e->delayDiv)
                  / abs((int)e->initValue - e->value);
               for (unsigned i = e->initValue;
                     (e->value > e->initValue) ? (i < e->value) : (i > e->value);
                     i += (e->value > e->initValue ? e->step : - e->step))
               {
                  mJack->queueMidiEvent(e->midiMsg(
                           mCurrentTime + (mJack->msToNframes(60 * 1000 / mTempo / mQuantSize) * e->delay / e->delayDiv)
                           + timeStep * abs((int)e->initValue - i),
                           i,
                           mParser->getPortMap(e->column).channel, mParser->getPortMap(e->column).port));
               }
               mJack->queueMidiEvent(e->midiMsg(
                        mCurrentTime + (mJack->msToNframes(60 * 1000 / mTempo / mQuantSize) * e->delay / e->delayDiv)
                        + timeStep * abs((int)e->initValue - e->value),
                        e->value,
                        mParser->getPortMap(e->column).channel, mParser->getPortMap(e->column).port));
            }
         }
      }

      {
         // Skip a bit. Silence the previous notes.
         SkipEvent *e = dynamic_cast<SkipEvent*>(*jt);
         if (e != NULL)
         {
            bAdvanceTime = true;
            stopChannel = e->column;
         }
      }

      {
         // Just a sign that the note(s) on the channel should not be silenced.
         PedalEvent *e = dynamic_cast<PedalEvent*>(*jt);
         if (e != NULL)
         {
            bAdvanceTime = true;

            SubpatternPlayEvent *pattern = dynamic_cast<SubpatternPlayEvent*>(e->event);
            if (pattern != NULL)
            {
               pattern->sequencer->setCurrentTime(mCurrentTime);
               pattern->sequencer->playNextLine();
            }
         }
      }

      {
         // This is a beginning of a nested pattern.
         SubpatternPlayEvent *e = dynamic_cast<SubpatternPlayEvent*>(*jt);
         if (e != NULL)
         {
            bAdvanceTime = true;
            stopChannel = e->column;

            e->sequencer->initPosition();
            e->sequencer->playNextLine();
            nextActives.push_back(*jt);
         }
      }
      //========================
      // End of events block.

      // Stop previous note(s) on this channel. TODO: fix that if a pattern is empty the note does not get silenced.
      if (stopChannel != (unsigned)-1)
      {
         // Resize the channel vector if needed.
         if (stopChannel >= mActiveNotesVec.size())
            mActiveNotesVec.resize(stopChannel + 1);

         // Stop previous notes on this channel.
         for (EventListT::iterator activeIt = mActiveNotesVec[stopChannel].begin();
               activeIt != mActiveNotesVec[stopChannel].end();
               activeIt ++)
         {
            // Stop a regular note.
            NoteEvent *note = dynamic_cast<NoteEvent*>(*activeIt);
            if (note != NULL)
               mJack->queueMidiEvent(MIDI_NOTE_OFF, note->pitch, 0, mCurrentTime - 1 - stopChannel,
                     mParser->getPortMap(stopChannel).channel, mParser->getPortMap(stopChannel).port);

            // Stop a pattern.
            SubpatternPlayEvent *seq = dynamic_cast<SubpatternPlayEvent*>(*activeIt);
            if (seq != NULL)
               seq->sequencer->silence(mCurrentTime);
         }

         mActiveNotesVec[stopChannel] = nextActives;
      }
   }

   // Advance the current time.
   if (bAdvanceTime)
      mCurrentTime += mJack->msToNframes(60 * 1000 / mTempo / mQuantSize);

   return true;
}*/

/*****************************************************************************************************/
/* Returns a list of events and increments internal position pointer. */
EventListT Sequencer::getNextLine()
{
   // Check if we reached the end. Return empty vector if so.
   if (mCurrentPos >= mSong.size())
      return EventListT ();

   LoopEvent     *lp = dynamic_cast<LoopEvent*>(mSong[mCurrentPos].front());
   EndLoopEvent *elp = dynamic_cast<EndLoopEvent*>(mSong[mCurrentPos].front());

   if (lp || elp)
   {
      if (lp != NULL)
         // The beginning of a loop; push the starting point to the loop stack.
         mLoopStack.push_back(std::pair<int, unsigned>(lp->count, mCurrentPos));

      else
      {
         // End of the loop; move the pointer to the beginning of the loop.
         if (mLoopStack.size() > 0)
         {
            if (mLoopStack.back().first == -1 || (-- mLoopStack.back().first) > 0)
               mCurrentPos = mLoopStack.back().second;
            else
               mLoopStack.pop_back();
         }
      }

      mCurrentPos ++;
      return getNextLine();
   }

   else
      return mSong[mCurrentPos ++];
}

/*****************************************************************************************************/
/* Set playing position to zero. */
void Sequencer::initPosition()
{
   mCurrentPos = 0;
}

/*****************************************************************************************************/
/* Return a column to port mapping. */
PortMap& Sequencer::getPortMap(unsigned column)
{
   return mParser->getPortMap(column);
}

/*****************************************************************************************************/
/* Set the current time frame. */
void Sequencer::setCurrentTime(jack_nframes_t time)
{
   mCurrentTime = time;
}

/*****************************************************************************************************/
/* Return the current time frame. */
jack_nframes_t Sequencer::getCurrentTime()
{
   return mCurrentTime;
}

/*****************************************************************************************************/
/* Getter for mTempo. */
unsigned Sequencer::getTempo()
{
   return mTempo;
}

/*****************************************************************************************************/
/* Setter for mTempo. */
void Sequencer::setTempo(unsigned t)
{
   mTempo = t;
}

/*****************************************************************************************************/
/* Getter for mQuantSize. */
unsigned Sequencer::getQuant()
{
   return mQuantSize;
}

/*****************************************************************************************************/
/* Setter for mQuantSize. */
void Sequencer::setQuant(unsigned q)
{
   mQuantSize = q;
}

/*****************************************************************************************************/
/* Silence currently active events. */
void Sequencer::silence(jack_nframes_t aCurrentTime)
{
   if (aCurrentTime != 0)
      mCurrentTime = aCurrentTime;

   for (std::vector<EventListT>::iterator it = mActiveNotesVec.begin();
         it != mActiveNotesVec.end(); it ++)
   {
      while (!(*it).empty())
      {
         NoteEvent *n = dynamic_cast<NoteEvent*>((*it).front());
         if (n != NULL)
            mJack->queueMidiEvent(MIDI_NOTE_OFF, n->pitch, 0, mCurrentTime - 1,
                  mParser->getPortMap(n->column).channel, mParser->getPortMap(n->column).port);

         SubpatternPlayEvent *s = dynamic_cast<SubpatternPlayEvent*>((*it).front());
         if (s != NULL)
            s->sequencer->silence();

         (*it).pop_front();
      }
   }
}

std::vector<EventListT>& Sequencer::getActiveNotes()
{
   return mActiveNotesVec;
}

void Sequencer::advanceTime(jack_nframes_t tm)
{
   mCurrentTime += tm;
}
