#include "parser.h"

#include <sstream>
#include <climits>

#include <assert.h>

#include "jackengine.h"
#include "events.h"
#include "midictlevent.h"

Parser::Parser(std::map<std::string, Sequencer*> *subseq, size_t chan)
{
   assert(subseq != NULL);

   mSubSeqMap = subseq;
   mSigns = new std::vector<int>(12, 0);
   mChannelNum = chan;
   mLastNote.resize(chan, NULL);
   mDfltNote.set(0,0,0,0);
   mTranspose = 0;
   mVolume = 64;
   mLinePos = 0;
}

Parser::~Parser()
{
   delete mSigns;
}

std::string Parser::trim(std::string s)
{
   if (s.empty())
      return s;   // Empty string.
   unsigned a = 0, b = s.length() - 1;

   while (a < b && isblank(s[a])) a ++;
   while (a < b && isblank(s[b])) b --;

   if (a == b) return "";
   else return s.substr(a, b - a + 1);
}

void Parser::setSubseqMap(std::map<std::string, Sequencer*> *subseq)
{
   mSubSeqMap = subseq;
}

EventListT Parser::parseLine(std::string line)
{
   JackEngine *jack = JackEngine::instance();

   mLinePos = 0;

   std::string chunk;          // A piece of the line to read the command.
   std::istringstream iss (line);
   EventListT eventList;

   // Return the empty list if the line is empty.
   if (line.length() == 0)
      return eventList;

   // A bar; find a number to identify the new size
   if (line[0] == '-')
   {
      BarEvent *b;
      unsigned i = 1;
      unsigned n = 0, d = 0;        // Nominator and divisor for the new size.
      char c = 0;

      // Skip all '-'.
      while (i < line.length() && line[i] == '-')
         i ++;

      std::istringstream barIss (line.substr(i));

      if (barIss >> n && barIss >> c && barIss >> d)
         b = new BarEvent(n, d);
      else
      {
         b = new BarEvent(0, 0);
         barIss.clear();
         barIss.seekg(0);
      }
      eventList.push_back(b);

      // Get the signs if any.
      while (barIss >> chunk)
      {
         int mod = INT_MAX;

         if (chunk[0] == '#')
            mod = +1;
         else if (chunk[0] == 'b' || chunk[0] == '&')
            mod = -1;
         else if (chunk[0] == 'n')
            mod = 0;

         // If the modifier is unchanged, then we should skip this.
         if (mod == INT_MAX)
            continue;

         NoteEvent n (chunk.substr(1));
         mSigns->at(n.pitch % 12) = mod;
      }

      return eventList;
   }

   // Process the line word by word next.
   iss >> chunk;

   // If this is a beginning of a sub-pattern.
   if (chunk == "define")
   {
      std::string name;
      if (iss >> name)
         eventList.push_back(new SubpatternBeginEvent(name));

      return eventList;
   }

   // End of sub-pattern definition.
   if (chunk == "end")
   {
      eventList.push_back(new SubpatternEndEvent());
      return eventList;
   }

   // Set the default note.
   if (chunk == "default")
   {
      iss >> chunk;
      try 
      {
         NoteEvent n (chunk);
         mDfltNote.set(n.pitch, n.volume, n.time, n.delay);
      }
      catch (int e)
      {
         throw (int)iss.tellg();
      }

      return eventList;
   }

   // Set the default volume.
   if (chunk == "volume")
   {
      iss >> mVolume;
      return eventList;
   }

   // Set the tempo.
   if (chunk == "tempo")
   {
      unsigned tempo;
      if (iss >> tempo)
         eventList.push_back(new TempoEvent(tempo));
      return eventList;
   }

   // Transposition.
   if (chunk == "transpose")
   {
      iss >> mTranspose;
      return eventList;
   }

   // Wait.
   if (chunk == "wait")
   {
      size_t n;
      iss >> n;
      eventList.push_back(new WaitEvent(n));
      return eventList;
   }

   // Register the port.
   if (chunk == "port")
   {
      unsigned columnA, columnB;    // Column number range to associate.
      std::string portName;         // The name of a port to create.
      unsigned channel;             // Channel number to use.
      std::string connClient;       // The client name to try to connect to.

      // Mandatory parameters.
      if (!(iss >> columnA))
         throw (int)iss.tellg();

      // Second column number is optional.
      if (!(iss >> columnB))
      {
         columnB = columnA;
         iss.clear();
      }

      // The port name is mandatory.
      if (!(iss >> portName))
         throw (int)iss.tellg();

      // Optional parameters.
      iss >> channel;

      // Create the port.
      jack_port_t *port = jack->registerOutputPort(portName);

      // Associate the columns.
      if (mColumnMap.size() < columnB)
         mColumnMap.resize(columnB);

      for (unsigned i = columnA; i <= columnB; i ++)
         mColumnMap[i - 1] = PortMap(channel, port);

      // Try to link to the destination port.
      char c;
      iss.clear();
      while ((c = iss.get()) != EOF)
         connClient += c;
      connClient = trim(connClient);

      if (!connClient.empty())
         if (jack->connectPort(port, connClient) != 0)
            std::cerr << "WARNING! Can not connect to client " << connClient << std::endl;

      return eventList;
   }

   // Learn a new alias.
   if (chunk == "alias")
   {
      std::string alias, replacement;

      if (!(iss >> alias))
         throw (int)iss.tellg();

      if (!(iss >> replacement))
      {
         mAliases.erase(alias);
         return eventList;
      }

      mAliases[alias] = replacement;
      return eventList;
   }

   // Beginning of a loop.
   if (chunk == "loop")
   {
      unsigned num;
      if (iss >> num)
         eventList.push_back(new LoopEvent(num));
      else
         eventList.push_back(new LoopEvent());
      return eventList;
   }

   // End of a loop.
   if (chunk == "endloop")
   {
      eventList.push_back(new EndLoopEvent());
      return eventList;
   }

   // If nothing else, try to parse as a note
   bool bGrouped = false;
   unsigned column = 0;
   iss.seekg(0);
   iss.clear();
   while (iss >> chunk)
   {
      try
      {
         // Comment line.
         if (chunk.length() == 0 || chunk[0] == ';')
            return eventList;

         // If a grouping.
         if (chunk.front() == '(')
         {
            bGrouped = true;
            chunk = chunk.substr(1);
         }
         else if (chunk.back() == ')')
         {
            bGrouped = false;
            chunk = chunk.substr(0, chunk.length() - 1);
         }

         // An aliased name.
         size_t terminalPosition = chunk.find_first_of("!%@/\\#.");
         std::string aliasPart = chunk.substr(0, terminalPosition);

         if (mAliases.find(aliasPart) != mAliases.end())
            chunk.replace(0, terminalPosition, mAliases[aliasPart]);

         /*=== Starting the individual elements processing in `if ... else if...` . ===*/

         // A subpattern by name.
         if (mSubSeqMap != NULL && mSubSeqMap->find(aliasPart) != mSubSeqMap->end())
         {
            SubpatternPlayEvent *e = new SubpatternPlayEvent(mSubSeqMap->at(aliasPart), column);
            mLastNote[column] = e;
            eventList.push_back(e);
         }

         // Silent note.
         else if (chunk == ".")
            eventList.push_back(new SkipEvent(column));

         // Continuing the previous note.
         else if (chunk == "|")
            eventList.push_back(new PedalEvent(column, mLastNote[column]));

         // Default note.
         else if (chunk == "*")
            eventList.push_back(mDfltNote.clone());

         // Previous note.
         else if (chunk == "^")
            eventList.push_back(mLastNote[column]);

         // A MIDI control message.
         else if (chunk.front() == '$')
         {
            try {
               eventList.push_back(new MidiCtlEvent(chunk, column));
            } catch (int e) {
               throw e + (int)iss.tellg();
            }
         }

         // And finally this must be a real note:
         else
         {
            NoteEvent *n = new NoteEvent(chunk);

            // Aply modifiers.
            if (n->volume == (unsigned)-1)
               n->volume = mVolume;                        // Apply the default volume.
            if (!n->natural)
               n->pitch += mSigns->at(n->pitch % 12);      // Apply the default sign.
            n->pitch += mTranspose;                        // Apply the transposition.
            n->column = column;

            // Store the value for the lastNote pattern.
            mLastNote[column] = n;

            // Push the event into the return list.
            eventList.push_back(n);
         }
      }
      catch (int e)
      {
         throw (int) iss.tellg();
      }
      if (!bGrouped)
         column ++;
   }

   return eventList;
}

PortMap& Parser::getPortMap(unsigned column)
{
   static PortMap dfltMap (0, NULL);
   if (column >= mColumnMap.size())
      return dfltMap;
   return mColumnMap[column];
}

PortMap::PortMap(unsigned ch, jack_port_t *p)
{
   channel = ch;
   port = p;
}

PortMap::PortMap()
{
   channel = 0;
   port = NULL;
}
