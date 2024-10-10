/*
 * A hack to re-enable movies in the Linux NWN client. 
 * 
 * FWIW, this is copywritten by David Holland (7/2003) 
 *
 * Copyright 2003-2004, David Holland, zzqzzq_zzq@hotmail.com
 * Copyright 2006, David Holland, david.w.holland@gmail.com
 * Copyright 2008, David Holland, david.w.holland@gmail.com
 * 
 * There is no warrenty provided with this code. Use it at your
 * own risk.
 * 
 * There are two functions in the linux client that currently return 
 * '1' in $eax.  This modifies them to return a '0'.
 *
 * CClientExoApp::GetDisableMovies(void):
 * CClientExoApp::GetDisableIntroMovies(void):
 *
 * Changing the return value of those two functions, causes a third function
 * to be called when a movie is wished to be played.
 *
 * CClientExoApp::PlayMovie(CExoString, int)
 * 
 * A jump-point is installed inside the third function, when the jump-point
 * is triggered, the movie that was attempted to play is determined, and
 * passed as a argument to a subprocess that could theoretically play the
 * movie.
 * 
 * there are two other patches required to modify a code path, otherwise
 * the client will get confused (hang) while trying to play a chapter
 * intro movie.  (hence, patches #4, and #5)
 *
 * And make two modifications inside of:
 * CNWCMessage::HandleServerToPlayerUpdate_WorkRemaining(void):
 *
 * nop() out the call to: CClientExoApp::SetSendAreaLoadedMessageAfterMovie(void)
 *       	First call after the PlayMovie() call.
 * nop() out the 'jmp' that follows it.
 *
 * <shrug, that's what I had in my notes, its been to long to remember the details>
 * 
 * note: the NWN client is suspended while the movie player is playing.
 *
 * Becareful, as not all movie players will work.
 * mplayer player playing via Xv is known not to work. (And will crash
 * the X server. ) mplayer via X11 appears to be fine.
 */

#define _GNU_SOURCE
#include <dlfcn.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <SDL/SDL.h>
#include <SDL/SDL_types.h>
#include <SDL/SDL_syswm.h>
#include <link.h>
#include <libgen.h>

#include <errno.h>

#include <sys/mman.h>
#include <limits.h>

#include <stdarg.h>

#include "nwmovies.h"

#ifndef PAGESIZE
#define PAGESIZE 4096
#endif

/* I'm sure there are approximately 4billion better ways to do this 
   However, this is the one that I came up with, and most importantly
   works at least most of the time */

static int _NWMovies_Current_Grab_Mode = SDL_GRAB_OFF; 		// Presumably the grab is off at start.
static int _NWMovies_NeedsToggle = 0; 

/* External functions we link in. */
static int          (*sdl_wm_grabinput_ptr)(int) = NULL;
static SDL_Surface *(*sdl_getvideosurface_ptr)(void) = NULL;
static int          (*sdl_wm_togglefullscreen_ptr)(SDL_Surface *) = NULL; 
static int          (*sdl_pollevent_ptr)(SDL_Event *event) = NULL; 
static int          (*sdl_wm_iconifywindow_ptr)(void) = NULL;

unsigned long _NWM_movie_retaddr = 0x0;  /* modified by setup_memory() */

void NWMovies_Ungrab(void);
void NWMovies_RestoreGrab(void);
void NWMovies_setup_memory(unsigned int patch0, unsigned int patch1, unsigned int patch2, unsigned int patch3, unsigned int patch4, 
				unsigned int patch5, unsigned int patch6); 
void NWMovies_printdata(char *ptr, int len);
void NWMovies_memcpy(unsigned char *dest,  unsigned char *src, size_t n);
extern void NWMovies_playmovie(void); 
unsigned int *NWMovies_findcookie(char *file);
void NWMovies_runcommand(char *title);

void NWMovies_log(const int echo, const char *fmt, ...)
{
	static FILE *fp;
	va_list arg_list;

	if (!fp) 
		fp = fopen(_NWMOVIES_LOGFILE, "a");

	va_start(arg_list, fmt);
	if (fp) {
		vfprintf(fp, fmt, arg_list);
		if (echo)
			vfprintf(stderr, fmt, arg_list);
	} else
		vfprintf(stderr, fmt, arg_list);
	va_end(arg_list);
}

// Initialize constructor attribute so we get called during executable startup.
void NWMovies_Initialize(void) __attribute__((constructor));

