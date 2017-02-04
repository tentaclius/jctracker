#include "common.h"

bool gPlaying = false;
 
#ifdef DEBUG
#define trace(...) {TRACE(__FILE__, __LINE__,  __VA_ARGS__);}
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

