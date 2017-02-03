#include "midiheap.h"

#include <stdint.h>
#include <stdlib.h>

/***************************************************/
/* Swap two elements with the given indicies. */
inline void MidiHeap::swap(size_t i, size_t j)
{
   MidiMessage t = mArray[i];
   mArray[i] = mArray[j];
   mArray[j] = t;
}

/***************************************************/
/* Return an index of the element which is parent to the element with the given index. */
size_t MidiHeap::parent(size_t i)
{
   return (i + 1) / 2 - 1;
}

/***************************************************/
/* Return an index of the left child of the given index element. */
inline size_t MidiHeap::lchild(size_t i)
{
   return (i + 1) * 2 - 1;
}

/***************************************************/
/* Return an index of the right child of the given index element. */
inline size_t MidiHeap::rchild(size_t i)
{
   return (i + 1) * 2;
}

/***************************************************/
/* Return an index of the smallest element of two given index elements. */
inline size_t MidiHeap::imin(size_t i, size_t j)
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
void MidiHeap::bubbleDown(size_t i)
{
   size_t j = imin(lchild(i), rchild(i));

   while (j != (size_t)-1 && mArray[i].time > mArray[j].time)
   {
      swap(i, j);
      i = j;
      j = imin(lchild(i), rchild(i));
   }
}

/***************************************************/
MidiHeap::MidiHeap(size_t s)
{
   mArray = new MidiMessage[s];
   mSize = s;
   pthread_mutex_init(&mMutex, NULL);
}

/***************************************************/
MidiHeap::~MidiHeap()
{
   delete mArray;
   pthread_mutex_destroy(&mMutex);
}

/***************************************************/
/* Add a new element while maintaining the order.
   The function locks until there is a space in the buffer. */
void MidiHeap::insert(MidiMessage &msg)
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
MidiMessage MidiHeap::popMin()
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
MidiMessage MidiHeap::peekMin()
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
size_t MidiHeap::count()
{
   return mTop;
}
