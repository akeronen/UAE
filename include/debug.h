 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Debugger
  *
  * (c) 1995 Bernd Schmidt
  *
  */

#ifdef DEBUGGER

#define	MAX_HIST	10000

extern int firsthist;
extern int lasthist;
extern int debugging;
#ifdef NEED_TO_DEBUG_BADLY
extern struct regstruct history[MAX_HIST];
extern union flagu historyf[MAX_HIST];
#else
extern uaecptr history[MAX_HIST];
#endif
extern int exception_debugging;

extern void debug(void);
extern void activate_debugger(void);
extern int notinrom (void);
extern const char *debuginfo(int);

#else

STATIC_INLINE void activate_debugger (void) { };

#endif