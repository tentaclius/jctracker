#include "jackengine.h"

#include <iostream>

#include <string.h>
#include <unistd.h>

#include "common.h"

/*****************************************************************************************************/
/* A thread for moving the midi events from the heap and to the ringbuffer */
void* bufferProcessingThread(void *arg)
{
   JackEngine *jack = (JackEngine*) arg;
   if (jack == NULL) return NULL;

   while (gPlaying)
   {
      // While we have an upcoming events that should be sent in the next buffer, do write them in the ringbuffer.
      while (jack->mMidiHeap->peekMin().time <= jack->currentFrameTime() + 100)
         jack->writeMidiData(jack->mMidiHeap->popMin());

      usleep(1000);
   }

   return NULL;
}

/*****************************************************************************************************/
/* Jack main processing callback. */
int jack_process_cb(jack_nframes_t nframes, void *arg)
{
   JackEngine *jack = (JackEngine*) arg;
   jack_port_t *portP = NULL;
   void *portbuffer = NULL;

   int t = 0;
   jack_nframes_t lastFrameTime = jack_last_frame_time(jack->mClient);

   // Clear all buffer first.
   for (std::vector<jack_port_t*>::iterator it = jack->mOutputPorts.begin();
         it != jack->mOutputPorts.end();
         it ++)
   {
      void *pbuf = jack_port_get_buffer(*it, nframes);
      if (pbuf != NULL)
         jack_midi_clear_buffer(pbuf);
   }

   // Read the data from the ringbuffer.
   while (jack_ringbuffer_read_space(jack->mRingbuffer) >= sizeof(MidiMessage))
   {
      MidiMessage midiData;
      if (jack_ringbuffer_peek(jack->mRingbuffer, (char*)&midiData, sizeof(MidiMessage)) != sizeof(MidiMessage))
      {
         std::cerr << "WARNING! Incomplete MIDI message read." << std::endl;
         continue;
      }

      t = midiData.time + nframes - lastFrameTime;
      if (t >= (int)nframes)
         break;

      jack_ringbuffer_read_advance(jack->mRingbuffer, sizeof(MidiMessage));

      if (t < 0)
         t = 0;

      // Initialize output buffer.
      if (midiData.port == 0)
         midiData.port = jack->mDefaultOutputPort;
      if (portP != midiData.port)
      {
         // The port has changed since the previous iteration or was not yet initialize. Reinitialize the buffer.
         portP = midiData.port;

         // Use the default midi port if no port specified.
         if (portP == NULL)
            portP = jack->mDefaultOutputPort;

         // Get the buffer
         portbuffer = jack_port_get_buffer(portP, nframes);
         if (portbuffer == NULL)
         {
            std::cerr << "WARNING! Cannot get jack port buffer." << std::endl;
            return -1;
         }
      }

      jack_midi_data_t *buffer = jack_midi_event_reserve(portbuffer, t, midiData.len);
      if (buffer == NULL)
      {
         std::cerr << "WARNING! Cannot get buffer for midi content." << std::endl;
         break;
      }
      memcpy(buffer, midiData.data, midiData.len);

      trace("jack_process_cb: midi(%x,%x,%x) t=%llu\n", midiData.data[0], midiData.data[1], midiData.data[2],
            (long long unsigned)midiData.time);
   }

   return 0;      
}

/*****************************************************************************************************/
/* Jack shutdown callback. */
void jack_shutdown_cb(void *arg)
{
   JackEngine *jack = (JackEngine*) arg;
   jack->shutdown();
}

/*****************************************************************************************************/
/* Write the midi message into the ringbuffer which is processed by jack callback in its turn. */
void JackEngine::writeMidiData(MidiMessage theMessage)
{
   if (jack_ringbuffer_write_space(mRingbuffer) > sizeof(MidiMessage))
   {
      if (jack_ringbuffer_write(mRingbuffer, (const char*) &theMessage, sizeof(MidiMessage)) != sizeof(MidiMessage))
         std::cerr << "WARNING! Midi message is not written entirely." << std::endl;
   }
   return;
}

/*****************************************************************************************************/
/* Hide the constructor, as it is a singleton. */
JackEngine::JackEngine()
{
}

/*****************************************************************************************************/
JackEngine::~JackEngine()
{
   delete mMidiHeap;
}

