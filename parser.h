#ifndef PARSER_H
#define PARSER_H

#include <vector>
#include <map>
#include <list>

#include <stdlib.h>

#include <jack/jack.h>
#include <jack/midiport.h>

#include "events.h"
#include "noteevent.h"


/* Forward declaration. */
class Sequencer;

/*******************************************************************************************/
/* A structure to associate a port and a channel to a column. */
struct PortMap
{
   unsigned channel;
   jack_port_t *port;

   PortMap(unsigned ch, jack_port_t *p);
   PortMap();
};

/*******************************************************************************************/
/* Parse an input line. */
class Parser
{
   size_t                  mChannelNum;
   std::vector<Event*>     mLastNote;
   NoteEvent               mDfltNote;
   unsigned                mVolume;
   std::vector<int>       *mSigns;
   std::map<std::string, std::string>
                           mAliases;
   std::vector<PortMap>    mColumnMap;
   int                     mTranspose;
   size_t                  mLinePos;
   std::map<std::string, Sequencer*> 
      *mSubSeqMap;

   private:
   /* Remove spaces at the beginning and the end of the string */
   std::string trim(std::string s);

   public:
   /* Create the parser. */
   Parser(std::map<std::string, Sequencer*> *subseq, size_t chan = 64);

   /* Destructor. */
   ~Parser();

   void setSubseqMap(std::map<std::string, Sequencer*> *subseq);

   /* Parse a given line (with one or multiple directives or patterns). */
   EventListT parseLine(std::string line);

   /* Return a port to which the column matches. */
   PortMap& getPortMap(unsigned column);
};

#endif
