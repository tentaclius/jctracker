#ifndef SEQUENCER_H
#define SEQUENCER_H

#include <vector>
#include <map>

#include "common.h"
#include "events.h"
#include "parser.h"
#include "jackengine.h"

/*******************************************************************************************/
/* Interpret and process the pattern line by line. */
class Sequencer
{
   JackEngine *mJack;
   std::vector<EventListT>
               mSong;
   Parser     *mParser;
   size_t      mCurrentPos;
   std::list<std::pair<int, unsigned>>
               mLoopStack;

   std::map<std::string, Sequencer*>
               mSubSeqMap;
   std::vector<EventListT>
               mActiveNotesVec;
   jack_nframes_t
               mCurrentTime;
   unsigned    mTempo;
   unsigned    mQuantSize;

   public:
      /* Constructor. */
      Sequencer(JackEngine *j);

      /* Destructor. */
      ~Sequencer();

      /* Read the data from the stream. */
      void readFromStream(std::istream &ss);

      /* Get the vector of the next events. */
      EventListT getNextLine();

      /* Queue MIDI events from the current position of the sequencer. */
      bool playNextLine(jack_nframes_t aCurrentTime = 0);

      /* Stop all active notes. */
      void silence(jack_nframes_t aCurrentTime = 0);

      /* Reset sequencer position. */
      void initPosition();

      PortMap& getPortMap(unsigned column);

      /* Set the current time. */
      void setCurrentTime(jack_nframes_t time);

      /* Return current sequencer's time. */
      jack_nframes_t getCurrentTime();

      /* Advance current sequencer's time. */
      void advanceTime(jack_nframes_t tm);

      /* Get tempo. */
      unsigned getTempo();

      /* Set tempo. */
      void setTempo(unsigned t);

      /* Get quant size. */
      unsigned getQuant();

      /* Set quant size. */
      void setQuant(unsigned q);

      /* Get the vector of all active events. Vector's index corresponds to column. */
      std::vector<EventListT>& getActiveNotes();
};

#endif