void NWMovies_Initialize(void)
{
	struct 	stat 		statbuf; 
	Dl_info			info; 
	char			*library_name; 

	FILE			*fp; 
	char			string1[80];
	char			string2[80]; 

	unsigned int		patch0_addr, patch1_addr, patch2_addr, patch3_addr, patch4_addr; 
	unsigned int		patch5_addr, patch6_addr;

	unsigned int		file_size, file_date; 

	unsigned int		*patch_address;
	void			*self_handle; 
	void			*libSDL_handle; 	/* Either a pointer to libSDL, or RTLD_NEXT, depending on which one "works" */
	void			*self_ptr; 
	char			*self_name_ptr; 
	

	self_handle = dlopen("", RTLD_NOW | RTLD_GLOBAL); 


	self_ptr = dlsym(self_handle, "_init"); 
	if( self_ptr == NULL || dladdr( self_ptr, &info ) <= 0 ) { 
		NWMovies_log(1, "ERROR: NWMovies: dladdr(self: _init): %s\n", dlerror()); 
		abort(); 
	} 
	/* recycle library_name */
	self_name_ptr = basename((char *)info.dli_fname); 
	if( strncmp( self_name_ptr, "nwmain", PATH_MAX) != 0 ) { 
		dlclose(self_handle); 
		return; 
	} 
	dlclose(self_handle);

	/* Spit out a version number and reset log file */
	fprintf(stderr, "NOTICE: NWMovies: Version: %s (Binary = %s)\n", _NWMOVIES_VERSION, info.dli_fname); 
	if ((fp = fopen(_NWMOVIES_LOGFILE, "w"))) {
		fprintf(fp, "NOTICE: NWMovies: Version: %s (Binary = %s)\n", _NWMOVIES_VERSION, info.dli_fname);
		fclose(fp);
	}

	NWMovies_log(0, "NOTICE: NWMovies: Looking up symbols in libSDL.....\n"); 

/* try to lookup libSDL functions via RTLD_NEXT, and if that doesn't work, 
 * open libSDL directly.
 */
	libSDL_handle = RTLD_NEXT;

	sdl_wm_grabinput_ptr = dlsym(libSDL_handle, "SDL_WM_GrabInput"); 
	if( sdl_wm_grabinput_ptr == NULL ) { 
		/* via RTLD_NEXT failed, try loading libSDL directly. */
		libSDL_handle = dlopen("libSDL-1.2.so.0", RTLD_NOW | RTLD_GLOBAL);
		if ( libSDL_handle == NULL ) {
			NWMovies_log(1, "ERROR: NWMovies: dladdr(libSDL-1.2.so.0: _init): %s\n", dlerror()); 
			abort(); 
		}
		sdl_wm_grabinput_ptr = dlsym(libSDL_handle, "SDL_WM_GrabInput"); 
		if( sdl_wm_grabinput_ptr == NULL ) { 
			NWMovies_log(1, "ERROR: sdl_wm_grabinput_ptr == NULL: %s\n", dlerror()); 
			abort(); 
		}
		NWMovies_log(0, "NOTICE: NWMovies: Using libSDL via direct dlopen()\n"); 
	} else { 
		NWMovies_log(0, "NOTICE: NWMovies: Using libSDL via RTLD_NEXT.\n"); 
	}

	sdl_getvideosurface_ptr = dlsym(libSDL_handle, "SDL_GetVideoSurface"); 
	if( sdl_getvideosurface_ptr == NULL ) { NWMovies_log(1, "ERROR: sdl_getvideosurface_ptr == NULL: %s\n", dlerror()); abort(); }

	sdl_wm_togglefullscreen_ptr = dlsym(libSDL_handle, "SDL_WM_ToggleFullScreen"); 
	if( sdl_wm_togglefullscreen_ptr == NULL ) { NWMovies_log(1, "ERROR: sdl_wm_togglefullscreen_ptr == NULL: %s\n", dlerror()); abort(); }

	sdl_pollevent_ptr = dlsym(libSDL_handle, "SDL_PollEvent" );
	if( sdl_pollevent_ptr == NULL ) { NWMovies_log(1, "ERROR: sdl_pollevent_ptr == NULL: %s\n", dlerror()); abort(); }

	sdl_pollevent_ptr = dlsym(libSDL_handle, "SDL_PollEvent" );
	if( sdl_pollevent_ptr == NULL ) { NWMovies_log(1, "ERROR: sdl_pollevent_ptr == NULL: %s\n", dlerror()); abort(); }

	sdl_wm_iconifywindow_ptr = dlsym(libSDL_handle, "SDL_WM_IconifyWindow" );
	if( sdl_wm_iconifywindow_ptr == NULL ) { NWMovies_log(1, "ERROR: sdl_wm_iconifywindow_ptr == NULL: %s\n", dlerror()); abort(); }

	if( dladdr( sdl_wm_grabinput_ptr, &info ) <= 0 ) { 
		NWMovies_log(1, "ERROR: NWMovies: dladdr: %s\n", dlerror()); 
		abort(); 
	}

	library_name = (char *)info.dli_fname; 	

	NWMovies_log(0, "NOTICE: NWMovies: SDL Library determined to be: %s\n", library_name); 

	NWMovies_log(0, "NOTICE: NWMovies: SDL_WM_GrabInput() address: %08x\n", (unsigned int)sdl_wm_grabinput_ptr); 
	NWMovies_log(0, "NOTICE: NWMovies: SDL_GetVideoSurface() address: %08x\n", (unsigned int)sdl_getvideosurface_ptr); 
	NWMovies_log(0, "NOTICE: NWMovies: SDL_WM_ToggleFullScreen() address: %08x\n", (unsigned int)sdl_wm_togglefullscreen_ptr); 
	NWMovies_log(0, "NOTICE: NWMovies: SDL_PollEvent() address: %08x\n", (unsigned int)sdl_pollevent_ptr); 
	NWMovies_log(0, "NOTICE: NWMovies: SDL_WM_IconifyWindow() address: %08x\n", (unsigned int)sdl_wm_iconifywindow_ptr); 

	if( stat("nwmain", &statbuf) != 0 ) { 
		NWMovies_log(1, "ERROR: NWMovies: Unable to stat nwmain: %d\n", errno); 
		exit(-1); 
	}

	/* ini parsing.  No, this doesn't have a lot of error checking. */

	fp = fopen("nwmovies.ini", "r"); 
	if( fp == NULL ) { 
		NWMovies_log(1, "WARNING: NWMovies: No INI file.  Creating.\n"); 
		fp = fopen("nwmovies.ini", "w"); 
		if( fp == NULL ) { 
			NWMovies_log(1, "ERROR: NWMovies: Unable to create INI file.  Aborting: %d\n", errno); 
			exit(-1); 
		}
		fprintf(fp, "size 0\n"); 
		fprintf(fp, "time 0\n"); 
		fprintf(fp, "patch0 0\n"); 
		fprintf(fp, "patch1 0\n"); 
		fprintf(fp, "patch2 0\n"); 
		fprintf(fp, "patch3 0\n"); 
		fprintf(fp, "patch4 0\n"); 
		fprintf(fp, "patch5 0\n"); 
		fprintf(fp, "patch6 0\n"); 
		fprintf(fp, "NeedsToggle 0\n"); 
		fclose(fp); 
		fp = fopen("nwmovies.ini", "r"); 
		if( fp == NULL ) { 
			NWMovies_log(1, "ERROR: NWMovies: Unable to re-open nwmovies.ini. Aborting: %d\n", errno); 
			exit(-1); 
		}
	}
	while( fscanf(fp, "%79s %79s\n", string1, string2) != EOF ) { 
		if( strcmp(string1, "size") == 0 ) { 
			file_size = atoi(string2); 
		}
		if( strcmp(string1, "time") == 0 ) { 
			file_date = atoi(string2); 
		} 
		if( strcmp(string1, "patch0") == 0 ) { 
			patch0_addr = strtol(string2, NULL, 0); 
		} 
		if( strcmp(string1, "patch1") == 0 ) { 
			patch1_addr = strtol(string2, NULL, 0); 
		} 
		if( strcmp(string1, "patch2") == 0 ) { 
			patch2_addr = strtol(string2, NULL, 0); 
		} 
		if( strcmp(string1, "patch3") == 0 ) { 
			patch3_addr = strtol(string2, NULL, 0); 
		} 
		if( strcmp(string1, "patch4") == 0 ) { 
			patch4_addr = strtol(string2, NULL, 0); 
		} 
		if( strcmp(string1, "patch5") == 0 ) { 
			patch5_addr = strtol(string2, NULL, 0); 
		} 
		if( strcmp(string1, "patch6") == 0 ) { 
			patch6_addr = strtol(string2, NULL, 0); 
		} 
		if( strcmp(string1, "NeedsToggle") == 0 ) { 
			_NWMovies_NeedsToggle = atoi(string2); 
		} 
	}
	fclose(fp); 
	
	if( 	statbuf.st_size != file_size || statbuf.st_mtime != file_date ) {

		NWMovies_log(1, "WARNING: NWMovies: INI recalculation required: %d:%d %d:%d\n", 
			(unsigned int)statbuf.st_size, file_size, (unsigned int)statbuf.st_mtime, file_date); 

		patch_address = NWMovies_findcookie( "nwmain" ); 
		
		fp = fopen("nwmovies.ini", "w"); 
		if( fp == NULL ) { 
			NWMovies_log(1, "ERROR: NWMovies: Unable to create INI file.  Aborting: %d\n", errno); 
			exit(-1); 
		}
		fprintf(fp, "%s %d\n", "size", (unsigned int)statbuf.st_size); 
		fprintf(fp, "%s %d\n", "time", (unsigned int)statbuf.st_mtime); 
		fprintf(fp, "%s 0x%08x\n", "patch0", patch_address[0]); 
		fprintf(fp, "%s 0x%08x\n", "patch1", patch_address[1]); 
		fprintf(fp, "%s 0x%08x\n", "patch2", patch_address[2]); 
		fprintf(fp, "%s 0x%08x\n", "patch3", patch_address[3]); 
		fprintf(fp, "%s 0x%08x\n", "patch4", patch_address[4]); 
		fprintf(fp, "%s 0x%08x\n", "patch5", patch_address[5]); 
		fprintf(fp, "%s 0x%08x\n", "patch6", patch_address[6]); 
		fprintf(fp, "%s 0\n", "NeedsToggle" ); 
		fclose(fp); 
		NWMovies_log(1, "NOTICE: NWMovies: INI File written: Now exiting.  This is perfectly normal!\n"); 
		NWMovies_log(1, "NOTICE: NWMovies: Your next run of NWN should be complete, and include movies.\n"); 
		exit(0); 
	}

	NWMovies_log(0, "NOTICE: NWMovies: Patch 0 Address: 0x%08x\n", patch0_addr); 
	NWMovies_log(0, "NOTICE: NWMovies: Patch 1 Address: 0x%08x\n", patch1_addr); 
	NWMovies_log(0, "NOTICE: NWMovies: Patch 2 Address: 0x%08x\n", patch2_addr); 
	NWMovies_log(0, "NOTICE: NWMovies: Patch 3 Address: 0x%08x\n", patch3_addr); 
	NWMovies_log(0, "NOTICE: NWMovies: Patch 4 Address: 0x%08x\n", patch4_addr); 
	NWMovies_log(0, "NOTICE: NWMovies: Patch 5 Address: 0x%08x\n", patch5_addr); 
	NWMovies_log(0, "NOTICE: NWMovies: Patch 6 Address: 0x%08x\n", patch6_addr); 

	NWMovies_setup_memory(patch0_addr, patch1_addr, patch2_addr, patch3_addr, patch4_addr, patch5_addr, patch6_addr); 

	NWMovies_log(0, "NOTICE: NWMovies: Initialized.\n");

	return;
}

