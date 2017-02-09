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
bool Sequencer::playNextLine()
{
   std::vector<EventListT> nextActives (mActiveNotesVec.size());

   bool bAdvanceTime = false;

   while (!bAdvanceTime)
   {
      EventListT eventLst = getNextLine();
      if (eventLst.empty())
         return false;

      // Start new notes. Loop through the event list.
      for (EventListT::iterator jt = eventLst.begin(); jt != eventLst.end(); jt ++)
      {
         trace("current time: %llu\n", (long long unsigned)mCurrentTime);

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
void Sequencer::silence()
{
   for (std::vector<EventListT>::iterator it = mActiveNotesVec.begin();
         it != mActiveNotesVec.end(); it ++)
   {
      while (!(*it).empty())
      {
         (*it).front()->stop(mJack, this);
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
