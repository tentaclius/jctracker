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


#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <signal.h>

#include "common.h"
#include "sequencer.h"


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