SDL_GrabMode SDL_WM_GrabInput(SDL_GrabMode mode) { 

	/* Get/Save the current grab mode. */
	
	NWMovies_log(0, "NOTICE: NWMovies: SDL_WM_GrabInput("); 
	switch( mode) { 
		case SDL_GRAB_QUERY:
			NWMovies_log(0, "QUERY) called..\n"); 
			_NWMovies_Current_Grab_Mode = sdl_wm_grabinput_ptr( mode ); 
			break; 
		case SDL_GRAB_OFF:
			NWMovies_log(0, "OFF) called..\n"); 
			_NWMovies_Current_Grab_Mode = SDL_GRAB_OFF;
			break; 
		case SDL_GRAB_ON:
			NWMovies_log(0, "ON) called..\n"); 
			_NWMovies_Current_Grab_Mode = SDL_GRAB_ON;
			break; 
		default: 
			NWMovies_log(0, "UNKNOWN) called..\n"); 
			break; 
		
	}
	return(sdl_wm_grabinput_ptr( mode )); 
}

/* 
 * Wrap around SDL_PollEvent() so we can handle the various unlock keys 
 *
 * Right Alt-Enter: Toggle Fullscreen
 * Left  Alt-Enter: Toggle Fullscreen w/ Iconfiy  (ie: Maximize Window - Odd No?)
 * Left  Control-G: Turn off Keyboard/Mouse Grab
 * Right Control-G: Turn ON  Keyboard/Mouse Grab
 */

