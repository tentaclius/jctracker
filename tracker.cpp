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
 [v] Set up jackd skeleton.
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
 [v] Initial signs (bemol/dies) applied to all notes.
 [v] Aliases for the notes with _hash_tables_!
 [v] Proper time management.
 [v] Make the transposition static.
 [v] Ports and channels assignment.
 [v] Automatic connection.
 [v] Move the patterns and the player into separate classes.
 [v] Note length and timing in parts of current note length.
 [v] Modifiers to work for aliases.
 [-] Store output ports in an unsigned indexed vector. Store the index in Midi messages.
 [-] Pattern file management: load, unload, reload of multiple patterns.
 [v] Allow several notes for the channel.
 [v] MIDI control messages.
 [ ] sleep/pause command.
 [ ] Better parsing error messages (with highlighting the error position).
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

#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>


#define MIDI_NOTE_ON                   0x90
#define MIDI_NOTE_OFF                  0x80
#define MIDI_PROGRAM_CHANGE            0xC0
#define MIDI_CONTROLLER                0xB0
#define MIDI_RESET                     0xFF
#define MIDI_HOLD_PEDAL                64
#define MIDI_ALL_SOUND_OFF             0x7B
#define MIDI_ALL_MIDI_CONTROLLERS_OFF  121
#define MIDI_ALL_NOTES_OFF             123
#define MIDI_BANK_SELECT_MSB           0
#define MIDI_BANK_SELECT_LSB           32


typedef jack_default_audio_sample_t sample_t;

int jack_process_cb(jack_nframes_t nframes, void *arg);
int jack_buffsize_cb(jack_nframes_t nframes, void *arg);
void jack_shutdown_cb(void *arg);
void* bufferProcessingThread(void *arg);

bool gPlaying = false;

/*******************************************************************************************/
/* TRACE */
#ifdef DEBUG
#define trace(...) TRACE(__FILE__, __LINE__,  __VA_ARGS__);
void TRACE(const char *file, int line, const char *fmt, ...)
{
   va_list argP;
   va_start(argP, fmt);

   if (fmt)
   {
      fprintf(stderr, "TRACE(%s:%d): ", file, line);
      vfprintf(stderr, fmt, argP);
   }

   va_end(argP);
}
#else
#define trace(...) {}
#endif

/*******************************************************************************************/
/* struct MidiMessage */
struct MidiMessage
{
   jack_port_t    *port;
   jack_nframes_t  time;
   int             len;

   unsigned char   data[3];

   /***************************************************/
   /* Construct the message with the given midi data. */
   MidiMessage(int b0, int b1, int b2, jack_nframes_t tm, unsigned channel = 0, jack_port_t *p = NULL)
   {
      if (b0 >= 0x80 && b0 <= 0xEF)
      {
         b0 &= 0xF0;
         b0 += channel;       // Channel.
      }

      if (b1 == -1)
      {
         len = 1;
         data[0] = b0;
      }
      else if (b2 == -1)
      {
         len = 2;
         data[0] = b0;
         data[1] = b1;
      }
      else
      {
         len = 3;
         data[0] = b0;
         data[1] = b1;
         data[2] = b2;
      }

      time = tm;
      port = p;
   }

   /***************************************************/
   /* Create the message with null data */
   MidiMessage()
   {
      memset(data, 0, 3);
      len = 0;
      time = 0;
   }
};

/*******************************************************************************************/
/* A simple heap implementation to keep the midi messages in order. */
class MidiHeap
{
   private:
      MidiMessage     *mArray;
      pthread_mutex_t  mMutex;
      pthread_cond_t   mbCanWrite;
      pthread_cond_t   mbCanRead;
      size_t           mSize;              // Maximum size (or array size).
      size_t           mTop;               // Current top position.

      /***************************************************/
      /* Swap two elements with the given indicies. */
      inline void swap(size_t i, size_t j)
      {
         MidiMessage t = mArray[i];
         mArray[i] = mArray[j];
         mArray[j] = t;
      }

      /***************************************************/
      /* Return an index of the element which is parent to the element with the given index. */
      inline size_t parent(size_t i)
      {
         return (i + 1) / 2 - 1;
      }

