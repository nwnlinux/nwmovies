/* 
 * SDL Wrapper Library.
 * Causes BinkPlayer to do all the right things so it 
 * can be used for "Native" movie play back /w NWN. 
 * 
 * Copyright (C) David Holland, March 2004
 * Copyright (C) David Holland, May 2008
 * Portions Copyright(C) lje/Jens 
 *
 * Includes suggestions and code by Skildron, and Eyrdan
 * 
 * No warrenties implied. 
 * Do what you want /w it so long as credit is given.
 * And you don't claim you wrote it yourself. 
 *
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <limits.h>
#include <dlfcn.h>
#include <pwd.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <SDL/SDL.h>
#include <SDL/SDL_syswm.h>
#include <time.h>
#include <sys/time.h>
#include <linux/rtc.h>
#include <sys/ioctl.h>
#include <assert.h>

#include "nwmovies.h"

#ifdef CRASH

#include <signal.h>
#include <execinfo.h>

#endif

// Overrides
void (*__sdl_updaterects)(SDL_Surface *screen, int numrects, SDL_Rect *rects) = NULL; 
SDL_Surface *(*__sdl_setvideomode)(int width, int height, int bpp, Uint32 flags) = NULL; 
int (*__sdl_nanosleep)(const struct timespec *req, struct timespec *rem) = NULL;
int (*__sdl_gettimeofday)(struct timeval *tv, struct timezone *tz) = NULL; 

// Lookups - These must be available.
static SDL_Surface *(*sdl_creatergbsurface_ptr)( Uint32 flags, int width, int height, int bitsPerPixel, 
                                  Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask) = NULL;
static void *(*sdl_freesurface_ptr)(SDL_Surface *surface) = NULL;
static char *(*sdl_geterror_ptr)(void) = NULL;
static int   (*sdl_getwminfo_ptr)(SDL_SysWMinfo *info) = NULL; 
static int   (*sdl_upperblit_ptr)(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect) = NULL; 
static int   (*xraisewindow_ptr)(Display *display, Window w) = NULL; 
static int   (*sdl_showcursor_ptr)(int toggle) = NULL; 

/* Optionally available */
SDL_Surface *(*__sdl_zoomSurface)(SDL_Surface * src, double zoomx, double zoomy, int smooth) = NULL; 

int __sdl_binkwidth = -1; 
int __sdl_binkheight = -1; 
int __sdl_fullscreen = 0; 
int __sdl_smooth = 0; 
int __sdl_scale = 0;
int __sdl_debug = 0; 

char	*__sdl_noperf = NULL;		/* Disable performance improvements */
int	__sdl_window_raised = 0; 	/* Try to raise the BinkPlayer Window to top when playing full screen. */

/* Needed due to this library may be preloaded for other exe's where we 
 * don't want to have nanosleep()/gettimeofday() overridden
 */ 
int __sdl_enabled = 0; 

float __sdl_scalex = 1.0; 
float __sdl_scaley = 1.0; 

SDL_Surface	*__sdl_screen_surface = NULL; 
SDL_Surface	*__sdl_fake_surface; 

#ifdef CRASH

void binklib_sighandler(int sig) {
	int	fd = -1 ;
	char	msg[1024];
	void	*array[1024];	/* large stack dump */
	size_t	size;
	pid_t	my_pid;
	pid_t	pid;

/* Write rudamentary crash log */
	fd = open("/tmp/binklib_crash.log", O_CREAT | O_WRONLY, 0644 );

	if( fd < 0 ) {
		fd = fileno(stderr);
	}
	sprintf(msg, "Aieeeeeeee, BinkLib Crashed: %d\n", sig);
	write(fd, msg, strlen(msg));

	size = backtrace(array, 1024);
	backtrace_symbols_fd(array, size, fd);

	close(fd);

/* Try for a more complex crash log */

	fd = open("/tmp/binklib_crash.cmd", O_CREAT | O_WRONLY, 0644 );
	if( fd < 0 ) {
		exit(-1);
	}
	sprintf(msg, "where\n");
	write(fd, msg, strlen(msg));

	sprintf(msg, "quit\n");
	write(fd, msg, strlen(msg));

	close(fd);

	putenv("LD_PRELOAD");	/* Unset the preload variable */
	my_pid = getpid();

	switch( pid = fork() ) {
	case -1:
		/* fork failed */
		exit(-1);
	case 0:
		/* child */

		sprintf(msg, "gdb /proc/%d/exe %d < /tmp/binklib_crash.cmd > /tmp/binklib_crash2.log 2>&1", my_pid, my_pid);
		system(msg);
		_exit(0);
	default:
		/* parent */
		sleep(10);
	}

	exit(-1);
}