int SDL_PollEvent(SDL_Event *event) { 

SDL_Event my_event; 
int	  eat_event; 

while( sdl_pollevent_ptr(&my_event) ) { 
  eat_event = 0; 

  /* Key down, is this one of the events we're interested in? */
  if( my_event.type == SDL_KEYDOWN ) {
    if( my_event.key.keysym.sym == SDLK_RETURN && ( my_event.key.keysym.mod & KMOD_RALT ) ) { 
     sdl_wm_togglefullscreen_ptr(sdl_getvideosurface_ptr());
     eat_event = 1; 
    }
    if( my_event.key.keysym.sym == SDLK_RETURN && ( my_event.key.keysym.mod & KMOD_LALT ) ) { 
     sdl_wm_togglefullscreen_ptr(sdl_getvideosurface_ptr());
     sdl_wm_iconifywindow_ptr();
     eat_event = 1; 
    }
    if( my_event.key.keysym.sym == SDLK_g      && ( my_event.key.keysym.mod & KMOD_LCTRL )) { 
     SDL_WM_GrabInput(SDL_GRAB_OFF);
     eat_event = 1; 
    }
    if( my_event.key.keysym.sym == SDLK_g      && ( my_event.key.keysym.mod & KMOD_RCTRL )) { 
     SDL_WM_GrabInput(SDL_GRAB_ON);
     eat_event = 1; 
    }
  }
  if( !eat_event ) { 
    /* Didn't eat event, send it on up. */
    memcpy( event, &my_event, sizeof(SDL_Event)); 
    return(1); 
  }
} /* real SDL_PollEvent() returned 0 */

/* Copy it anyways to maintain behavior */
memcpy( event, &my_event, sizeof(SDL_Event)); 
return(0); 

}

