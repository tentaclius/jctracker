/*
BSD 2-Clause License



Copyright (c) 2017, Anton Erdman.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*
 Roadmap:
 [v] Set up jackd client skeleton.
 [v] Try sending few single note_on note_off midi events to the gJack port in process() callback.
 [v] Signal handler to send midi stop all event.
 [v] Write the sequencer: note_on at the note appearing; note_off at the next note in the row.
 [v] Pedaled notes.
 [v] Sort upcoming midi events by time with a heap.
 [v] Find a proper way to handle delayed and timed notes.
 [v] Create event vectors with pre-defined size.
 [v] Note playing time.
 [v] Process empty notes, default notes and other templates.
 [v] Directives: volume, default, tempo etc.
 [v] Handle loops.
 [v] Separate threads for different tasks.
 [v] Implement bar event and sizes support and tempo changes.
 [v] Transposition ("transpose 20")..
 [v] Initial signs (sharp/flat) applied to all notes.
 [v] Aliases for the notes with _hash_tables_!
 [v] Proper time management.
 [v] Make the transposition static.
 [v] Ports and channels assignment.
 [v] Automatic connection.
 [v] Move the patterns and the player into separate classes.
 [v] Note length and timing in parts of current note length.
 [v] Modifiers to work for aliases.
 [-] Store output ports in an unsigned indexed vector. Store the index in Midi messages.
 [v] Allow several notes for the channel.
 [v] MIDI control messages.
 [v] MIDI control ramp. $4=1..100:3/2
 [v] MIDI pitch bend control.
 [v] Multiple matterns. define <name> ... end
 [v] wait command.
 [ ] Rewrite Events with virtual functions noteOn(), noteOff(), control()...
 [ ] Better error messages for the parser (with highlighting the error position).
 [ ] Output thread reading a message queue. The queue drops the messages if no room in the queue.
 [ ] Track source file. Display the current lines while playing.
 [ ] OSC controls.
 [ ] 'define' full pattern.
 [ ] Ligato.
 [ ] Transposition separately for each column.
 [ ] Unicode sharp/flat/natural signs.
 [ ] Add deliberate NOTE_END pattern.
 [ ] Jack transport.
 [ ] Input MIDI port to record notes from a MIDI keyboard.
*/

#include <iostream>
#include <vector>
#include <sstream>
#include <list>
#include <map>
#include <climits>
#include <algorithm>

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <math.h>
#include <assert.h>

#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>

#include "main.h"
#include "parser.h"
#include "events.h"



typedef jack_default_audio_sample_t sample_t;


bool gPlaying = false;

/*******************************************************************************************/
/* A class for synchronous output from realtime thread. */
class OutputLine
{
   private:
      std::vector<std::string> mQueue;
      unsigned mReadIdx, mWriteIdx;
      pthread_mutex_t mMutex;
      pthread_cond_t mCond;

      inline unsigned advanceIndex(unsigned idx)
      {
         idx ++;
         if (idx == mQueue.size())
            idx = 0;
         return idx;
      }

      inline bool isFull()
      {
         return advanceIndex(mWriteIdx) == mReadIdx;
      }

      inline bool isEmpty()
      {
         return mReadIdx == mWriteIdx;
      }
   
   public:
      OutputLine(size_t size)
      {
         mQueue.resize(size);
         mReadIdx = mWriteIdx = 0;
      }

      void push(std::string msg)
      {
         if (isFull())
            return;
         else {
            mQueue[mWriteIdx] = msg;
            mWriteIdx = advanceIndex(mWriteIdx);
         }
      }

      std::string pull()
      {
         pthread_mutex_lock(&mMutex);

         if (isEmpty())
            pthread_cond_wait(&mCond, &mMutex);

         std::string s = mQueue[mReadIdx];
         mReadIdx = advanceIndex(mReadIdx);

         pthread_mutex_unlock(&mMutex);

         return s;
      }
};

/*******************************************************************************************/
/* Signal handler. */
void signalHandler(int s)
{
   JackEngine *jack = JackEngine::instance();
   std::cerr << "Signal " << s << " arrived. Shutting down." << std::endl;
   jack->stopSounds();
   gPlaying = false;
   usleep(100000);
   jack->shutdown();
   exit(1);
}





/***************************************************/
/* Virtual function to schedule NOTE ON. */
/*
ControlFlow NoteEvent::execute(JackEngine *jack, Sequencer *seq)
{
   ControlFlow ret = {true, true, true};

   PortMap pm = seq->getPortMap(column);
      
   // Queue the note on event.
   jack->queueMidiEvent(MIDI_NOTE_ON, pitch, volume,
         seq->getCurrentTime() + jack->msToNframes(delay)
         + (partDiv != 0 ? (jack->msToNframes(60 * 1000 / seq->getTempo() / seq->getQuant()) * partDelay / partDiv) : 0)
         + column,
         pm.channel, pm.port);

   if (!endless && (time != 0 || partTime != 0))
   {
      // If the note has specific time, schedule the off event right now.
      ret.bNeedsStopping = false;
      jack->queueMidiEvent(MIDI_NOTE_OFF, pitch, volume,
            seq->getCurrentTime() + jack->msToNframes(delay)
            + (partDiv != 0 ? (jack->msToNframes(60 * 1000 / seq->getTempo() / seq->getQuant())
                  * partDelay / partDiv) : 0)
            + jack->msToNframes(time)
            + (partDiv != 0 ? (jack->msToNframes(60 * 1000 / seq->getTempo() / seq->getQuant())
                  * partTime / partDiv) : 0) - 2,
            pm.channel, pm.port);
   }

   return ret;
}
*/

/*****************************************************************************************************/
/* Read the data from the sequencer and queue the midi events to Jack */
void play(JackEngine *jack, Sequencer &seq)
{
   // Play while we got something to play.
   while (gPlaying && seq.playNextLine());

   // Wait for all events to be processed.
   while (jack->hasPendingEvents() & gPlaying)
      usleep(200000);
   usleep(200000);

   gPlaying = false;
}

/*******************************************************************************************/
/* main */
int main(int argc, char **argv)
{
   std::string line;
   
   // Setup signal handler.
   struct sigaction action;
   sigemptyset(&action.sa_mask);
   action.sa_flags = 0;
   action.sa_handler = signalHandler;

   sigaction(2, &action, 0L);
   sigaction(3, &action, 0L);
   sigaction(4, &action, 0L);
   sigaction(6, &action, 0L);
   sigaction(15, &action, 0L);

   gPlaying = true;

   // Init Jackd connection.
   JackEngine *jack = JackEngine::instance();
   try {
      jack->init();
   } catch (std::string &s) {
      std::cout << "Error during Jack initialization: " << s << std::endl;
   }
   
   // Init the sequencer and load the pattern.
   Sequencer seq (jack);
   seq.readFromStream(std::cin);

   // Play the pattern.
   play(jack, seq);

   // Shutdown the client and exit.
   jack->stopSounds();
   usleep(200000);

   jack->shutdown();

   return 0;
}
