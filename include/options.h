/*
 * Command-line options.
 *
 * Copyright 1994 Alexandre Julliard
 */

#ifndef OPTIONS_H
#define OPTIONS_H

struct options
{
    char * spyFilename;
    char * desktopGeometry; /* NULL when no desktop */
    char * programName;     /* To use when loading resources */
    int    usePrivateMap;
    int    synchronous;     /* X synchronous mode */
    int    backingstore;    /* Use backing store */
    short  cmdShow;
    int    debug;
    int    allowReadOnly;   /* Opening a read only file will succeed even
			       if write access is requested */
};

extern struct options Options;

#endif