void NWMovies_Ungrab(void)
{
	sdl_wm_grabinput_ptr(SDL_GRAB_OFF); 
}

void NWMovies_RestoreGrab(void)
{ 
	SDL_Surface	*cur_surf = NULL; 

	if( _NWMovies_NeedsToggle ) { 
		cur_surf = sdl_getvideosurface_ptr();
		if( (cur_surf != NULL) && (cur_surf->flags & SDL_FULLSCREEN)) { 
			sdl_wm_togglefullscreen_ptr(cur_surf); 
			sdl_wm_togglefullscreen_ptr(cur_surf); 
		}
	}

	if( _NWMovies_Current_Grab_Mode != SDL_GRAB_QUERY ) { 
		sdl_wm_grabinput_ptr( _NWMovies_Current_Grab_Mode );
	}
}

void NWMovies_setup_memory(unsigned int patch0, unsigned int patch1, unsigned int patch2, unsigned int patch3, unsigned int patch4, unsigned int patch5, unsigned int patch6)
{
	unsigned char	instruction[5]; 			/* 5 byte instruction */
	long	address_offset; 
	int	i,j; 

	unsigned char    patch0_code[]   = "\xb8\x00\x00\x00\x00\x90\x5d\xc3"; /* These must be a multiple of 4 bytes */
	unsigned char    patch1_code[]   = "\xb8\x00\x00\x00\x00\x90\x5d\xc3";
	unsigned char    patch2_code[]   = "\x90\x90\x90\x90\x90\x83\xec\x08";

/* 168 & earlier */
	unsigned char   patch3_code1[]   = "\x90\x90\x83\xec";
/* 169b2 */
	unsigned char   patch3_code2[]   = "\x90\x90\x90\x83";

	unsigned char	patch3_buf[4]; 
	int		patch3_flag = 0; 	/* Change to 1, for 1.69 */

	memcpy( (void *)patch3_buf, (void *)patch3, 4); 
	if( patch3_buf[2] == 144 ) { patch3_flag = 1; } 


	NWMovies_log(0, "NOTICE: NWMovies: PrePatch0: ");
	NWMovies_printdata((void *)patch0, sizeof(patch0_code)-1); 
	NWMovies_log(0, "\nNOTICE: NWMovies: PrePatch1: "); 
	NWMovies_printdata((void *)patch1, sizeof(patch1_code)-1); 
	NWMovies_log(0, "\nNOTICE: NWMovies: PrePatch2: "); 
	NWMovies_printdata((void *)patch2, sizeof(patch2_code)-1); 
	NWMovies_log(0, "\n"); 
	if( patch3_flag ) { 
		NWMovies_log(0, "NOTICE: NWMovies: PrePatch3: 169+: "); 
		NWMovies_printdata((void *)patch3, sizeof(patch3_code2)-1); 
	} else { 
		NWMovies_log(0, "NOTICE: NWMovies: PrePatch3: 168-: "); 
		NWMovies_printdata((void *)patch3, sizeof(patch3_code1)-1); 
	}
	NWMovies_log(0, "\n"); 

	NWMovies_memcpy((void *)patch0, patch0_code, sizeof(patch0_code)-1); 
	NWMovies_memcpy((void *)patch1, patch1_code, sizeof(patch1_code)-1); 
	NWMovies_memcpy((void *)patch2, patch2_code, sizeof(patch2_code)-1); 
	if( patch3_flag ) { 
		NWMovies_memcpy((void *)patch3, patch3_code2, sizeof(patch3_code2)-1); 
	} else { 
		NWMovies_memcpy((void *)patch3, patch3_code1, sizeof(patch3_code1)-1); 
	}

	NWMovies_log(0, "NOTICE: NWMovies: PostPatch0: "); 
	NWMovies_printdata((void *)patch0, sizeof(patch0_code)-1); 
	NWMovies_log(0, "\nNOTICE: NWMovies: PostPatch1: "); 
	NWMovies_printdata((void *)patch1, sizeof(patch1_code)-1); 
	NWMovies_log(0, "\nNOTICE: NWMovies: PostPatch2: "); 
	NWMovies_printdata((void *)patch2, sizeof(patch2_code)-1); 
	NWMovies_log(0, "\n"); 
	if( patch3_flag ) { 
		NWMovies_log(0, "NOTICE: NWMovies: PostPatch3: 169+: "); 
		NWMovies_printdata((void *)patch3, sizeof(patch3_code2)-1); 
	} else { 
		NWMovies_log(0, "NOTICE: NWMovies: PostPatch3: 168-: "); 
		NWMovies_printdata((void *)patch3, sizeof(patch3_code1)-1); 
	}
	NWMovies_log(0, "\n"); 

	NWMovies_log(0, "NOTICE: NWMovies: PrePatch4: "); 
	NWMovies_printdata((void *)patch4, 5); 
	NWMovies_log(0, "\n"); 

	address_offset = (unsigned long) &NWMovies_playmovie;
	address_offset = address_offset - (unsigned long) patch4 - 5; /* How many bytes should the jump be */
	memcpy(instruction + 1, &address_offset, 4); 
	instruction[0] = '\xe9'; 
	NWMovies_memcpy((void *)patch4, instruction, 5); 			/* Put the jump in */
	_NWM_movie_retaddr = (unsigned long)patch4 + 0x5; 			/* setup return address */

	NWMovies_log(0, "NOTICE: NWMovies: PostPatch4: "); 
	NWMovies_printdata((void *)patch4, 5); 
	NWMovies_log(0, "\n"); 

	if( patch5 != 0 && patch5 != 1 && patch6 != 0 && patch6 != 1 ) { 
		NWMovies_log(0, "NOTICE: NWMovies: MoviesPrePatch: "); 
		NWMovies_printdata((void *)patch5, patch6 - patch5); 
		NWMovies_log(0, "\n"); 

		for( i = patch5; i < patch6; i=i+4 ) { 
			memcpy( instruction, (void *)i, 4); 
			j = 0; 
			while( j + i < patch6 && j < 4 ) { 
				instruction[j] = 0x90; 
				j++;
			} 
			NWMovies_memcpy( (void *) i, instruction, 4 );
		}
		NWMovies_log(0, "NOTICE: NWMovies: MoviesPostPatch: "); 
		NWMovies_printdata((void *)patch5, patch6 - patch5); 
		NWMovies_log(0, "\n"); 
	} else { 
		NWMovies_log(0, "NOTICE: NWMovies: Movie Button already enabled. Skipping movie patch.\n"); 
	}
}