#endif

void __sdl_initialize(void) __attribute__((constructor));
 
void __sdl_initialize(void) {
	char	*env_string; 
	struct	timeval	tv; 

	void	*self_handle;
	void	*self_ptr;
	char	*self_name_ptr;
	Dl_info	info;
	void	*dlhandle; 
	void	*x11_handle; 			/* XRaiseWindow doesn't seem to get imported automagically */

#ifdef CRASH
        signal(SIGBUS, binklib_sighandler);
        signal(SIGSEGV, binklib_sighandler);
#endif
	/* Make certain we only enable ourselves only for BinkPlayer */

        self_handle = dlopen("", RTLD_NOW | RTLD_GLOBAL);
        self_ptr = dlsym(self_handle, "_init");
        if( self_ptr == NULL || dladdr( self_ptr, &info ) <= 0 ) {
                fprintf(stderr, "ERROR: BinkLib: dladdr(self: _init): %s\n", dlerror());
                abort();
        }
        /* recycle library_name */
        self_name_ptr = basename((char *)info.dli_fname);
        if( strncmp( self_name_ptr, "BinkPlayer", PATH_MAX) != 0 ) {
                dlclose(self_handle);
                return;
        }
        dlclose(self_handle);
	__sdl_enabled = 1; 		/* Enable ourselves. */

	fprintf(stderr, "NOTICE: Loading BinkLib (%s)\n", _NWMOVIES_VERSION); 
	if( getenv( "LD_PRELOAD" ) ) { 
		fprintf(stderr, "NOTICE: Current LD_PRELOAD=%s\n", getenv("LD_PRELOAD")); 
	} else { 
		fprintf(stderr, "NOTICE: Current LD_PRELOAD=(NONE) - How did we get loaded btw?\n"); 
	}

	/* Lookup things in libSDL */

sdl_creatergbsurface_ptr = dlsym(RTLD_NEXT, "SDL_CreateRGBSurface");
if( sdl_creatergbsurface_ptr == NULL ) { fprintf(stderr, "ERROR: sdl_creatergbsurface_ptr == NULL: %s\n", dlerror()); abort(); }

sdl_freesurface_ptr = dlsym(RTLD_NEXT, "SDL_FreeSurface");
if( sdl_freesurface_ptr == NULL ) { fprintf(stderr, "ERROR: sdl_freesurface_ptr == NULL: %s\n", dlerror()); abort(); }

sdl_geterror_ptr = dlsym(RTLD_NEXT, "SDL_GetError");
if( sdl_geterror_ptr == NULL ) { fprintf(stderr, "ERROR: sdl_geterror_ptr == NULL: %s\n", dlerror()); abort(); }

sdl_getwminfo_ptr = dlsym(RTLD_NEXT, "SDL_GetWMInfo");
if( sdl_getwminfo_ptr == NULL ) { fprintf(stderr, "ERROR: sdl_getwminfo_ptr == NULL: %s\n", dlerror()); abort(); }

sdl_upperblit_ptr = dlsym(RTLD_NEXT, "SDL_UpperBlit");
if( sdl_upperblit_ptr == NULL ) { fprintf(stderr, "ERROR: sdl_upperblit_ptr == NULL: %s\n", dlerror()); abort(); }

sdl_showcursor_ptr = dlsym(RTLD_NEXT, "SDL_ShowCursor");
if( sdl_showcursor_ptr == NULL ) { fprintf(stderr, "ERROR: sdl_showcursor_ptr == NULL: %s\n", dlerror()); abort(); }

x11_handle = dlopen("libX11.so", RTLD_NOW | RTLD_GLOBAL); 
if( !x11_handle ) { 
	printf("ERROR: Unable to dlopen(libX11.so): %s\n", dlerror()); 
	abort(); 
}

xraisewindow_ptr = dlsym(x11_handle, "XRaiseWindow");
if( xraisewindow_ptr == NULL ) { fprintf(stderr, "ERROR: xraisewindow_ptr == NULL: %s\n", dlerror()); abort(); }


/********************************************************************************/

	/* Lets try looking up the Zoom functions */
	dlhandle = dlopen("libSDL_gfx.so", RTLD_NOW);
	if( ! dlhandle ) { 
		dlhandle = dlopen("./nwmovies/libSDL_gfx.so", RTLD_NOW); 
		if( !dlhandle ) { 
			/* Do this so the dlerror() call returns something intelligible */
			dlhandle = dlopen("libSDL_gfx.so", RTLD_NOW);
		}
	} 

	if( !dlhandle ) { 
		fprintf(stderr, "NOTICE: ZoomSurface functions unavailable. The error was: %s\n", dlerror()); 
	} else { 
		__sdl_zoomSurface = dlsym(dlhandle, "zoomSurface"); 
		if( __sdl_zoomSurface == NULL ) { 
			fprintf(stderr, "NOTICE: ZoomSurface functions unavailable. The error was: %s\n", dlerror()); 
		} else { 
			fprintf(stderr, "NOTICE: ZoomSurface functions available.\n"); 
		}
	}
	
	/* Lookup the thing(s) we provide replacements for */
	__sdl_updaterects = dlsym(RTLD_NEXT, "SDL_UpdateRects");
if( __sdl_updaterects == NULL ) { fprintf(stderr, "ERROR: __sdl_updaterects == NULL: %s\n", dlerror()); abort(); }

	__sdl_setvideomode = dlsym(RTLD_NEXT, "SDL_SetVideoMode"); 
if( __sdl_setvideomode == NULL ) { fprintf(stderr, "ERROR: __sdl_setvideomode == NULL: %s\n", dlerror()); abort(); }

	__sdl_nanosleep = dlsym(RTLD_NEXT, "nanosleep"); 
if( __sdl_nanosleep == NULL ) { fprintf(stderr, "ERROR: __sdl_nanosleep == NULL: %s\n", dlerror()); abort(); }

	__sdl_gettimeofday = dlsym(RTLD_NEXT, "gettimeofday"); 
if( __sdl_gettimeofday == NULL ) { fprintf(stderr, "ERROR: __sdl_gettimeofday == NULL: %s\n", dlerror()); abort(); }

	__sdl_noperf = getenv("BINK_NOPERF"); 

	env_string = getenv("BINK_WIDTH"); 
	if( env_string != NULL ) {
		__sdl_binkwidth = atoi(env_string); 
		if( __sdl_binkwidth <= 0 ) { __sdl_binkwidth = -1; } 
	} 

	env_string = getenv("BINK_HEIGHT"); 
	if( env_string != NULL ) { 
		__sdl_binkheight = atoi(env_string); 
		if( __sdl_binkheight <= 0 ) { __sdl_binkheight = -1; } 
	} 

	env_string = getenv("BINK_FULLSCREEN"); 
	if( env_string != NULL ) {
		__sdl_fullscreen = atoi(env_string); 
	} 

	env_string = getenv("BINK_SMOOTH"); 
	if( env_string != NULL ) {
		__sdl_smooth = atoi(env_string); 
	} 

	env_string = getenv("BINK_SCALE"); 
	if( env_string != NULL ) {
		__sdl_scale = atoi(env_string); 
	} 

	__sdl_gettimeofday( &tv, NULL); 
	fprintf(stderr, "%ld.%06ld: BinkLib: Initialized\n", tv.tv_sec, tv.tv_usec); 
}

