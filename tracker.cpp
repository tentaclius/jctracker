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

// Roadmap:
// [v] Set up jackd skeleton.
// [v] Try sending few single note_on note_off midi events to the gJack port in process() callback.
// [v] Signal handler to send midi stop all event.
// [v] Write the sequencer: note_on at the note appearing; note_off at the next note in the row.
// [v] Pedaled notes.
// [v] Sort upcoming midi events by time with a heap.
// [v] Find a proper way to handle delayed and timed notes.
// [v] Create event vectors with pre-defined size.
// [v] Note playing time.
// [v] Process empty notes, default notes and other templates.
// [v] Directives: volume, default, tempo etc.
// [v] Handle loops.
// [v] Separate threads for different tasks.
// [v] Implement bar event and sizes support and tempo changes.
// [v] Transposition ("transpose 20")..
// [v] Initial signs (bemol/dies) applied to all notes.
// [v] Aliases for the notes with _hash_tables_!
// [v] Proper time management.
// [v] Make the transposition static.
// [v] Ports and channels assignment.
// [v] Automatic connection.
// [v] Move the patterns and the player into separate classes.
// [v] Note length and timing in parts of current note length.
// [-] Store output ports in an unsigned indexed vector. Store the index in Midi messages.
// [-] Pattern file management: load, unload, reload of multiple patterns.
// [ ] Transposition separately for each column.
// [ ] Unicode sharp/flat/natural signs.
// [ ] Jack transport.
// [ ] Automation channel.
// [ ] MIDI control messages.
// [ ] Input MIDI port to record notes from a MIDI keyboard.

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
      MidiMessage     *ar;
      pthread_mutex_t  mutex;
      pthread_cond_t   canWrite;
      pthread_cond_t   canRead;
      size_t           size;               // Maximum size (or array size).
      size_t           top;                // Current top position.

      /***************************************************/
      /* Swap two elements with the given indicies. */
      inline void swap(size_t i, size_t j)
      {
         MidiMessage t = ar[i];
         ar[i] = ar[j];
         ar[j] = t;
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
         if (i < top && j >= top)
            return i;
         if (i >= top && j < top)
            return j;
         if (i >= top && j >= top)
            return (size_t) -1;

         // Compare the two by the time and the port number.
         if (ar[i].time < ar[j].time)
            return i;
         else if (ar[i].time > ar[j].time)
            return j;
         else if (ar[i].port < ar[j].port)
            return i;
         else
            return j;
      }

      /***************************************************/
      /* Rearrange the buffer to maintain the order. */
      void bubbleDown(size_t i)
      {
         size_t j = imin(lchild(i), rchild(i));

         while (j != (size_t)-1 && ar[i].time > ar[j].time)
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
         ar = new MidiMessage[s];
         size = s;
         pthread_mutex_init(&mutex, NULL);
      }

      /***************************************************/
      ~MidiHeap()
      {
         delete ar;
         pthread_mutex_destroy(&mutex);
      }

      /***************************************************/
      /* Add a new element while maintaining the order.
         The function locks until there is a space in the buffer. */
      void insert(MidiMessage &msg)
      {
         pthread_mutex_lock(&mutex);

         if (top + 1 >= size)
            pthread_cond_wait(&canWrite, &mutex);

         size_t i = top ++;
         ar[i] = msg;

         while (parent(i) != (size_t)-1 && ar[i].time < ar[parent(i)].time)
         {
            swap(i, parent(i));
            i = parent(i);
         }

         pthread_cond_broadcast(&canRead);
         pthread_mutex_unlock(&mutex);
      }

      /***************************************************/
      /* Pop the minimal element.
         The function locks until there is an element to read. */
      MidiMessage popMin()
      {
         pthread_mutex_lock(&mutex);

         if (top == 0)
            pthread_cond_wait(&canRead, &mutex);

         MidiMessage min = ar[0];
         ar[0] = ar[top - 1];
         top --;
         bubbleDown(0);

         pthread_cond_broadcast(&canWrite);
         pthread_mutex_unlock(&mutex);

         return min;
      }

      /***************************************************/
      /* Look the minimal element without removing it from the buffer.
         The function locks until there is an element to look at. */
      MidiMessage peekMin()
      {
         MidiMessage min;

         pthread_mutex_lock(&mutex);
         if (top == 0)
            pthread_cond_wait(&canRead, &mutex);

         min = ar[0];
         pthread_mutex_unlock(&mutex);

         return min;
      }

      /***************************************************/
      /* The number of elements available in the buffer. */
      size_t count()
      {
         return top;
      }
};