      /***************************************************/
      /* Return an index of the left child of the given index element. */
      inline size_t lchild(size_t i)
      {
         return (i + 1) * 2 - 1;
      }

      /***************************************************/
      /* Return an index of the right child of the given index element. */
      inline size_t rchild(size_t i)
      {
         return (i + 1) * 2;
      }

      /***************************************************/
      /* Return an index of the smallest element of two given index elements. */
      inline size_t imin(size_t i, size_t j)
      {
         // If only one exists.
         if (i < mTop && j >= mTop)
            return i;
         if (i >= mTop && j < mTop)
            return j;
         if (i >= mTop && j >= mTop)
            return (size_t) -1;

         // Compare the two by the time and the port number.
         if (mArray[i].time < mArray[j].time)
            return i;
         else if (mArray[i].time > mArray[j].time)
            return j;
         else if (mArray[i].port < mArray[j].port)
            return i;
         else
            return j;
      }

      /***************************************************/
      /* Rearrange the buffer to maintain the order. */
      void bubbleDown(size_t i)
      {
         size_t j = imin(lchild(i), rchild(i));

         while (j != (size_t)-1 && mArray[i].time > mArray[j].time)
         {
            swap(i, j);
            i = j;
            j = imin(lchild(i), rchild(i));
         }
      }

   public:
      /***************************************************/
      MidiHeap(size_t s)
      {
         mArray = new MidiMessage[s];
         mSize = s;
         pthread_mutex_init(&mMutex, NULL);
      }

      /***************************************************/
      ~MidiHeap()
      {
         delete mArray;
         pthread_mutex_destroy(&mMutex);
      }

      /***************************************************/
      /* Add a new element while maintaining the order.
         The function locks until there is a space in the buffer. */
      void insert(MidiMessage &msg)
      {
         pthread_mutex_lock(&mMutex);

         if (mTop + 1 >= mSize)
            pthread_cond_wait(&mbCanWrite, &mMutex);

         size_t i = mTop ++;
         mArray[i] = msg;

         while (parent(i) != (size_t)-1 && mArray[i].time < mArray[parent(i)].time)
         {
            swap(i, parent(i));
            i = parent(i);
         }

         pthread_cond_broadcast(&mbCanRead);
         pthread_mutex_unlock(&mMutex);
      }

      /***************************************************/
      /* Pop the minimal element.
         The function locks until there is an element to read. */
      MidiMessage popMin()
      {
         pthread_mutex_lock(&mMutex);

         if (mTop == 0)
            pthread_cond_wait(&mbCanRead, &mMutex);

         MidiMessage min = mArray[0];
         mArray[0] = mArray[mTop - 1];
         mTop --;
         bubbleDown(0);

         pthread_cond_broadcast(&mbCanWrite);
         pthread_mutex_unlock(&mMutex);

         return min;
      }

      /***************************************************/
      /* Look the minimal element without removing it from the buffer.
         The function locks until there is an element to look at. */
      MidiMessage peekMin()
      {
         MidiMessage min;

         pthread_mutex_lock(&mMutex);
         if (mTop == 0)
            pthread_cond_wait(&mbCanRead, &mMutex);

         min = mArray[0];
         pthread_mutex_unlock(&mMutex);

         return min;
      }

      /***************************************************/
      /* The number of elements available in the buffer. */
      size_t count()
      {
         return mTop;
      }
};

