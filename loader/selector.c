/*
 * Selector manipulation functions
 *
 * Copyright 1993 Robert J. Amstadt
 * Copyright 1995 Alexandre Julliard
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef WINELIB

#ifdef __linux__
#include <sys/mman.h>
#include <linux/unistd.h>
#include <linux/head.h>
#include <linux/mman.h>
#include <linux/a.out.h>
#include <linux/ldt.h>
#endif
#if defined(__NetBSD__) || defined(__FreeBSD__)
#include <sys/mman.h>
#include <machine/segments.h>
#endif

#include "windows.h"
#include "ldt.h"
#include "wine.h"

#include "dlls.h"
#include "neexe.h"
#include "if1632.h"
#include "prototypes.h"
#include "stddebug.h"
/* #define DEBUG_SELECTORS */
#include "debug.h"


#define MAX_ENV_SIZE 16384  /* Max. environment size (ought to be dynamic) */

static HANDLE EnvironmentHandle = 0;
WORD PSPSelector = 0;

#define MAX_SELECTORS (512 * 2)

int max_selectors = 0;


extern void KERNEL_Ordinal_1();    /* FatalExit() */
extern void KERNEL_Ordinal_102();  /* Dos3Call() */
extern char WindowsPath[256];

extern char **Argv;
extern int Argc;
extern char **environ;

unsigned int 
GetEntryPointFromOrdinal(struct w_files * wpnt, int ordinal);

#if 0


/**********************************************************************
 *					InitSelectors
 */
void
InitSelectors(void) 
{
    int i;
    max_selectors = MAX_SELECTORS;
    SelectorMap = malloc(max_selectors * sizeof(unsigned short));
    Segments    = malloc(max_selectors * sizeof(SEGDESC));
    for (i = 0; i < max_selectors; i++) {
	if (i < FIRST_SELECTOR) {
	    SelectorMap[i] = SELECTOR_IS32BIT;
#ifdef __ELF__
	    /* quick hack, just reserves 4 meg for wine. */
	} else if ((i << (16 + __AHSHIFT)) >= 0x8000000 &&
		   (i << (16 + __AHSHIFT)) <= 0x8400000) {
	    SelectorMap[i]= SELECTOR_IS32BIT;
#endif
	} else {
	    SelectorMap[i]=0;
	}
    }
#ifdef __ELF__
    /* create an ldt. */
    if (set_ldt_entry(1, 0x8000000, 65535, 1,0x1a ,1,0)) {
	perror ("set_ldt_entry");
	exit (1);
    }
#endif
    }


#endif /* 0 */




/**********************************************************************
 *  Check whether pseudo-functions like __0040H for direct memory
 *  access are referenced and return 1 if so.
 *  FIXME: Reading and writing to the returned selectors has no effect
 *         (e.g. reading from the Bios data segment (esp. clock!) )
 */