/*******************************************************************************************/
/* Manage Jack connection and hide specific objects. */
class JackEngine
{
   private:
      MidiHeap          *midiHeap;        // A sorted queue of midi events.
      jack_client_t     *client;          // The client representation.
      jack_ringbuffer_t *ringbuffer;
      jack_nframes_t     bufferSize;

      pthread_t          midiWriteThread;

      std::vector<jack_port_t*> outputPorts;
      size_t                    portCount;

      /***************************************************/
      /* Write the midi message into the ringbuffer which is processed by jack callback in its turn. */
      void writeMidiData(MidiMessage theMessage)
      {
         if (jack_ringbuffer_write_space(ringbuffer) > sizeof(MidiMessage))
         {
            if (jack_ringbuffer_write(ringbuffer, (const char*) &theMessage, sizeof(MidiMessage)) != sizeof(MidiMessage))
               std::cerr << "WARNING! Midi message is not written entirely." << std::endl;
         }
         return;
      }

   public:
      jack_port_t       *input_port;      // Isn't used yet.
      jack_port_t       *defaultOutputPort;

      jack_nframes_t     sampleRate;

      /***************************************************/
      JackEngine()
      {
         portCount = 0;
      }

      /***************************************************/
      ~JackEngine()
      {
         delete midiHeap;
      }