void NWMovies_printdata(char *ptr, int len)
{
	int i; 

	for(i=0; i<len; i++) { 
		NWMovies_log(0, "%02x ", (unsigned char) ptr[i]); 
	}
	return; 
}

void NWMovies_memcpy(unsigned char *dest, unsigned char *src, size_t n) 
{
	/* int i;  */
	unsigned char *p = dest; 

	/* Align to a multiple of PAGESIZE, assumed to be a power of two */
	/* Do two pages, just to make certain we get a big enough chunk */
	p = (unsigned char *)(((int) p + PAGESIZE-1) & ~(PAGESIZE-1));
	if( mprotect(p-PAGESIZE, 2 * PAGESIZE, PROT_READ|PROT_WRITE|PROT_EXEC) != 0 ) { 
		NWMovies_log(1, "ERROR: NWMovies: Could not de-mprotect(%p)\n", p); 
		exit(-1); 
	}

	memcpy(dest, src, n);
	/* restore memory protection */
	if( mprotect(p-PAGESIZE, 2 * PAGESIZE, PROT_READ|PROT_EXEC) != 0 ) { 
		NWMovies_log(1, "ERROR: NWMovies: Could not re-mprotect(%p)\n", p); 
		exit(-1); 
	}
}

unsigned long	_NWMovies_ESI;