/*******************************************************************************************/
/* Manage Jack connection and hide specific objects. */
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

      /***************************************************/
      /* Write the midi message into the ringbuffer which is processed by jack callback in its turn. */
      void writeMidiData(MidiMessage theMessage)
      {
         if (jack_ringbuffer_write_space(mRingbuffer) > sizeof(MidiMessage))
         {
            if (jack_ringbuffer_write(mRingbuffer, (const char*) &theMessage, sizeof(MidiMessage)) != sizeof(MidiMessage))
               std::cerr << "WARNING! Midi message is not written entirely." << std::endl;
         }
         return;
      }

   public:

      /***************************************************/
      JackEngine()
      {
      }

      /***************************************************/
      ~JackEngine()
      {
         delete mMidiHeap;
      }

      /***************************************************/
      /* Initialization and activation of jack interface. */
      void init()
      {
         jack_options_t options = JackNullOption;
         jack_status_t  status;

         // Midi event heap.
         mMidiHeap = new MidiHeap(256);

         // Create the ringbuffer.
         mRingbuffer = jack_ringbuffer_create(1024 * sizeof(MidiMessage));

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

      /***************************************************/
      /* Register an output port. */
      jack_port_t* registerOutputPort(std::string name)
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

      /***************************************************/
      /* Connect to a port. */
      int connectPort(jack_port_t *port, std::string destination)
      {
         return jack_connect(mClient, jack_port_name(port), destination.c_str());
      }

      /***************************************************/
      /* Shutdown the jack interface. */
      void shutdown()
      {
         jack_port_unregister(mClient, mInputPort);
         jack_port_unregister(mClient, mDefaultOutputPort);
         jack_client_close(mClient);
      }

      /***************************************************/
      /* Convert microsecond time to jack nframes. */
      jack_nframes_t msToNframes(uint64_t ms)
      {
         return ms * mSampleRate / 1000;
      }

      /***************************************************/
      /* Return the current time in nframes. */
      jack_nframes_t currentFrameTime()
      {
         return jack_frame_time(mClient);
      }

      /***************************************************/
      /* Is there are unprocessed midi events. */
      bool hasPendingEvents()
      {
         return mMidiHeap->count() > 0;
      }

      /***************************************************/
      /* Put a midi message into the heap. */
      void queueMidiEvent(MidiMessage &message)
      {
         mMidiHeap->insert(message);
      }

      /***************************************************/
      /* Put a midi message into the heap. */
      void queueMidiEvent(unsigned char b0, unsigned char b1, unsigned char b2, jack_nframes_t time,
            unsigned channel = 0, jack_port_t *port = NULL)
      {
         MidiMessage msg (b0, b1, b2, time, channel, port);
         mMidiHeap->insert(msg);
      }

      /***************************************************/
      /* Send a control midi message to stop all sounds. */
      void stopSounds()
      {
         for (std::vector<jack_port_t*>::iterator it = mOutputPorts.begin();
              it != mOutputPorts.end(); it ++)
         {
            MidiMessage msg(MIDI_CONTROLLER, MIDI_ALL_SOUND_OFF, 0, currentFrameTime(), 0, (*it));
            writeMidiData(msg);
         }
      }

      /***************************************************/
      friend int jack_process_cb(jack_nframes_t nframes, void *arg);
      friend int jack_buffsize_cb(jack_nframes_t nframes, void *arg);
      friend void jack_shutdown_cb(void *arg);
      friend void stop_all_sound();
      friend void* bufferProcessingThread(void *arg);
}
gJack;

/*******************************************************************************************/
/* Signal handler. */
void signalHandler(int s)
{
   std::cerr << "Signal " << s << " arrived. Shutting down." << std::endl;
   gJack.stopSounds();
   gPlaying = false;
   usleep(100000);
   gJack.shutdown();
   exit(1);
}

/*******************************************************************************************/
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

      trace("jack_process_cb: midi(%x,%x,%x,%llu) t=%d\n", midiData.data[0], midiData.data[1], midiData.data[2], midiData.time, t);
   }

   return 0;      
}

/*******************************************************************************************/
/* Jack shutdown callback. */
void jack_shutdown_cb(void *arg)
{
   JackEngine *jack = (JackEngine*) arg;
   jack->shutdown();
}

/*******************************************************************************************/
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

/*******************************************************************************************/
/* A parent of all possible tracker events. */
class Event
{
   public:
      virtual ~Event() {}
};

/*******************************************************************************************/
/* A note to be played. */
struct NoteEvent : public Event
{
   unsigned column;     // The internal channel number.
   unsigned pitch;      // The pitch of the note.
   unsigned volume;     // The volume.
   
   double   delay;      // Delay time for the note in microseconds.
   double   time;       // Time of the note playing in microseconds.
   double   partDelay;  // Delay in parts of the current note.
   double   partTime;   // Playing time in parts of the current note.
   double   partDiv;

   bool     natural;    // If the note is of natural tone.
   bool     endless;    // Do not send NOTE_OFF for this note.

   NoteEvent(unsigned n, unsigned v, uint64_t tm, uint64_t dl, unsigned col)
      : column(col)
      , pitch(n)
      , volume(v)
      , delay(dl)
      , time(tm)
      , partDelay(0)
      , partTime(0)
      , partDiv(0)
      , natural(false)
      , endless(false)
   {}