      /***************************************************/
      /* Initialization and activation of jack interface. */
      void init()
      {
         jack_options_t options = JackNullOption;
         jack_status_t  status;

         // Midi event heap.
         midiHeap = new MidiHeap(256);

         // Create the ringbuffer.
         ringbuffer = jack_ringbuffer_create(1024 * sizeof(MidiMessage));

         if ((client = jack_client_open("jctracker", options, &status)) == 0)
            throw "Jack server is not running.";

         // Set the callbacks.
         jack_set_process_callback(client, jack_process_cb, (void*) this);
         jack_on_shutdown(client, jack_shutdown_cb, (void*) this);

         sampleRate = jack_get_sample_rate(client);

         // Create two ports.
         input_port  = jack_port_register(client, "input",  JACK_DEFAULT_MIDI_TYPE, JackPortIsInput,  0);
         defaultOutputPort = jack_port_register(client, "default", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
         outputPorts.push_back(defaultOutputPort);

         // Find out the buffer size.
         bufferSize = jack_get_buffer_size(client);

         if (jack_activate(client))
            throw "cannot activate Jack client";

         pthread_create(&midiWriteThread, NULL, bufferProcessingThread, this);
      }

      /***************************************************/
      /* Register an output port. */
      jack_port_t* registerOutputPort(std::string name)
      {
         for (std::vector<jack_port_t*>::iterator it = outputPorts.begin();
              it != outputPorts.end(); it ++)
         {
            if (name == jack_port_short_name(*it))
               return *it;
         }

         jack_port_t *p = jack_port_register(client, name.c_str(), JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
         outputPorts.push_back(p);
         return p;
      }

      /***************************************************/
      /* Connect to a port. */
      int connectPort(jack_port_t *port, std::string destination)
      {
         return jack_connect(client, jack_port_name(port), destination.c_str());
      }

      /***************************************************/
      /* Shutdown the jack interface. */
      void shutdown()
      {
         jack_port_unregister(client, input_port);
         jack_port_unregister(client, defaultOutputPort);
         jack_client_close(client);
      }

      /***************************************************/
      /* Convert microsecond time to jack nframes. */
      jack_nframes_t msToNframes(uint64_t ms)
      {
         return ms * sampleRate / 1000;
      }

      /***************************************************/
      /* Return the current time in nframes. */
      jack_nframes_t currentFrameTime()
      {
         return jack_frame_time(client);
      }

      /***************************************************/
      /* Is there are unprocessed midi events. */
      bool hasPendingEvents()
      {
         return midiHeap->count() > 0;
      }

      /***************************************************/
      /* Put a midi message into the heap. */
      void queueMidiEvent(MidiMessage &message)
      {
         midiHeap->insert(message);
      }

      /***************************************************/
      /* Put a midi message into the heap. */
      void queueMidiEvent(unsigned char b0, unsigned char b1, unsigned char b2, jack_nframes_t time,
            unsigned channel = 0, jack_port_t *port = NULL)
      {
         MidiMessage msg (b0, b1, b2, time, channel, port);
         midiHeap->insert(msg);
      }

      /***************************************************/
      /* Send a control midi message to stop all sounds. */
      void stopSounds()
      {
         for (std::vector<jack_port_t*>::iterator it = outputPorts.begin();
              it != outputPorts.end(); it ++)
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
   jack_nframes_t lastFrameTime = jack_last_frame_time(jack->client);

   // Clear all buffer first.
   for (std::vector<jack_port_t*>::iterator it = jack->outputPorts.begin();
         it != jack->outputPorts.end();
         it ++)
   {
      void *pbuf = jack_port_get_buffer(*it, nframes);
      if (pbuf != NULL)
         jack_midi_clear_buffer(pbuf);
   }

   // Read the data from the ringbuffer.
   while (jack_ringbuffer_read_space(gJack.ringbuffer) >= sizeof(MidiMessage))
   {
      MidiMessage midiData;
      if (jack_ringbuffer_peek(gJack.ringbuffer, (char*)&midiData, sizeof(MidiMessage)) != sizeof(MidiMessage))
      {
         std::cerr << "WARNING! Incomplete MIDI message read." << std::endl;
         continue;
      }

      t = midiData.time + nframes - lastFrameTime;
      if (t >= (int)nframes)
         break;

      jack_ringbuffer_read_advance(gJack.ringbuffer, sizeof(MidiMessage));

      if (t < 0)
         t = 0;

      // Initialize output buffer.
      if (midiData.port == 0)
         midiData.port = jack->defaultOutputPort;
      if (portP != midiData.port)
      {
         // The port has changed since the previous iteration or was not yet initialize. Reinitialize the buffer.
         portP = midiData.port;

         // Use the default midi port if no port specified.
         if (portP == NULL)
            portP = jack->defaultOutputPort;

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

      trace("jack_process_cb: midi(%x,%x,%x,%d) t=%d\n", midiData.data[0], midiData.data[1], midiData.data[2], midiData.time, t);
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
      while (jack->midiHeap->peekMin().time <= jack->currentFrameTime() + 100)
         jack->writeMidiData(jack->midiHeap->popMin());

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
   
   uint64_t delay;      // Delay time for the note in microseconds.
   uint64_t time;       // Time of the note playing in microseconds.
   double   partDelay;  // Delay in parts of the current note.
   double   partTime;   // Playing time in parts of the current note.

   bool     natural;    // If the note is of natural tone.

   NoteEvent(unsigned n, unsigned v, uint64_t tm, uint64_t dl, unsigned col)
      : column(col)
      , pitch(n)
      , volume(v)
      , delay(dl)
      , time(tm)
      , partDelay(0)
      , partTime(0)
      , natural(false)
   {}

   NoteEvent() { NoteEvent(0,64,0,0,0); }

   // Constructor that parses the note from the string.
   NoteEvent(const std::string &buf)
   {
      const int octaveLen = 12;
      natural = false;

      pitch = 0;
      volume = (unsigned)-1;
      time = 0;
      delay = 0;
      partTime = 0;
      partDelay = 0;

      std::istringstream iss (buf);

      if (buf.length() == 0)
         throw 0;

      // The note
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
      if (iss.peek() == 'b')
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

            case '\\':
               // Playing time in parts of the current note length.
               iss >> partDelay;
               break;

            case '/':
               // Delay time in parts of the current note.
               iss >> partTime;
               break;

            case '!':
               // Volume modifier.
               iss >> volume;
               break;
         }
      }
   }

   // Set the parameters of the current midi message.
   void set(unsigned n, unsigned v, uint64_t tm, uint64_t dl)
   {
      pitch = n;
      volume = v;
      time = tm;
      delay = dl;
   }

   ~NoteEvent() {}

   // Return a new instance of the same data.
   NoteEvent* clone()
   {
      return new NoteEvent(pitch, volume, time, delay, column);
   }
};

/*******************************************************************************************/
/* Silent note or pause. */
struct SkipEvent : public Event
{
   SkipEvent() {}
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
   size_t channelNum;
   std::vector<NoteEvent> *lastNote;
   NoteEvent dfltNote;
   unsigned volume;
   std::vector<int> *signs;
   std::map<std::string, std::string> aliases;
   std::vector<PortMap> columnToPortMap;
   int transpose;

   public:
      // Create the parser.
      Parser(size_t chan = 64)
      {
         signs = new std::vector<int>(12, 0);
         channelNum = chan;
         lastNote = new std::vector<NoteEvent>(chan, NoteEvent(0,0,0,0,0));
         dfltNote.set(0,0,0,0);
         transpose = 0;
         volume = 64;
      }

      ~Parser()
      {
         delete lastNote;
         delete signs;
      }

      // Parse a given line (with one or multiple directives or patterns).
      std::vector<Event*> parseLine(std::string line)
      {
         std::string chunk;          // A piece of the line to read the command.
         std::istringstream iss (line);
         std::vector<Event*> eventList;
         //eventList.reserve(channelNum);

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
               else if (chunk[0] == 'b')
                  mod = -1;
               else if (chunk[0] == 'n')
                  mod = 0;

               // If the modifier is unchanged, then we should skip this.
               if (mod == INT_MAX)
                  continue;

               NoteEvent n (chunk.substr(1));
               signs->at(n.pitch % 12) = mod;
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
               dfltNote.set(n.pitch, n.volume, n.time, n.delay);
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
            iss >> volume;
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
            iss >> transpose;
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
            if (columnToPortMap.size() < columnB)
               columnToPortMap.resize(columnB);

            for (unsigned i = columnA; i <= columnB; i ++)
               columnToPortMap[i - 1] = PortMap(channel, port);

            // Try to link to the destination port.
            iss.clear();
            if (iss >> connClient)
               if (gJack.connectPort(port, connClient) != 0)
                  std::cerr << "WARNING! Can not connect to client " << connClient << std::endl;

            return eventList;
         }

         // New alias.
         if (chunk == "alias")
         {
            std::string alias, replacement;
            
            if (!(iss >> alias))
               throw iss.tellg();

            if (!(iss >> replacement))
            {
               aliases.erase(alias);
               return eventList;
            }

            aliases[alias] = replacement;
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

               // An aliased name.
               if (aliases.find(chunk) != aliases.end())
                  chunk = aliases[chunk];

               // Silent note.
               if (chunk == ".")
                  eventList.push_back(new SkipEvent());

               // Continuing the previous note.
               else if (chunk == "|")
                  eventList.push_back(new PedalEvent());

               // Default note.
               else if (chunk == "*")
                  eventList.push_back(dfltNote.clone());

               // Previous note.
               else if (chunk == "^")
                  eventList.push_back(lastNote->at(column).clone());

               // And finally this must be a real note:
               else
               {
                  NoteEvent *n = new NoteEvent(chunk);

                  // Aply modifiers.
                  if (n->volume == (unsigned)-1)
                     n->volume = volume;                        // Apply the default volume.
                  if (!n->natural)
                     n->pitch += signs->at(n->pitch % 12);      // Apply the default sign.
                  n->pitch += transpose;                        // Apply the transposition.
                  n->column = column;

                  // Store the value for the lastNote pattern.
                  lastNote->at(column).set(n->pitch, n->volume, n->time, n->delay);

                  // Push the event into the return list.
                  eventList.push_back(n);
               }
            }
            catch (int e)
            {
               throw (int) iss.tellg();
            }
            column ++;
         }

         return eventList;
      }

      PortMap& getPortMap(unsigned column)
      {
         static PortMap dfltMap (0, NULL);
         if (column >= columnToPortMap.size())
            return dfltMap;
         return columnToPortMap[column];
      }
};

/*******************************************************************************************/
/* Interpret and process the pattern line by line. */
class Sequencer
{
   std::vector<std::vector<Event*> > song;
   Parser parser;
   size_t currentPos;
   std::list<std::pair<int, unsigned> > loopStack;

