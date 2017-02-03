#ifndef MIDIHEAP_H
#define MIDIHEAP_H

#include <pthread.h>

#include "midimessage.h"

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

      /* Swap two elements with the given indicies. */
      inline void swap(size_t i, size_t j);

      /* Return an index of the element which is parent to the element with the given index. */
      inline size_t parent(size_t i);

      /* Return an index of the left child of the given index element. */
      inline size_t lchild(size_t i);

      /* Return an index of the right child of the given index element. */
      inline size_t rchild(size_t i);

      /* Return an index of the smallest element of two given index elements. */
      inline size_t imin(size_t i, size_t j);

      /* Rearrange the buffer to maintain the order. */
      void bubbleDown(size_t i);

   public:
      /* Constructor. */
      MidiHeap(size_t s);

      /* Destructor. */
      ~MidiHeap();

      /* Add a new element while maintaining the order.
         The function locks until there is a space in the buffer. */
      void insert(MidiMessage &msg);

      /* Pop the minimal element.
         The function locks until there is an element to read. */
      MidiMessage popMin();

      /* Look the minimal element without removing it from the buffer.
         The function locks until there is an element to look at. */
      MidiMessage peekMin();

      /* The number of elements available in the buffer. */
      size_t count();
};

#endif