unsigned int GetMemoryReference(char *dll_name, char *function, WORD *sel,
				 int *addr)
{
  static HANDLE memory_handles[ 10 ] = { 0,0,0,0,0,0,0,0,0,0 };
  static char *memory_names[ 10 ] = { "segment 0xA000",
					"segment 0xB000",
					"segment 0xB800",
					"Bios-Rom",
					"segment 0xD000",
					"segment 0x0000",
					"segment 0xE000",
					"segment 0xF000",
					"segment 0xC000",
					"Bios data segment" };
  short nr;

  if( strcasecmp( dll_name, "KERNEL" ) ) 
    return 0;

  if( HIWORD( function ) ) {
    if( ( *function != '_' ) || ( *(function+1) != '_' ) )
      return 0;
    if( !strcasecmp( function, "__A000H" ) ) nr = 0;
    else if( !strcasecmp( function, "__B000H" ) ) nr = 1;
    else if( !strcasecmp( function, "__B800H" ) ) nr = 2;
    else if( !strcasecmp( function, "__ROMBIOS" ) ) nr = 3;
    else if( !strcasecmp( function, "__D000H" ) ) nr = 4;
    else if( !strcasecmp( function, "__0000H" ) ) nr = 5;
    else if( !strcasecmp( function, "__E000H" ) ) nr = 6;
    else if( !strcasecmp( function, "__F000H" ) ) nr = 7;
    else if( !strcasecmp( function, "__C000H" ) ) nr = 8;
    else if( !strcasecmp( function, "__0040H" ) ) nr = 9;
    else if( !strcasecmp( function, "__AHIncr" ) ) {
      *sel = __AHINCR;   
      *addr = MAKELONG(__AHINCR,__AHINCR);
      return 1;      
    }
    else if( !strcasecmp( function, "__AHShift" ) ) {
      *sel = __AHSHIFT;
      *addr = MAKELONG(__AHSHIFT,__AHSHIFT);
      return 1;
    }
     else
      return 0;
  }
  else {
    switch( LOWORD( function ) ) {
    case 174: nr = 0; break;
    case 181: nr = 1; break;
    case 182: nr = 2; break;
    case 173: nr = 3; break;
    case 179: nr = 4; break;
    case 183: nr = 5; break;
    case 190: nr = 6; break;
    case 194: nr = 7; break;
    case 195: nr = 8; break;
    case 193: nr = 9; break;
    case 114:
      *sel = __AHINCR;  
      *addr = MAKELONG(__AHINCR,__AHINCR);
      return 1;
    case 113:
      *sel = __AHSHIFT;
      *addr = MAKELONG(__AHSHIFT,__AHSHIFT);
      return 1;
    default: return 0;
    }
  }
  
  if( !memory_handles[ nr ] ) {
    fprintf( stderr, "Warning: Direct access to %s!\n", memory_names[ nr ] );
    memory_handles[ nr ] = GlobalAlloc( GMEM_FIXED, 65535 );
  }
  *sel = memory_handles[ nr ];
  *addr = MAKELONG(*sel,*sel);
  return 1;
}
 


/**********************************************************************
 *					GetEntryPointFromOrdinal
 */
union lookup{
    struct entry_tab_header_s *eth;
    struct entry_tab_movable_s *etm;
    struct entry_tab_fixed_s *etf;
    char  * cpnt;
};

unsigned int GetEntryDLLName(char * dll_name, char * function, WORD* sel, 
				int  * addr)
{
	struct dll_table_entry_s *dll_table;
	struct w_files * wpnt;
	char * cpnt;
	int ordinal, j, len;

	if( GetMemoryReference( dll_name, function, sel, addr ) )
	  return 0;

	dll_table = FindDLLTable(dll_name);

	if(dll_table) {
		ordinal = FindOrdinalFromName(dll_table, function);
		if(!ordinal){
			dprintf_module(stddeb,"GetEntryDLLName: %s.%s not found\n",
				dll_name, function);
			*sel=0;
			*addr=0;
			return -1;
		}
		*sel = dll_table[ordinal].selector;
		*addr  = (unsigned int) dll_table[ordinal].address;
#ifdef WINESTAT
		dll_table[ordinal].used++;
#endif
		return 0;
	};

	/* We need a means  of determining the ordinal for the function. */
	/* Not a builtin symbol, look to see what the file has for us */
	for(wpnt = wine_files; wpnt; wpnt = wpnt->next){
		if(strcasecmp(wpnt->name, dll_name)) continue;
		cpnt  = wpnt->ne->nrname_table;
		while(1==1){
			if( ((int) cpnt)  - ((int)wpnt->ne->nrname_table) >  
			   wpnt->ne->ne_header->nrname_tab_length)  return 1;
			len = *cpnt++;
			if(strncmp(cpnt, function, len) ==  0) break;
			cpnt += len + 2;
		};
		ordinal =  *((unsigned short *)  (cpnt +  len));
		j = GetEntryPointFromOrdinal(wpnt, ordinal);		
		*addr  = j & 0xffff;
		j = j >> 16;
		*sel = j;
		return 0;
	};
	return 1;
}