   public:
      Sequencer(JackEngine *j)
      {
         currentPos = 0;
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
               std::vector<Event*> lst = parser.parseLine(line);
               if (!lst.empty())
                  song.push_back(lst);
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
         if (currentPos >= song.size())
            return std::vector<Event*> ();

         LoopEvent     *lp = dynamic_cast<LoopEvent*>(song[currentPos][0]);
         EndLoopEvent *elp = dynamic_cast<EndLoopEvent*>(song[currentPos][0]);

         if (lp || elp)
         {
            if (lp != NULL)
               // The beginning of a loop; push the starting point to the loop stack.
               loopStack.push_back(std::pair<int, unsigned>(lp->count, currentPos));

            else if (elp != NULL)
            {
               // End of the loop; move the pointer to the beginning of the loop.
               if (loopStack.size() > 0)
               {
                  if (loopStack.back().first == -1 || (-- loopStack.back().first) > 0)
                     currentPos = loopStack.back().second;
                  else
                     loopStack.pop_back();
               }
            }

            currentPos ++;
            return getNextLine();
         }

         else
            return song[currentPos ++];
      }

      /*****************************************************/
      /* Return the mapping of the event's column to the port and channel. */
      PortMap& getPortMap(unsigned column)
      {
         return parser.getPortMap(column);
      }
};

/*****************************************************************************************************/
/* Read the data from the sequencer and queue the midi events to Jack */
void play(JackEngine *jack, Sequencer &seq)
{
   // Runtime values.
   std::list<std::pair<unsigned, unsigned> > activeNotes;
   unsigned tempo = 100;
   unsigned quantz = 4;

   jack_nframes_t currentTime = jack->currentFrameTime();

   for (std::vector<Event*> eventVec = seq.getNextLine();
        !eventVec.empty() && gPlaying;
        eventVec = seq.getNextLine())
   {
      // The list to keep track of notes that should be muted at the next iteration.
      // Replaces activeList at the end of this iter.
      std::list<std::pair<unsigned, unsigned> > nextActive;

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

      // Silence the previous notes.
      while (!activeNotes.empty())
      {
         std::pair<unsigned, unsigned> n = activeNotes.front();
         activeNotes.pop_front();

         // Check if we have a pedal event in this iteration for the previous note.
         PedalEvent *pedal = dynamic_cast<PedalEvent*>(eventVec[n.second]);
         if (eventVec.size() > n.second && pedal != NULL)
            nextActive.push_back(n);
         else
            gJack.queueMidiEvent(MIDI_NOTE_OFF, n.first, 0, currentTime - 1,
                  seq.getPortMap(n.second).channel, seq.getPortMap(n.second).port);
      }
      activeNotes = nextActive;

      // Start new notes.
      unsigned column = 0;
      for (std::vector<Event*>::iterator jt = eventVec.begin(); jt != eventVec.end(); jt ++)
      {
         NoteEvent *e = dynamic_cast<NoteEvent*>(*jt);
         if (e != NULL)
         {
            if (e->time == 0 && e->partTime == 0)
            {
               // If the note does not have specific sound time, turn it off at the next cycle.
               std::pair<unsigned, unsigned> n;
               n.first = e->pitch;                  // pitch;
               n.second = column;                   // channel
               activeNotes.push_back(n);
            }
            else
            {
               // If the note has specific time, schedule the off event right now.
               gJack.queueMidiEvent(MIDI_NOTE_OFF, e->pitch, e->volume,
                     currentTime + gJack.msToNframes(e->delay) + gJack.msToNframes(60 * 1000 / tempo / quantz) * e->partDelay 
                      + gJack.msToNframes(e->time) + gJack.msToNframes(60 * 1000 / tempo / quantz) * e->partTime - 2,
                     seq.getPortMap(e->column).channel, seq.getPortMap(e->column).port);
            }

            // Queue the note on event.
            gJack.queueMidiEvent(MIDI_NOTE_ON, e->pitch, e->volume,
                  currentTime + gJack.msToNframes(e->delay) + gJack.msToNframes(60 * 1000 / tempo /quantz) * e->partDelay,
                  seq.getPortMap(e->column).channel, seq.getPortMap(e->column).port);
         }

         column ++;
      }

      currentTime += gJack.msToNframes(60 * 1000 / tempo / quantz);
   }  // End of the main loop.

   // Queue NOTE_OFF for the remaining notes.
   while (!activeNotes.empty() && gPlaying)
   {
      std::pair<unsigned, unsigned> n = activeNotes.front();
      activeNotes.pop_front();

      gJack.queueMidiEvent(MIDI_NOTE_OFF, n.first, 0, currentTime - 1,
            seq.getPortMap(n.second).channel, seq.getPortMap(n.second).port);
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
   action.sa_flags = 0;

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
   gJack.shutdown();

   return 0;
}
