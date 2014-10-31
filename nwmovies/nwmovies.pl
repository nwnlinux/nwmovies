#!/usr/bin/perl 

use strict; 
use File::Basename;
use File::Glob ':glob';

use vars qw(@files $file $key %hash $command); 
use vars qw(%binkhash); 
use vars qw($played); 
use vars qw($fullscreen $line @array); 
use vars qw($height $width);
use vars qw($ld_preload $moviepath $childpid);

use vars qw($nwuser_loaded); 
use vars qw($ret @skip_movies %skip_movies);

use vars qw( @old_preload @new_preload $i );

use vars qw( $mplayer $plaympeg $binkplayer ); 

# Shush warnings in the x86_64 environment. 
$nwuser_loaded = 0; 		# Presume 0.
$ld_preload = $ENV{"LD_PRELOAD"}; 
delete( @ENV{"LD_PRELOAD"} ); 

$mplayer = qx{ which mplayer 2>/dev/null };
$plaympeg = qx{ which plaympeg 2>/dev/null }; 
$binkplayer = qx{ which BinkPlayer 2>/dev/null }; 

if( $binkplayer eq "" && -x "./BinkPlayer" ) { 
	$binkplayer = "./BinkPlayer";
}

if( $binkplayer eq "" && -x "./nwmovies/BinkPlayer" ) {
	$binkplayer = "./nwmovies/BinkPlayer";
}

chomp($mplayer); 
chomp($plaympeg); 
chomp($binkplayer); 

# Simple way to disable a specific movie player.  
# Uncomment the player you want to disable. 

# $mplayer = "";
# $plaympeg = "";
# $binkplayer = "";

# Note both mplayer, and plaympeg must be unavailable, or uncommented
# to try Bink support.

# If you have SDL_gfx installed/symlinked inside of ~nwn and have a big enough
# computer you may try to scale the movies fullscreen. 
# FWIW a AMD dual 2600MP's is NOT fast enough to scale the movies
# to 1600x1200.
# A Q9300 however is enough to scale the movies to 1280x1024 w/ smothing
#$ENV{"BINK_SCALE"} = 1; 		# Scale movie to full screen.
#$ENV{"BINK_SMOOTH"} = 1; 		# Smooth the scaled movie for even 
					# more CPU utilization. 

#$ENV{"BINK_NOPERF"} = 1; 		# Disable some performance improvement code inside
					# of BinkLib.so (Debugging mostly)

printf("NOTICE: NWMovies.pl playing: %s: %s\n", $ARGV[0], scalar(localtime) ); 

# Case insensitive movie selector.
#
# Do this the hard way due to some weirdness w/ perl & NWuser.
# Note won't work if NWUser is modified for something other than $HOME/.nwn
#

$moviepath = "./movies"; 
if( $ld_preload =~ /nwuser/ ) { 
	printf("NOTICE: NWUser loaded. Examining \$HOME/.nwn/movies for movies as well.\n"); 
	$nwuser_loaded = 1; 
} 

# Must remove NW* from the preload
@old_preload = split(/:/, $ld_preload); 
@new_preload = (); 
foreach $i (@old_preload) {
	if( !($i =~ /nwmovies/) && !($i =~/nwmouse/) && !($i =~/nwuser/) && !($i =~/nwlogger/)) { 
		push(@new_preload); 
	}
}
if( scalar(@new_preload) ) { 
	$ld_preload = join(":", @new_preload); 
	$ENV{"LD_PRELOAD"} = $ld_preload;
} else { 
	$ld_preload = ""; 
	delete @ENV{"LD_PRELOAD"}; 
}

# Load the available files. 
@files = (); 
open(CMD, "ls ${moviepath} 2>/dev/null |") || die("ERROR: NWMovies.pl: Unable to spawn command: $!\n"); 
while( $line = <CMD> ) { 
	chomp($line); 
	$line = "./movies/" . $line; 
	push(@files, $line); 
}
close(CMD); 

# Check for nwuser, and load the nwuser'd movies.
if( $nwuser_loaded == 1) { 
	open(CMD, "ls \$HOME/.nwn/movies 2>/dev/null |") || die("ERROR: NWMovies.pl: Unable to spawn command: $!\n"); 
	while( $line = <CMD> ) { 
		chomp($line); 
		$line = "\$HOME/.nwn/movies/" . $line; 
		push(@files, $line); 
	}
	close(CMD); 
}

# Per a suggestion by Skildron, allow users to skip specific movies (always) by creating a 
# "nwmovies.skip" file in either the NWN directory or $HOME/.nwn.  This is useful if
# you're actually using 'nwuser' and you've a movie installed in a global area, but 
# really really don't want to watch it.   Note these are stored 1 per line, case
# insensitive, but without the '.bik' extension. 

@skip_movies = (); 
if( -e "./nwmovies.skip" ) { 
	printf("NOTICE: Attempting to read ./nwmovies.skip for movies to skip.\n"); 
	$ret = open(SKIP, "./nwmovies.skip" );
	if( ! $ret ) { 
		printf("WARNING: NWMovies.pl unable to open skip file ./nwmovies.skip. Ignoring: $!\n");
	} else { 
		@skip_movies = <SKIP>; 
		close(SKIP);
	}
}

# Note, this allows "nwuser" to override a global nwmovies.skip.
if( $nwuser_loaded && -e $ENV{"HOME"} . "/.nwn/nwmovies.skip" ) { 
	printf("NOTICE: Attempting to read %s/.nwn/nwmovies.skip for movies to skip.\n", $ENV{"HOME"} ); 
	$ret = open(SKIP, $ENV{"HOME"} . "/.nwn/nwmovies.skip" );
	if( ! $ret ) { 
		printf("WARNING: NWMovies.pl unable to open skip file %s/.nwn/nwmovies.skip. Ignoring: $!\n", $ENV{"HOME"} );
	} else { 
		@skip_movies = <SKIP>; 
		close(SKIP);
	}
}

