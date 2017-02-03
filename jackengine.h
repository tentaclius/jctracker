#ifndef JACKENGINE_H
#define JACKENGINE_H

#include <iostream>
#include <vector>

#include <pthread.h>

#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>

#include "midimessage.h"
#include "midiheap.h"

#define MIDI_HEAP_SIZE                 1024
#define RINGBUFFER_SIZE                1024

/*******************************************************************************************/
/* Manage Jack connection and hide specific objects. Singleton. */
class JackEngine
{
   private:
      MidiHeap          *mMidiHeap;        // A sorted queue of midi events.
      jack_client_t     *mClient;          // The client representation.
      jack_ringbuffer_t *mRingbuffer;
      jack_nframes_t     mBufferSize;
      jack_nframes_t     mSampleRate;
      jack_port_t       *mDefaultOutputPort;
      jack_port_t       *mInputPort;      // Isn't used yet.

      pthread_t          mMidiWriteThread;

      std::vector<jack_port_t*> mOutputPorts;

      /* Write the midi message into the ringbuffer which is processed by jack callback in its turn. */
      void writeMidiData(MidiMessage theMessage);

      /* Hide the constructor, as it is a singleton. */
      JackEngine();

   public:
      static JackEngine* instance();

      ~JackEngine();

      /* Initialization and activation of jack interface. */
      void init();

      /* Register an output port. */
      jack_port_t* registerOutputPort(std::string name);

      /* Connect to a port. */
      int connectPort(jack_port_t *port, std::string destination);

      /* Shutdown the jack interface. */
      void shutdown();

      /* Convert microsecond time to jack nframes. */
      jack_nframes_t msToNframes(uint64_t ms);

      /* Return the current time in nframes. */
      jack_nframes_t currentFrameTime();

      /* Is there are unprocessed midi events. */
      bool hasPendingEvents();

      /* Put a midi message into the heap. */
      void queueMidiEvent(MidiMessage &message);
      
      void queueMidiEvent(MidiMessage message);

      /* Put a midi message into the heap. */
      void queueMidiEvent(unsigned char b0, unsigned char b1, unsigned char b2, jack_nframes_t time,
            unsigned channel = 0, jack_port_t *port = NULL);

      /* Send a control midi message to stop all sounds. */
      void stopSounds();

      /* Jack callbacks. */
      friend int jack_process_cb(jack_nframes_t nframes, void *arg);
      friend int jack_buffsize_cb(jack_nframes_t nframes, void *arg);
      friend void jack_shutdown_cb(void *arg);
      friend void stop_all_sound();
      friend void* bufferProcessingThread(void *arg);
};

#endif
