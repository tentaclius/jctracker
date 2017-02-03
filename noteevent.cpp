#include "noteevent.h"

#include <sstream>

NoteEvent::NoteEvent(unsigned n, unsigned v, uint64_t tm, uint64_t dl, unsigned col)
   : pitch(n)
   , volume(v)
   , delay(dl)
   , time(tm)
   , partDelay(0)
   , partTime(0)
   , partDiv(1)
   , natural(false)
     , endless(false)
{
   column = col;
}

NoteEvent::NoteEvent()
{
   NoteEvent(0,64,0,0,0);
}

/***************************************************/
/* Constructor that parses the note from the string. */
NoteEvent::NoteEvent(const std::string &buf, unsigned aColumn)
{
   column = aColumn;
   const int octaveLen = 12;
   natural = false;

   pitch = 0;
   volume = (unsigned)-1;
   time = 0;
   delay = 0;
   partTime = 0;
   partDelay = 0;
   partDiv = 1;

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
void NoteEvent::set(unsigned n, unsigned v, uint64_t tm, uint64_t dl)
{
   pitch = n;
   volume = v;
   time = tm;
   delay = dl;
}

/***************************************************/
NoteEvent::~NoteEvent()
{
}

/***************************************************/
/* Return a new instance of the same data. */
NoteEvent* NoteEvent::clone()
{
   return new NoteEvent(pitch, volume, time, delay, column);
}

/***************************************************/
/* Virtual functions to start/stop the note. */
/*
   void stop(JackEngine *jack, Sequencer *seq);
   ControlFlow execute(JackEngine *jack, Sequencer *seq);
   */
