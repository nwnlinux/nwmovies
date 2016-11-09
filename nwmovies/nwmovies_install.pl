#!/usr/bin/perl -w 

# Dweebie installation program. 
#
# - David Holland zzqzzq_zzq@hotmail.com - 7/7/03
# - David Holland david.w.holland@gmail.com 5/3/06

use strict; 

use vars qw(  $size $command );
use vars qw( $location );
use vars qw( @array $line $offset );
use vars qw( $dir $ndir ); 

use vars qw( $machine $x86_64 ); 
use vars qw( $gcc $cflags $ldflags );

sub do_exec($);
sub sound_check(); 

if( exists( $ENV{"CC"} )) {
        $gcc = $ENV{"CC"};
} else {
        $gcc = "gcc -m32";
}

if( exists( $ENV{"CFLAGS"} )) {
        $cflags = $ENV{"CFLAGS"};
} else {
        $cflags = "";
}

if( exists( $ENV{"LDFLAGS"} )) {
        $ldflags = $ENV{"LDFLAGS"};
} else {
        $ldflags = "";
}

if( !defined($ARGV[0]) || lc($ARGV[0]) ne "build" ) { 
	sound_check(); 
	exit(0); 
}

foreach $dir ( ".", "nwmovies" ) {
        if ( -f "$dir/nwmovies.c" ) {
                $ndir = $dir;
                last;
        }
}

$machine = `/bin/uname -m`; chomp($machine); 
if( $machine =~ "x86_64" ) { 
	$x86_64 = "-I/emul/ia32-linux/usr/include -L/emul/ia32-linux/usr/lib -L/emul/ia32-linux/lib -L/lib32 -L/usr/lib32";
} else { 
	$x86_64 = ""; 
}

$command = sprintf("%s %s %s -Wall -I%s/libdis -g -fPIC -shared -Wl,-soname,libdisasm.so %s/libdis/libdis.c %s/libdis/i386.c -o %s/libdis/libdisasm.so",
			 $gcc, $cflags, $x86_64, $ndir, $ndir, $ndir, $ndir );
do_exec($command); 

$command = sprintf("%s %s %s -Wall -shared -g -I/usr/include/libelf -I%s/libdis -o %s/nwmovies.so %s/nwmovies.c %s/nwmovies_lookup.c %s/nwmovies_cookie.c %s/nwmovies_player.c %s/nwmovies_link.S %s -ldl -Wl,-static -lelf -Wl,-Bdynamic", 
			$gcc, $cflags, $x86_64,  $ndir, $ndir, $ndir, $ndir, $ndir, $ndir, $ndir, $ldflags ); 
do_exec($command); 

# Install symlinks
if( $ndir eq "." ) { 
	chdir(".."); 
}
symlink("nwmovies/nwmovies.so", "nwmovies.so"); 

printf("\n"); 
sound_check(); 
printf("\n"); 

printf("NOTICE: NWMovies: Please check for errors above\n");
printf("NOTICE: NWMovies: Built nwmovies.so and libdis.so libraries which need manually copied to games\n");
printf("NOTICE: NWMovies: lib directory. Then modify your nwn startup command to set LD_PRELOAD to\n");
printf("NOTICE: NWMovies: include \"./lib/nwmovies.so\" before executing nwmain.\n");

exit(0); 

### Subs ###

sub do_exec($) { 
	my( $cmd ) = @_; 
	my $ret; 

	printf("NOTICE: NWMovies: Executing:\n%s\n", $cmd); 
	$ret = system($cmd); 
	if ($ret == -1) { 
		die("ERROR: failed to execute: $!\n");
	} elsif ($ret & 127) {
		printf("ERROR: child died with signal %d, %s coredump\n", 
			($ret & 127),  ($ret & 128) ? 'with' : 'without');
		die("ERROR: Horrible screaming death in do_exec()\n"); 
	} elsif( ($ret >> 8) != 0 ) {
		printf("ERROR: child exited with value %d\n", $ret >> 8);
		die("ERROR: system() call failed.\n"); 
	}
	return; 
}

# Sound configuration check(s).

sub sound_check() { 

	my $ret; 
	my @lines; 
	my @words; 
	my @subwords;
	my $i; 
	my $j; 

	printf("NOTICE: Examining sound configuration for some clues...\n"); 
	printf("NOTICE: Take this output with a grain of salt..\n\n"); 

	# Check for ESD
	$ret = open(CMD, "/usr/bin/pgrep esd |"); 
	if( !defined($ret)) { 
		printf("WARNING: Failed to spawn 'pgrep esd'\n"); 
	} else { 
		@lines = <CMD>; 
		close(CMD); 
		if( scalar( @lines ) != 0 ) { 
			printf("NOTICE: It appears you have ESD running.\n"); 
		}
	}

	# Check for ARTS
	$ret = open(CMD, "/usr/bin/pgrep artsd |"); 
	if( !defined($ret)) { 
		printf("WARNING: Failed to spawn 'pgrep artsd'\n"); 
	} else { 
		@lines = <CMD>; 
		close(CMD); 
		if( scalar( @lines ) != 0 ) { 
			printf("NOTICE: It appears you have KDE/ARTSD running.\n"); 
		}
	}

	# At this point we check for ALSA
	$ret = open(FILE, "/proc/asound/pcm"); 
	if( !defined($ret) ) { 
		printf("NOTICE: ALSA not present.\n"); 
	} else { 
		@lines = <FILE>; 
		close(FILE); 
		chomp(@lines);
		if( scalar(@lines) == 0 ) { 
			printf("NOTICE: No ALSA devices present.\n"); 
		}
		foreach $i (@lines) { 
			@words = split(/:/, $i); 
			foreach $j (@words) { 
				if( $j =~ /playback/ ) { 
					@subwords = split(/\s+/, $j);
					if( $subwords[2] > 1 ) { 
						printf("NOTICE: At least one ALSA device support multiple PCM playback channels\n");
						return; 
					} 
				} 
			}
		} 
		printf("NOTICE: It appears none of your PCM playback devices supports\n"); 
		printf("NOTICE: more than one simultaneous channel.\n");
		printf("NOTICE: You may need to look into software mixing, via dmix, or ESD/ARTSD.\n"); 
	}
	return; 
}