   NoteEvent() { NoteEvent(0,64,0,0,0); }

   /***************************************************/
   /* Constructor that parses the note from the string. */
   NoteEvent(const std::string &buf, unsigned column = 0)
   {
      const int octaveLen = 12;
      natural = false;

      pitch = 0;
      volume = (unsigned)-1;
      time = 0;
      delay = 0;
      partTime = 0;
      partDelay = 0;
      partDiv = 0;

      std::istringstream iss (buf);

      if (buf.length() == 0)
         throw 0;

      // The note.
      switch (iss.get())
      {
         case 'C':
         case 'c':
            pitch = 0;
            break;

         case 'D':
         case 'd':
            pitch = 2;
            break;

         case 'E':
         case 'e':
            pitch = 4;
            break;

         case 'F':
         case 'f':
            pitch = 5;
            break;

         case 'G':
         case 'g':
            pitch = 7;
            break;

         case 'A':
         case 'a':
            pitch = 9;
            break;

         case 'B':
         case 'b':
            pitch = 11;
            break;

         default:
            throw 0;
      }

      // Check for Flat or Sharp modifer
      if (iss.peek() == '#')
      {
         iss.get();
         pitch ++;
      }
      if (iss.peek() == 'b' || iss.peek() == '&')
      {
         iss.get();
         pitch --;
      }
      if (iss.peek() == 'n')
      {
         iss.get();
         natural = true;
      }

      // Read the octave number:
      unsigned octave = 0;
      if (iss >> octave)
         pitch += (octave + 1) * octaveLen;
      else
         // Default is fourth octave
         pitch += (4 + 1) * octaveLen;

      // Read possible modifiers:
      char c = 0;
      iss.clear();
      while ((c = iss.get()) != EOF && c != ' ' && c != '\t')
      {
         switch (c)
         {
            case '@':
               // Playing time modifier
               iss >> time;
               break;

            case '%':
               // Delay time modifier
               iss >> delay;
               break;

            case '+':
               // Playing time in parts of the current note length.
               iss >> partDelay;
               break;

            case '/':
               iss >> partDiv;
               if (partTime == 0) partTime = 1;
               break;

            case ':':
               iss >> partTime;
               break;

            case '!':
               // Volume modifier.
               iss >> volume;
               break;

            case '.':
               // Endless note.
               endless = true;
               break;
         }
      }
   }

   /***************************************************/
   /* Set the parameters of the current midi message. */
   void set(unsigned n, unsigned v, uint64_t tm, uint64_t dl)
   {
      pitch = n;
      volume = v;
      time = tm;
      delay = dl;
   }

   /***************************************************/
   ~NoteEvent() {}

   /***************************************************/
   /* Return a new instance of the same data. */
   NoteEvent* clone()
   {
      return new NoteEvent(pitch, volume, time, delay, column);
   }
};

/*******************************************************************************************/
/* A message to a midi controller. */
struct MidiCtlEvent : public Event
{
   unsigned column;
   unsigned controller;
   unsigned value;
   double delay;
   double delayDiv;

   MidiCtlEvent()
   {
      column = 0;
      controller = 0;
      value = 0;
      delay = 0;
      delayDiv = 1;
   }

   MidiCtlEvent(const std::string &str, unsigned column = 0)
   {
      column = 0;
      controller = 0;
      value = 0;
      delay = 0;
      delayDiv = 1;

      if (str.front() != '$')
         throw 0;

      std::istringstream iss (str.substr(1));

      if (!(iss >> controller) || iss.get() != '=' || !(iss >> value))
         throw iss.tellg();

      char c;
      while ((c = iss.get()) != EOF)
      {
         switch (c)
         {
            case '/':
               iss >> delayDiv;
               break;
            case ':':
               iss >> delay;
               break;
         }
      }
   }
};

/*******************************************************************************************/
/* Silent note or pause. */
struct SkipEvent : public Event
{
   unsigned column;

   SkipEvent(unsigned col)
   {
      column = col;
   }
   ~SkipEvent() {}
};

/*******************************************************************************************/
/* For visual separation and changing note size. */
struct BarEvent : public Event
{
   unsigned nom, div;
   