# Convert movie list into hash, so we can use exist later on. 
%skip_movies = (); 
if( scalar(@skip_movies) ) { 
	chomp(@skip_movies); 
	foreach (@skip_movies) { $skip_movies{ lc($_) } = 1; } 
}
#### Movies to be skipped is now in %skip_movies
#
if( exists( $skip_movies{ lc($ARGV[0]) } ) ) { 
	#want to skip, bye..
	printf("NOTICE: Skipping %s, listed in nwmovies.skip.\n", $ARGV[0]); 
	exit(1); 
}

# Create a hash of %{ lc(moviename) } -> pathname 
foreach $file (@files) { 

	$key = lc($file); 
	if ( substr( $key, -3 ) ne "bik" ) { 
		$key = basename($key); 
		$key = fileparse($key, qr{\..*});
	
		$hash{ $key } = $file; 
	} else { 
		$key = basename($key); 
		$key = fileparse($key, qr{\..*});
	
		$binkhash{ $key } = $file; 
	}
}

# braindead, but functional INI parser 

$fullscreen = 1; 			# Presume fullscreen
$width = 800;
$height = 600;
open(INI, "./nwn.ini") || die("ERROR: NWMovies.pl: Unable to open INI file: $!\n"); 
while( $line = <INI> ) { 
	chomp($line); 
	if( index( lc($line), "fullscreen" ) >= 0 ) {
		@array = split(/=/, $line); 
		$fullscreen = $array[1] + 0; 
	}
	if( index( lc($line), "width" ) >= 0 ) { 
		@array = split(/=/, $line); 
		$width = $array[1] + 0; 
	}
	if( index( lc($line), "height" ) >= 0 ) { 
		@array = split(/=/, $line); 
		$height = $array[1] + 0; 
	}
} 
close(INI);

if( exists( $hash{ lc($ARGV[0]) } )  || exists( $binkhash{ lc($ARGV[0]) } )   ) { 

	if( $mplayer eq "" && $plaympeg eq "" && $binkplayer eq "" ) { 
		printf("ERROR: No movie player located, not playing anything.\n"); 
		exit(1); 
	}

	$played = 0; 		#  Changes to one if we've found a player, and played something. 

# Prefer BinkPlayer, followed by Mplayer at this point. 

	if( $binkplayer ne "" && !$played ) {

		# The BinkPlayer Understands these variables w/ the preloaded library
 		# BINK_WIDTH, BINK_HEIGHT, BINK_FULLSCREEN, BINK_SMOOTH, BINK_SCALE

		if( $ld_preload ne "" ) { 
			$ld_preload = "./nwmovies/binklib.so:" . $ld_preload;
		} else { 
			$ld_preload = "./nwmovies/binklib.so";
		}

		if( $fullscreen ) { 
			$ENV{"BINK_WIDTH"}=$width;
			$ENV{"BINK_HEIGHT"}=$height;
			$ENV{"BINK_FULLSCREEN"}=1;
			$command = sprintf("%s %s", $binkplayer, $binkhash{ lc($ARGV[0]) } ); 
		} else { 
			$ENV{"BINK_FULLSCREEN"}=0; 
			$command = sprintf("%s %s", $binkplayer, $binkhash{ lc($ARGV[0]) } ); 
		}
		$played = 1; 
	}

	if( $mplayer ne "" && !$played ) { 
		if( $fullscreen ) { 
			# Note:  -vo x11 also works for older versions of mplayer
			$command = sprintf("%s -x %s -y %s -vo sdl -vm %s", $mplayer, $width, $height, $hash{ lc($ARGV[0]) } ); 
		} else { 
			# Note:  -vo x11 also works for older versions of mplayer
			$command = sprintf("%s -x %s -y %s -vo sdl %s", $mplayer, $width, $height, $hash{ lc($ARGV[0]) } ); 
		}
		$played = 1; 
	}


	if( $plaympeg ne "" && !$played ) { 	
		if( !defined( $hash{ lc($ARGV[0]) } ) ) { 
			if( $fullscreen ) { 
				$command = sprintf("%s --scale %sx%s --fullscreen %s", $plaympeg, $width, $height, $hash{ lc($ARGV[0]) } ); 
			} else { 
				$command = sprintf("%s --scale %sx%s %s", $plaympeg, $width, $height, $hash{ lc($ARGV[0]) } ); 
			}
		} else { 
			if( $fullscreen ) { 
				$command = sprintf("%s --scale %sx%s --fullscreen %s", $plaympeg, $width, $height, $hash{ lc($ARGV[0]) } ); 
			} else { 
				$command = sprintf("%s --scale %sx%s %s", $plaympeg, $width, $height, $hash{ lc($ARGV[0]) } ); 
			}
		}
		$played = 1; 
	}

	printf("NOTICE: NWMovies: Executing: %s\n", $command); 

# Converted to fork/exec
#	system($command); 

	if (!defined($childpid = fork())) {
    		printf("ERROR: NWMovies.pl: cannot fork: $!\n");
	} elsif ($childpid == 0) {
		$ENV{"LD_PRELOAD"} = $ld_preload;
    		exec("$command");
    		printf("ERROR: NWMovies.pl: can't exec $command: $!\n"); 
	} else { 
    		waitpid($childpid, 0);
	} 

} else { 
	printf("ERROR: NWMovies.pl: Missing movie file: %s\n", $ARGV[0]); 
}

printf("NOTICE: NWMovies.pl finished playing: %s: %s\n", $ARGV[0], scalar(localtime) ); 
exit(0); 