void SDL_UpdateRects(SDL_Surface *fake_screen, int numrects, SDL_Rect *rects) {
	int i; 
	SDL_Surface 	*zoom_picture; 
	SDL_Rect	dest; 
	SDL_SysWMinfo	wminfo; 

	struct	timeval	tv; 

	if( !__sdl_enabled || __sdl_screen_surface == NULL ) {
		__sdl_updaterects( fake_screen, numrects, rects); 
		return; 
	} 

/* Hopefully we have a window by this point, and can lookup the Window ID, and
 * use XRaiseWindow() to bring the window to the foreground
 */

	if( ! __sdl_window_raised ) { 
		__sdl_window_raised = 1; 

		if( __sdl_fullscreen ) { 
			SDL_VERSION(&wminfo.version); 
			if( sdl_getwminfo_ptr(&wminfo)) { /* We have data */
				assert( wminfo.subsystem == SDL_SYSWM_X11 );    // We're not running on X11? 

				wminfo.info.x11.lock_func();
				xraisewindow_ptr( wminfo.info.x11.display, wminfo.info.x11.window ); 
				wminfo.info.x11.unlock_func();

			} else { 
				fprintf(stderr, "WARNING: Failed to get WM Info\n"); 
			}
		}
	}

	if( __sdl_debug ) { 

		for(i=0; i<numrects; i++) { 

			__sdl_gettimeofday( &tv, NULL); 
			fprintf(stderr, "%ld.%06ld: BinkLib: SDL_UpdateRects: %02d Screen: %p - X: %03d Y: %03d / W: %03d H: %03d\n", 
				tv.tv_sec, tv.tv_usec, i,
				fake_screen, 
				rects[i].x, rects[i].y, 
				rects[i].w, rects[i].h ); 

		}
	}

	if( numrects == 1 && rects[0].x == 208 && rects[0].y == 114 && rects[0].w == 224 && rects[0].h == 252 ) { 
		exit(0); 
	} 
	if( numrects == 1 && rects[0].x == 288 && rects[0].y == 174 && rects[0].w == 224 && rects[0].h == 252 ) {
		exit(0); 
	} 

	if( __sdl_debug ) { 
		fprintf(stderr, "\n\n"); 
	} 

	if( __sdl_scale && __sdl_zoomSurface != NULL ) { 
		zoom_picture = __sdl_zoomSurface (fake_screen, __sdl_scalex, __sdl_scaley, __sdl_smooth);
		if( zoom_picture != NULL ) { 
			dest.x = (__sdl_screen_surface->w - zoom_picture->w) / 2;
			dest.y = (__sdl_screen_surface->h - zoom_picture->h) / 2;
			dest.w = zoom_picture->w;
			dest.h = zoom_picture->h;
			/* Ordinarily SDL_BlitSurface, but there's a redef that renames it to SDL_UpperBlit */
			if (sdl_upperblit_ptr (zoom_picture, NULL, __sdl_screen_surface, &dest) < 0) {
				fprintf (stderr, "Blit failed: %s\n", sdl_geterror_ptr ());
			} 
			sdl_freesurface_ptr (zoom_picture);
		}
	} else { 
		dest.x = (__sdl_screen_surface->w - fake_screen->w) / 2;
		dest.y = (__sdl_screen_surface->h - fake_screen->h) / 2;
		dest.w = fake_screen->w;
		dest.h = fake_screen->h;
		/* Ordinarily SDL_BlitSurface, but there's a redef that renames it to SDL_UpperBlit */
		if (sdl_upperblit_ptr (fake_screen, NULL, __sdl_screen_surface, &dest) < 0) {
			fprintf (stderr, "Blit failed: %s\n", sdl_geterror_ptr ());
		} 
	}

	dest.x = 0; 
	dest.y = 0; 
	dest.w = __sdl_screen_surface->w; 
	dest.h = __sdl_screen_surface->h; 

	__sdl_updaterects(__sdl_screen_surface, 1, &dest); 
	return; 
}