unsigned int GetEntryDLLOrdinal(char * dll_name, int ordinal, WORD *sel, 
				int  * addr)
{
	struct dll_table_entry_s *dll_table;
	struct w_files * wpnt;
	int j;

	if( GetMemoryReference( dll_name, (char*)ordinal, sel, addr ) )
	  return 0;

	dll_table = FindDLLTable(dll_name);

	if(dll_table) {
	    *sel = dll_table[ordinal].selector;
	    *addr  = (unsigned int) dll_table[ordinal].address;
#ifdef WINESTAT
		dll_table[ordinal].used++;
#endif
	    return 0;
	};

	/* Not a builtin symbol, look to see what the file has for us */
	for(wpnt = wine_files; wpnt; wpnt = wpnt->next){
		if(strcasecmp(wpnt->name, dll_name)) continue;
		j = GetEntryPointFromOrdinal(wpnt, ordinal);
		*addr  = j & 0xffff;
		j = j >> 16;
		*sel = j;
		return 0;
	};
	return 1;
}

unsigned int 
GetEntryPointFromOrdinal(struct w_files * wpnt, int ordinal)
{
    union lookup entry_tab_pointer;
    struct entry_tab_header_s *eth;
    struct entry_tab_movable_s *etm;
    struct entry_tab_fixed_s *etf;
    int current_ordinal;
    int i;
    
   entry_tab_pointer.cpnt = wpnt->ne->lookup_table;
    /*
     * Let's walk through the table until we get to our entry.
     */
    current_ordinal = 1;
    while (1)
    {
	/*
	 * Read header for this bundle.
	 */
	eth = entry_tab_pointer.eth++;
	
	if (eth->n_entries == 0)
	    return 0xffffffff;  /* Yikes - we went off the end of the table */

	if (eth->seg_number == 0)
	{
	    current_ordinal += eth->n_entries;
	    if(current_ordinal > ordinal) return 0;
	    continue;
	}

	/*
	 * Read each of the bundle entries.
	 */
	for (i = 0; i < eth->n_entries; i++, current_ordinal++)
	{
	    if (eth->seg_number >= 0xfe)
	    {
                etm = entry_tab_pointer.etm++;
		if (current_ordinal == ordinal)
		{
		    return MAKELONG(etm->offset,
                                  wpnt->ne->selector_table[etm->seg_number-1]);
		}
	    }
	    else
	    {
                etf = entry_tab_pointer.etf++;
		if (current_ordinal == ordinal)
		{
		    return MAKELONG( etf->offset[0] + ((int)etf->offset[1]<<8),
                                  wpnt->ne->selector_table[eth->seg_number-1]);
		}
	    }
	}
    }
}


/***********************************************************************
 *           GetDOSEnvironment   (KERNEL.131)
 */
SEGPTR GetDOSEnvironment(void)
{
    return WIN16_GlobalLock( EnvironmentHandle );
}


/**********************************************************************
 *           CreateEnvironment
 */
static HANDLE CreateEnvironment(void)
{
    HANDLE handle;
    char **e;
    char *p;

    handle = GlobalAlloc( GMEM_MOVEABLE, MAX_ENV_SIZE );
    if (!handle) return 0;
    p = (char *) GlobalLock( handle );

    /*
     * Fill environment with Windows path, the Unix environment,
     * and program name.
     */
    strcpy(p, "PATH=");
    strcat(p, WindowsPath);
    p += strlen(p) + 1;

    for (e = environ; *e; e++)
    {
	if (strncasecmp(*e, "path", 4))
	{
	    strcpy(p, *e);
	    p += strlen(p) + 1;
	}
    }

    *p++ = '\0';

    /*
     * Display environment
     */
    p = (char *) GlobalLock( handle );
    dprintf_selectors(stddeb, "Environment at %p\n", p);
    for (; *p; p += strlen(p) + 1) dprintf_selectors(stddeb, "    %s\n", p);

    return handle;
}


/**********************************************************************
 *           GetCurrentPDB   (KERNEL.37)
 */
WORD GetCurrentPDB()
{
    return PSPSelector;
}


/**********************************************************************
 *           CreatePSP
 */
