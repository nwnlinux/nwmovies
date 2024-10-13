#!/usr/bin/perl -w 

# Dweebie installation program. 
#
# - David Holland zzqzzq_zzq@hotmail.com - 7/7/03
# - David Holland david.w.holland@gmail.com 5/3/06

use strict; 

sub do_exec($);
sub do_symlink($);
sub sound_check(); 

use vars qw( $size $command );
use vars qw( $location );
use vars qw( @array $line $offset );
use vars qw( $dir $ndir ); 
use vars qw( $i ); 

use vars qw( $machine $x86_64 ); 
use vars qw( $gcc $cflags $ldflags );
use vars qw( $nasm $nasmFlags ); 

use vars qw ( @sourceList ); @sourceList = qw( nwmovies nwmovies_cookie nwmovies_lookup nwmovies_player );
use vars qw ( @objectList );

use vars qw( $sourceFile $objectFile ); 

if( exists( $ENV{"CC"} )) 		{ $gcc = $ENV{"CC"}; 		  } else { $gcc = "gcc -fPIC"; }
if( exists( $ENV{"CFLAGS"} )) 		{ $cflags = $ENV{"CFLAGS"}; 	  } else { $cflags = ""; }
if( exists( $ENV{"LDFLAGS"} )) 		{ $ldflags = $ENV{"LDFLAGS"}; 	  } else { $ldflags = ""; }


# Finish initializing the source/object lists.
for($i = 0; $i < scalar(@sourceList); $i++ ) { 
	$objectList[$i] = sprintf("%s.o", $sourceList[$i]); 
	$sourceList[$i] = sprintf("%s.c", $sourceList[$i]); 

}

# Roughly figure out where we're being called from. 
foreach $dir ( ".", "nwmovies" ) {
        if ( -f "$dir/nwmovies.c" ) {
                $ndir = $dir;
                last;
        }
}

# Command line checking.
if( defined($ARGV[0]) && lc($ARGV[0]) eq "debug" ) { 
	$cflags=sprintf("%s -g -DDEBUG", $cflags); 
}

if( !defined($ARGV[0]) || ( lc($ARGV[0]) ne "build" ) && ( lc($ARGV[0]) ne "debug" ) ) { 
	sound_check(); 
	do_symlink($ndir); 
	exit(0); 
}

$machine = `/bin/uname -m`; chomp($machine); 
if( $machine =~ "x86_64" ) { 
	$x86_64 = "-m32 -I/emul/ia32-linux/usr/include -L/emul/ia32-linux/usr/lib -L/emul/ia32-linux/lib -L/lib32 -L/usr/lib32";
} else { 
	$x86_64 = ""; 
}

# gcc/libdisasm.so
$command = sprintf("%s %s %s -Wall -I%s/libdis -g -fPIC -shared -Wl,-soname,libdisasm.so %s/libdis/libdis.c %s/libdis/i386.c -o %s/libdis/libdisasm.so",
			 $gcc, $cflags, $x86_64, $ndir, $ndir, $ndir, $ndir );
do_exec($command); 

# Build source
for( $i = 0; $i < scalar(@sourceList); $i++ ) { 

	$sourceFile = $sourceList[$i]; 
	$objectFile = $objectList[$i];

	if( -f $ndir . "/" . $objectFile) { 
		printf("NOTICE: Removing old: %s/%s\n", $ndir, $objectFile); 
		unlink($ndir . "/" . $objectFile); 
	}

	printf("NOTICE: Compiling: %s -> %s\n", $sourceFile, $objectFile); 
	$command = sprintf("%s %s %s -Wall -shared -g -I/usr/include/libelf -I%s/libdis", 
			$gcc, $cflags, $x86_64, $ndir); 

	$command = sprintf("%s -o %s/%s -c %s/%s", 
			$command, $ndir, $objectFile, $ndir, $sourceFile ); 

	do_exec($command); 
}

# nwmovies_link.so
# nasm -f elf32 -o nwmovies_link.o nwmovies_link.asm
# gcc -m32 -g -o nwmovies_link.o nwmovies_link.S
$command = sprintf("%s %s %s -o %s/nwmovies_link.o -c %s/nwmovies_link.S", $gcc, $cflags, $x86_64, $ndir, $ndir);
do_exec($command); 

if( -f $ndir . "/nwmovies.so") { 
	printf("NOTICE: Removing old: %s/nwmovies.so\n", $ndir);
	unlink($ndir . "/nwmovies.so");
}

#gcc -m32 -shared \
# ./nwmovies.o ./nwmovies_lookup.o ./nwmovies_cookie.o ./nwmovies_player.o ./nwmovies_link.o  \
# -o ./nwmovies.so 

$command = sprintf("%s %s %s -shared",		$gcc, $ldflags, $x86_64); 
$command = sprintf("%s %s/%s",			$command, $ndir, join(" " . $ndir . "/", @objectList)); 
$command = sprintf("%s %s/nwmovies_link.o",	$command, $ndir );
$command = sprintf("%s -o %s/nwmovies.so",	$command, $ndir ); 
$command = sprintf("%s -lelf",			$command );		# Add libelf
do_exec($command);

# If debugging, do not cleanup.
if( defined($ARGV[0]) && lc($ARGV[0]) eq "debug" ) { 
	printf("NOTICE: Early exit for debugging...\n"); 
	exit(0);
}

printf("\n"); 
# Perform cleanup
for( $i = 0; $i < scalar(@objectList); $i++ ) { 
	$objectFile = $objectList[$i];
	if( -f $ndir . "/" . $objectFile) { 
		printf("NOTICE: Removing intermediate object file: %s/%s\n", $ndir, $objectFile); 
		unlink($ndir . "/" . $objectFile); 
	}
}
printf("NOTICE: Removing intermediate object file: %s/nwmovies_link.o\n", $ndir);
unlink($ndir . "/nwmovies_link.o");

printf("\n"); 
do_symlink($ndir); 

printf("NOTICE: NWMovies: Please check for errors above\n");
printf("NOTICE: NWMovies: Modify your nwn startup command to set LD_PRELOAD to\n");
printf("NOTICE: NWMovies: include \"./nwmovies.so\" before executing nwmain.\n");

exit(0); 

### Subs ###

sub do_symlink($) { 

	my ($ndir) = @_; 
# Install symlinks
	if( $ndir eq "." ) { 
		chdir(".."); 
	}

	printf("NOTICE: Installing nwmovies.so symlink..."); 
	symlink("nwmovies/nwmovies.so", "nwmovies.so"); 
	printf("\n"); 

	return; 
}

sub do_exec($) { 
	my( $cmd ) = @_; 
	my $ret; 

	printf("NOTICE: NWMovies: Executing: %s\n", $cmd); 
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

        printf("NOTICE: The sound configuration check was so out of date it has been removed\n"); 
	printf("NOTICE: Please utilize the \"build\" option to build NWMovies\n");
	return; 

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