   BarEvent(unsigned n, unsigned d) : nom(n), div(d) {}
   BarEvent(unsigned n, unsigned d, unsigned pitch) : nom(n), div(d) {}
   ~BarEvent() {}
};

/*******************************************************************************************/
/* Tempo changing command. */
struct TempoEvent : public Event
{
   unsigned tempo;

   TempoEvent(unsigned t) : tempo(t) {}
   ~TempoEvent() {}
};

/*******************************************************************************************/
/* The previous note will not be muted. */
struct PedalEvent : public Event
{
   unsigned column;     // The internal channel number.
   PedalEvent(unsigned c)
   {
      column = c;
   }

   ~PedalEvent() {}
};

/*******************************************************************************************/
/* Beginning of a loop. */
struct LoopEvent : public Event
{
   unsigned count;

   LoopEvent(unsigned n)
   {
      count = n;
   }

   LoopEvent()
   {
      count = (unsigned)-1;
   }
};

/*******************************************************************************************/
/* End of the loop. */
struct EndLoopEvent : public Event
{
};

/*******************************************************************************************/
/* A structure to associate a port and a channel to a column. */
struct PortMap
{
   unsigned channel;
   jack_port_t *port;

   PortMap(unsigned ch, jack_port_t *p)
   {
      channel = ch;
      port = p;
   }

   PortMap()
   {
      channel = 0;
      port = NULL;
   }
};

/*******************************************************************************************/
/* Parse an input line. */
class Parser
{
   size_t mChannelNum;
   std::vector<NoteEvent> *mLastNote;
   NoteEvent mDfltNote;
   unsigned mVolume;
   std::vector<int> *mSigns;
   std::map<std::string, std::string> mAliases;
   std::vector<PortMap> mColumnMap;
   int mTranspose;
   size_t mLinePos;

   private:
      /***************************************************/
      /* Remove spaces at the beginning and the end of the string */
      std::string trim(std::string s)
      {
         unsigned a = 0, b = s.length() - 1;

         while (a < b && isblank(s[a])) a ++;
         while (a < b && isblank(s[b])) b --;

         if (a == b) return "";
         else return s.substr(a, b - a + 1);
      }

   public:
      /***************************************************/
      /* Create the parser. */
      Parser(size_t chan = 64)
      {
         mSigns = new std::vector<int>(12, 0);
         mChannelNum = chan;
         mLastNote = new std::vector<NoteEvent>(chan, NoteEvent(0,0,0,0,0));
         mDfltNote.set(0,0,0,0);
         mTranspose = 0;
         mVolume = 64;
         mLinePos = 0;
      }

      /***************************************************/
      ~Parser()
      {
         delete mLastNote;
         delete mSigns;
      }