/*****************************************************************************************************/
/* Return an instance of the singleton. */
JackEngine* JackEngine::instance()
{
   static JackEngine *inst = new JackEngine();
   return inst;
}

/*****************************************************************************************************/
/* Initialization and activation of jack interface. */
void JackEngine::init()
{
   jack_options_t options = JackNullOption;
   jack_status_t  status;

   // Midi event heap.
   mMidiHeap = new MidiHeap(MIDI_HEAP_SIZE);

   // Create the ringbuffer.
   mRingbuffer = jack_ringbuffer_create(RINGBUFFER_SIZE * sizeof(MidiMessage));

   if ((mClient = jack_client_open("jctracker", options, &status)) == 0)
      throw "Jack server is not running.";

   // Set the callbacks.
   jack_set_process_callback(mClient, jack_process_cb, (void*) this);
   jack_on_shutdown(mClient, jack_shutdown_cb, (void*) this);

   mSampleRate = jack_get_sample_rate(mClient);

   // Create two ports.
   mInputPort  = jack_port_register(mClient, "input",  JACK_DEFAULT_MIDI_TYPE, JackPortIsInput,  0);
   mDefaultOutputPort = jack_port_register(mClient, "default", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
   mOutputPorts.push_back(mDefaultOutputPort);

   // Find out the buffer size.
   mBufferSize = jack_get_buffer_size(mClient);

   if (jack_activate(mClient))
      throw "cannot activate Jack client";

   pthread_create(&mMidiWriteThread, NULL, bufferProcessingThread, this);
}

/*****************************************************************************************************/
/* Register an output port. */
jack_port_t* JackEngine::registerOutputPort(std::string name)
{
   for (std::vector<jack_port_t*>::iterator it = mOutputPorts.begin();
         it != mOutputPorts.end(); it ++)
   {
      if (name == jack_port_short_name(*it))
         return *it;
   }

   jack_port_t *p = jack_port_register(mClient, name.c_str(), JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
   mOutputPorts.push_back(p);
   return p;
}

/*****************************************************************************************************/
/* Connect to a port. */
int JackEngine::connectPort(jack_port_t *port, std::string destination)
{
   return jack_connect(mClient, jack_port_name(port), destination.c_str());
}

/*****************************************************************************************************/
/* Shutdown the jack interface. */
void JackEngine::shutdown()
{
   jack_port_unregister(mClient, mInputPort);
   jack_port_unregister(mClient, mDefaultOutputPort);
   jack_client_close(mClient);
}

/*****************************************************************************************************/
/* Convert microsecond time to jack nframes. */
jack_nframes_t JackEngine::msToNframes(uint64_t ms)
{
   return ms * mSampleRate / 1000;
}

/*****************************************************************************************************/
/* Return the current time in nframes. */
jack_nframes_t JackEngine::currentFrameTime()
{
   return jack_frame_time(mClient);
}

/*****************************************************************************************************/
/* Is there are unprocessed midi events. */
bool JackEngine::hasPendingEvents()
{
   return mMidiHeap->count() > 0;
}

/*****************************************************************************************************/
/* Put a midi message into the heap. */
void JackEngine::queueMidiEvent(MidiMessage &message)
{
   mMidiHeap->insert(message);
}

/*****************************************************************************************************/
/* Put a midi message into the midi heap. */
void JackEngine::queueMidiEvent(MidiMessage message)
{
   mMidiHeap->insert(message);
}

/*****************************************************************************************************/
/* Construct and put a midi message into the heap. */
void JackEngine::queueMidiEvent(unsigned char b0, unsigned char b1, unsigned char b2, jack_nframes_t time, unsigned channel, jack_port_t *port)
{
   MidiMessage msg (b0, b1, b2, time, channel, port);
   mMidiHeap->insert(msg);
}

/*****************************************************************************************************/
/* Send a control midi message to stop all sounds. */
void JackEngine::stopSounds()
{
   for (std::vector<jack_port_t*>::iterator it = mOutputPorts.begin();
         it != mOutputPorts.end(); it ++)
   {
      MidiMessage msg(MIDI_CONTROLLER, MIDI_ALL_SOUND_OFF, 0, currentFrameTime(), 0, (*it));
      writeMidiData(msg);
   }
}