SDL_Surface *SDL_SetVideoMode(int width, int height, int bpp, Uint32 flags) {
	Uint32		my_flags; 

	if( !__sdl_enabled ) { 
		return( __sdl_setvideomode(width, height, bpp, flags) ); 
	}

	my_flags = flags; 
	
	if( (__sdl_binkwidth > 0 && __sdl_binkheight > 0) && (__sdl_binkwidth < width || __sdl_binkheight < height) ) { 
		__sdl_scale = 1; 
	}

	if( __sdl_binkwidth > 0 ) { 
		__sdl_scalex = (float)__sdl_binkwidth / width; 
	} else { 
		__sdl_binkwidth = width; 
	} 
	if( __sdl_binkheight > 0 ) { 
		__sdl_scaley = (float)__sdl_binkheight / height; 
	} else { 
		__sdl_binkheight = height; 
	} 

	if( __sdl_fullscreen ) { 
		my_flags = my_flags | SDL_FULLSCREEN; 
	} else { 
		my_flags = my_flags & ~SDL_FULLSCREEN; 
	}

	sdl_showcursor_ptr( SDL_DISABLE ); 

/* Hey, you know, some comments here might of been useful....
 *
 * This creates a fake software surface for BinkPlayer to draw into. 
 * Binkplayer draws into our fake surface, and then inside UpdateRects
 * We either blit from the fake software surface directly back into the 
 * real screen surface (__sdl_screen_surface), or we use the zoom functions
 * to scale fake surface to real screen surface.
 */

	__sdl_screen_surface = __sdl_setvideomode(__sdl_binkwidth, __sdl_binkheight, bpp, my_flags); 
	if( __sdl_screen_surface != NULL ) { 
	
		/* Ordinarily SDL_AllocSurface, but there's a define that redef's it into SDL_CreateRGBSurface */	
		__sdl_fake_surface = sdl_creatergbsurface_ptr( SDL_SWSURFACE, width, height, 32, 
					__sdl_screen_surface->format->Rmask, __sdl_screen_surface->format->Gmask, 
					__sdl_screen_surface->format->Bmask, __sdl_screen_surface->format->Amask ); 

		if( __sdl_fake_surface == NULL ) { 
			fprintf(stderr, "BinkLib: SDL_AllocSurface failed: %s\n", sdl_geterror_ptr());
			return(NULL); 
		} 

		return( __sdl_fake_surface );
	}

	return( NULL ); 
} 

int nanosleep(const struct timespec *req, struct timespec *rem) { 
	struct	timeval	tv; 
	__sdl_gettimeofday( &tv, NULL); 

	if( __sdl_debug ) { 
		fprintf(stderr, "%ld.%06ld: BinkLib: Calling nanosleep(%ld.%06ld)\n", tv.tv_sec, tv.tv_usec, req->tv_sec, req->tv_nsec); 
	}

	if( !__sdl_enabled || __sdl_noperf != NULL ) { 
		return( __sdl_nanosleep(req, rem) );
	}

	if( __sdl_nanosleep == NULL ) { return 0; } 
	return( __sdl_nanosleep(req, rem) );
}

/* A discovery by lje found that wrapping this function so it 
   pauses occasionally and ergo, lowers cpu dramatically. 
 */ 

int __sdl_gtod_counter = 0;

int gettimeofday(struct timeval *tv, struct timezone *tz) {
	if( !__sdl_enabled || __sdl_noperf != NULL ) { 
		return(__sdl_gettimeofday(tv, tz)); 
	}

	if ((++__sdl_gtod_counter) > 5) {
		__sdl_gtod_counter = 0;
		usleep(1);
	}
	return( __sdl_gettimeofday(tv, tz) );
}