      /***************************************************/
      /* Parse a given line (with one or multiple directives or patterns). */
      std::vector<Event*> parseLine(std::string line)
      {
         mLinePos = 0;
         std::string chunk;          // A piece of the line to read the command.
         std::istringstream iss (line);
         std::vector<Event*> eventList;

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
               throw iss.tellg();
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

         // Register the port.
         if (chunk == "port")
         {
            unsigned columnA, columnB;    // Column number range to associate.
            std::string portName;         // The name of a port to create.
            unsigned channel;             // Channel number to use.
            std::string connClient;       // The client name to try to connect to.

            // Mandatory parameters.
            if (!(iss >> columnA))
               throw iss.tellg();

            // Second column number is optional.
            if (!(iss >> columnB))
            {
               columnB = columnA;
               iss.clear();
            }

            // The port name is mandatory.
            if (!(iss >> portName))
               throw iss.tellg();

            // Optional parameters.
            iss >> channel;

            // Create the port.
            jack_port_t *port = gJack.registerOutputPort(portName);

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
               if (gJack.connectPort(port, connClient) != 0)
                  std::cerr << "WARNING! Can not connect to client " << connClient << std::endl;

            return eventList;
         }

         // Learn a new alias.
         if (chunk == "alias")
         {
            std::string alias, replacement;
            
            if (!(iss >> alias))
               throw iss.tellg();

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

               // Silent note.
               if (chunk == ".")
                  eventList.push_back(new SkipEvent(column));

               // Continuing the previous note.
               else if (chunk == "|")
                  eventList.push_back(new PedalEvent(column));

               // Default note.
               else if (chunk == "*")
                  eventList.push_back(mDfltNote.clone());

               // Previous note.
               else if (chunk == "^")
                  eventList.push_back(mLastNote->at(column).clone());

               // A MIDI control message.
               else if (chunk.front() == '$')
               {
                  try {
                     eventList.push_back(new MidiCtlEvent(chunk, column));
                  } catch (int e) {
                     throw e + iss.tellg();
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
                  mLastNote->at(column).set(n->pitch, n->volume, n->time, n->delay);

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

      /***************************************************/
      /* Return a port to which the column matches. */
      PortMap& getPortMap(unsigned column)
      {
         static PortMap dfltMap (0, NULL);
         if (column >= mColumnMap.size())
            return dfltMap;
         return mColumnMap[column];
      }
};

/*******************************************************************************************/
/* Interpret and process the pattern line by line. */
class Sequencer
{
   std::vector<std::vector<Event*> > mSong;
   Parser mParser;
   size_t mCurrentPos;
   std::list<std::pair<int, unsigned> > mLoopStack;

   public:
      Sequencer(JackEngine *j)
      {
         mCurrentPos = 0;
      }

      /*****************************************************/
      // Read the data from the stream.
      void readFromStream(std::istream &ss)
      {
         std::string line;

         while (std::getline(ss, line))
         {
            try
            {
               std::vector<Event*> lst = mParser.parseLine(line);
               if (!lst.empty())
                  mSong.push_back(lst);
            }
            catch (int e)
            {
               std::cerr << "Cannot parse line: " << line << std::endl;
            }
         }
      }

      /*****************************************************/
      /* Get the vector of the next events. */
      std::vector<Event*> getNextLine()
      {
         // Check if we reached the end. Return empty vector if so.
         if (mCurrentPos >= mSong.size())
            return std::vector<Event*> ();

         LoopEvent     *lp = dynamic_cast<LoopEvent*>(mSong[mCurrentPos][0]);
         EndLoopEvent *elp = dynamic_cast<EndLoopEvent*>(mSong[mCurrentPos][0]);

         if (lp || elp)
         {
            if (lp != NULL)
               // The beginning of a loop; push the starting point to the loop stack.
               mLoopStack.push_back(std::pair<int, unsigned>(lp->count, mCurrentPos));

            else if (elp != NULL)
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

      /*****************************************************/
      /* Return the mapping of the event's column to the port and channel. */
      PortMap& getPortMap(unsigned column)
      {
         return mParser.getPortMap(column);
      }
};

/*****************************************************************************************************/
/* Read the data from the sequencer and queue the midi events to Jack */
void play(JackEngine *jack, Sequencer &seq)
{
   // Runtime values.
   unsigned tempo = 100;
   unsigned quantz = 4;
   std::vector<std::list<NoteEvent*>> activeNotesVec;

   jack_nframes_t currentTime = jack->currentFrameTime();

   for (std::vector<Event*> eventVec = seq.getNextLine();
        !eventVec.empty() && gPlaying;
        eventVec = seq.getNextLine())
   {
      // The list to keep track of notes that should be muted at the next iteration.
      // Replaces activeList at the end of this iter.
      std::list<NoteEvent*> nextActive;

      // Check if we have a special command.
      {
         // Tempo change.
         TempoEvent *e = dynamic_cast<TempoEvent*>(eventVec.front());
         if (e != NULL)
         {
            tempo = e->tempo;
            continue;
         }
      }

      {
         // Note size change.
         BarEvent *e = dynamic_cast<BarEvent*>(eventVec.front());
         if (e != NULL)
         {
            if (e->nom > 0)
               quantz = e->nom;
            continue;
         }
      }

      // Start new notes.
      for (std::vector<Event*>::iterator jt = eventVec.begin(); jt != eventVec.end(); jt ++)
      {
         unsigned stopChannel = UINT_MAX;
         std::list<NoteEvent*> nextActives;

         {
            // A regular note.
            NoteEvent *e = dynamic_cast<NoteEvent*>(*jt);
            if (e != NULL)
            {
               // Mark the column on which we need to mute previous notes.
               stopChannel = e->column;

               // Queue the note on event.
               gJack.queueMidiEvent(MIDI_NOTE_ON, e->pitch, e->volume,
                     currentTime + gJack.msToNframes(e->delay)
                     + (e->partDiv != 0 ? (gJack.msToNframes(60 * 1000 / tempo /quantz) * e->partDelay / e->partDiv) : 0)
                     + e->column,
                     seq.getPortMap(e->column).channel, seq.getPortMap(e->column).port);

               if (!e->endless)
               {
                  if (e->time == 0 && e->partTime == 0)
                     // If the note does not have specific sound time, turn it off at the next cycle.
                     nextActives.push_back(e);
                  else
                     // If the note has specific time, schedule the off event right now.
                     gJack.queueMidiEvent(MIDI_NOTE_OFF, e->pitch, e->volume,
                           currentTime + gJack.msToNframes(e->delay)
                           + (e->partDiv != 0 ? (gJack.msToNframes(60 * 1000 / tempo / quantz) * e->partDelay / e->partDiv) : 0)
                           + gJack.msToNframes(e->time)
                           + (e->partDiv != 0 ? (gJack.msToNframes(60 * 1000 / tempo / quantz) * e->partTime / e->partDiv) : 0) - 2,
                           seq.getPortMap(e->column).channel, seq.getPortMap(e->column).port);
               }
            }
         }

         {
            // Midi control.
            MidiCtlEvent *e = dynamic_cast<MidiCtlEvent*>(*jt);
            if (e != NULL)
            {
               stopChannel = e->column;

               // This is a control message to the midi.
               gJack.queueMidiEvent(MIDI_CONTROLLER, e->controller, e->value,
                     currentTime + (gJack.msToNframes(60 * 1000 / tempo / quantz) * e->delay / e->delayDiv),
                     seq.getPortMap(e->column).channel, seq.getPortMap(e->column).port);
            }
         }

         {
            // Skip a bit. Silence the previous notes.
            SkipEvent *e = dynamic_cast<SkipEvent*>(*jt);
            if (e != NULL)
            {
               stopChannel = e->column;
            }
         }
         
         if (stopChannel != UINT_MAX)
         {
            // Resize the channel vector if needed.
            if (stopChannel >= activeNotesVec.size())
               activeNotesVec.resize(stopChannel + 1);

            // Stop previous notes on this channel.
            for (std::list<NoteEvent*>::iterator activeIt = activeNotesVec[stopChannel].begin();
                  activeIt != activeNotesVec[stopChannel].end();
                  activeIt ++)
            {
               jack->queueMidiEvent(MIDI_NOTE_OFF, (*activeIt)->pitch, 0, currentTime - 1 - stopChannel,
                     seq.getPortMap(stopChannel).channel, seq.getPortMap(stopChannel).port);
            }

            activeNotesVec[stopChannel] = nextActives;
         }
      }

      currentTime += gJack.msToNframes(60 * 1000 / tempo / quantz);
   }  // End of the main loop.

   // Queue NOTE_OFF for the remaining notes.
   for (std::vector<std::list<NoteEvent*>>::iterator it = activeNotesVec.begin();
        it != activeNotesVec.end(); it ++)
   {
      while (!(*it).empty())
      {
         NoteEvent *n = (*it).front();
         (*it).pop_front();

         gJack.queueMidiEvent(MIDI_NOTE_OFF, n->pitch, 0, currentTime - 1,
               seq.getPortMap(n->column).channel, seq.getPortMap(n->column).port);
      }
   }

   // Wait for all events to be processed.
   while (gJack.hasPendingEvents() & gPlaying)
      usleep(200000);
   usleep(200000);

   gPlaying = false;
}

/*******************************************************************************************/
/* main */
int main(int argc, char **argv)
{
   std::string line;
   Parser parser;
   
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
   try {
      gJack.init();
   } catch (std::string &s) {
      std::cout << "Error during Jack initialization: " << s << std::endl;
   }
   
   // Init the sequencer and load the pattern.
   Sequencer seq (&gJack);
   seq.readFromStream(std::cin);

   // Play the pattern.
   play(&gJack, seq);

   // Shutdown the client and exit.
   gJack.stopSounds();
   sleep(1);

   gJack.shutdown();

   return 0;
}