static WORD CreatePSP(void)
{
    HANDLE handle;
    struct dos_psp_s *psp;
    unsigned short *usp;
    char *p1, *p2;
    int i;

    handle = GlobalAlloc( GMEM_MOVEABLE, sizeof(*psp) );
    if (!handle) return 0;
    psp = (struct dos_psp_s *) GlobalLock( handle );

    /*
     * Fill PSP
     */
    psp->pspInt20 = 0x20cd;
    psp->pspDispatcher[0] = 0x9a;
    usp = (unsigned short *) &psp->pspDispatcher[1];
    *usp       = (unsigned short) KERNEL_Ordinal_102;
    *(usp + 1) = WINE_CODE_SELECTOR;
    psp->pspTerminateVector[0] = (unsigned short) KERNEL_Ordinal_1;
    psp->pspTerminateVector[1] = WINE_CODE_SELECTOR;
    psp->pspControlCVector[0] = (unsigned short) KERNEL_Ordinal_1;
    psp->pspControlCVector[1] = WINE_CODE_SELECTOR;
    psp->pspCritErrorVector[0] = (unsigned short) KERNEL_Ordinal_1;
    psp->pspCritErrorVector[1] = WINE_CODE_SELECTOR;
    psp->pspEnvironment = SELECTOROF( GetDOSEnvironment() );

    p1 = psp->pspCommandTail;
    for (i = 1; i < Argc; i++)
    {
	if ((int) ((int) p1 - (int) psp->pspCommandTail) + 
	    strlen(Argv[i]) > 124)
	    break;
	
	if (i != 1)
	    *p1++ = ' ';

	for (p2 = Argv[i]; *p2 != '\0'; )
	    *p1++ = *p2++;
	
    }
    *p1 = '\0';
    psp->pspCommandTailCount = strlen(psp->pspCommandTail);

    return SELECTOROF( WIN16_GlobalLock( handle ) );
}


/**********************************************************************
 *					CreateSelectors
 */
unsigned short *CreateSelectors(struct w_files * wpnt)
{
    int fd = wpnt->fd;
    struct ne_segment_table_entry_s *seg_table = wpnt->ne->seg_table;
    struct ne_header_s *ne_header = wpnt->ne->ne_header;
    unsigned short auto_data_sel;

    int old_length, file_image_length = 0;
    int saved_old_length = 0;
    int i, length;
    void *base_addr;
    WORD *selectors;
    ldt_entry entry;

    auto_data_sel=0;
    /*
     * Allocate memory for the table to keep track of all selectors.
     */
    selectors = (WORD *) malloc( ne_header->n_segment_tab * sizeof(WORD) );
    if (selectors == NULL)
	return NULL;

    /*
     * Step through the segment table in the exe header.
     */
    for (i = 0; i < ne_header->n_segment_tab; i++)
    {
	/*
	 * Is there an image for this segment in the file?
	 */
	if (seg_table[i].seg_data_offset == 0)
	{
	    /*
	     * No image in exe file, let's allocate some memory for it.
	     */
	    length = seg_table[i].min_alloc;
	}
	else
	{
	    /*
	     * Image in file, let's just point to the image in memory.
	     */
	    length            = seg_table[i].min_alloc;
	    file_image_length = seg_table[i].seg_data_length;
	    if (file_image_length == 0)	file_image_length = 0x10000;
	}

	if (length == 0) length = 0x10000;
	old_length = length;

	/*
	 * If this is the automatic data segment, its size must be adjusted.
	 * First we need to check for local heap.  Second we nee to see if
	 * this is also the stack segment.
	 */
	if (i + 1 == ne_header->auto_data_seg || i + 1 == ne_header->ss)
	{
	    length = 0x10000;
	    ne_header->sp = length - 2;
            dprintf_selectors(stddeb,"Auto data image length %x\n",file_image_length);
 	}

        if (!(selectors[i] = AllocSelector( 0 )))
        {
            fprintf( stderr, "CreateSelectors: out of free LDT entries\n" );
            exit( 1 );
        }
        if (!(base_addr = (void *) malloc( length )))
        {
            fprintf( stderr, "CreateSelectors: malloc() failed\n" );
            exit( 1 );
        }
        entry.base           = (unsigned long) base_addr;
        entry.limit          = length - 1;
        entry.seg_32bit      = 0;
        entry.limit_in_pages = 0;

	/*
	 * Is this a DATA or CODE segment?
	 */
        if (seg_table[i].seg_flags & NE_SEGFLAGS_DATA)
        {
            entry.type = SEGMENT_DATA;
            entry.read_only = seg_table[i].seg_flags & NE_SEGFLAGS_READONLY;
            memset( base_addr, 0, length );
        }
        else
        {
            entry.type = SEGMENT_CODE;
            entry.read_only = seg_table[i].seg_flags & NE_SEGFLAGS_EXECUTEONLY;
        }
        LDT_SetEntry( SELECTOR_TO_ENTRY(selectors[i]), &entry );

	if (seg_table[i].seg_data_offset != 0)
	{
	    /*
	     * Image in file.
	     */
	    lseek( fd, seg_table[i].seg_data_offset * 
                   (1 << ne_header->align_shift_count), SEEK_SET );
	    if(read(fd, base_addr, file_image_length) != file_image_length)
            {
                fprintf( stderr, "Unable to read segment %d from file\n", i+1);
                exit(1);
            }
	}

	/*
	 * If this is the automatic data segment, then we must initialize
	 * the local heap.
	 */
	if (i + 1 == ne_header->auto_data_seg)
	{
	    auto_data_sel = selectors[i];
	    saved_old_length = old_length;
	}
    }

    if(!auto_data_sel)dprintf_selectors(stddeb,"Warning: No auto_data_sel\n");
    for (i = 0; i < ne_header->n_segment_tab; i++)
    {
/*	Segments[s->selector >> __AHSHIFT].owner = auto_data_sel; */
	if (selectors[i] == auto_data_sel)
            LocalInit( auto_data_sel, saved_old_length,
			   0x10000 - 2 - saved_old_length 
			   - ne_header->stack_length );
#if 0
	    HEAP_LocalInit(auto_data_sel,
                           (char *)GET_SEL_BASE(selectors[i]) + saved_old_length, 
			   0x10000 - 2 - saved_old_length 
			   - ne_header->stack_length);
#endif
    }

    if(!EnvironmentHandle)
    {
        EnvironmentHandle = CreateEnvironment();
        PSPSelector = CreatePSP();
    }

    return selectors;
}