void NWMovies_playmovie2(void)
{	
	char 	*title; 

	SDL_Surface	*current_surface = NULL; 
	Uint32		current_flags; 

	memcpy(&title, (void *)_NWMovies_ESI, 4);
	if (!title) {
		NWMovies_log(1, "ERROR: NWMovies: Movie title was NULL");
		return;
	}

/* try to release the Xserver grab, then play, then grab again */

// We have to hack up the current video surface here, since
// we need to lie (alot) to SDL since once we're full screen
// it doesn't allow us to release the grab.  (Stupid SDL) 
// 
// This was previously why we had to rebuild libSDL to enable
// the visibility of the SDL_WM_GrabInputRaw() function. 
// Now we could be cheating. 

// There's three ways to do this that I can see. 
// One: Make SDL_WM_GrabInputRaw() visible, and call it directly.
//      This requires custom libSDL's to be built, and that's "hard". 
//
// Two: The offically supported method, call SDL_WM_ToggleFullScreen().
//      This works, but causes quite a bit of screen flickering, at movie start, and end
//      As the main nwn client screen toggles fullscreen
//
// Three: Twiddle the SDL_FULLSCREEN bit off the 'current_surface->flags' bits ourselves.
//        This is a hack, but looks much nicer. 
//
// I'm going w/ the safe option #2, as the default.
// But the recommendation is to use Option #3 via (NWMOVIES_GRAB_HACK) for visual reasons. 

	current_surface = sdl_getvideosurface_ptr(); 
	if(current_surface == NULL) { 
		NWMovies_log(1, "ERROR: NWMovies: PlayMovie2: Unable to get current SDL Surface for Internal Grab release. Giving up!\n"); 
		abort(); 
	}

	// Save the "real" surface flags in "current_flags"
	current_flags = current_surface->flags; 

	if( getenv( "NWMOVIES_GRAB_HACK" ) != NULL ) { 
		// Option #3 (hack alert!!)
 		current_surface->flags &= ~SDL_FULLSCREEN; 
	} else { 
		// Option #2
		// If we're full screen, toggle the fullscreen bits. 
		if( current_flags & SDL_FULLSCREEN ) { 
			sdl_wm_togglefullscreen_ptr(current_surface);
		}
	}

	NWMovies_log(0, "NOTICE: NWMovies: Attempting to play movie \"%s\"\n", title); 

	NWMovies_Ungrab();
	NWMovies_runcommand(title);

// And finally restore the fullscreen mode, and any grab settings. 
	if( getenv( "NWMOVIES_GRAB_HACK" ) != NULL ) { 
		// Option #3 (hack alert!!)
		current_surface->flags = current_flags;
	} else { 
		//if the "real" surface flags" were full screen, we gotta toggle back.
		if( current_flags & SDL_FULLSCREEN ) { 
			sdl_wm_togglefullscreen_ptr(current_surface);
		}
	}

	NWMovies_RestoreGrab();

	return; 
}