/**********************************************************************
 */
void
FixupFunctionPrologs(struct w_files * wpnt)
{
    struct ne_header_s *ne_header = wpnt->ne->ne_header;   
    union lookup entry_tab_pointer;
    struct entry_tab_header_s *eth;
    struct entry_tab_movable_s *etm;
    struct entry_tab_fixed_s *etf;
    unsigned char *fixup_ptr;
    int i;

    if (!(ne_header->format_flags & 0x0001))
	return;

    entry_tab_pointer.cpnt = wpnt->ne->lookup_table;
    /*
     * Let's walk through the table and fixup prologs as we go.
     */
    while (1)
    {
	/* Get bundle header */
	eth = entry_tab_pointer.eth++;

	/* Check for end of table */
	if (eth->n_entries == 0)
	    return;

	/* Check for empty bundle */
	if (eth->seg_number == 0)
	    continue;

	/* Examine each bundle */
	for (i = 0; i < eth->n_entries; i++)
	{
	    /* Moveable segment */
	    if (eth->seg_number >= 0xfe)
	    {
		etm = entry_tab_pointer.etm++;
		fixup_ptr = (char *)GET_SEL_BASE(wpnt->ne->selector_table[etm->seg_number-1]) + etm->offset;
	    }
	    else
	    {
		etf = entry_tab_pointer.etf++;
		fixup_ptr = (char *)GET_SEL_BASE(wpnt->ne->selector_table[eth->seg_number-1])
			     + (int) etf->offset[0] 
			     + ((int) etf->offset[1] << 8);

	    }

	    /* Verify the signature */
	    if (((fixup_ptr[0] == 0x1e && fixup_ptr[1] == 0x58)
		 || (fixup_ptr[0] == 0x8c && fixup_ptr[1] == 0xd8))
		&& fixup_ptr[2] == 0x90)
	    {
		fixup_ptr[0] = 0xb8;	/* MOV AX, */
		fixup_ptr[1] = wpnt->hinstance;
		fixup_ptr[2] = (wpnt->hinstance >> 8);
	    }
	}
    }
}

#endif /* ifndef WINELIB */
