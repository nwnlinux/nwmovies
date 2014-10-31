/*
 * Link the modified nwmain with the nwmovies C code
 */

	.text

	.align	4,0x90
	.globl	NWMovies_playmovie
NWMovies_playmovie:
	mov %esi, _NWMovies_ESI

	/* replicate code we whacked */
	push	%esi
	lea	0xffffffe8(%ebp),%ebx
	push	%ebx

	pusha
	call	NWMovies_playmovie2
	popa

	jmp	*_NWM_movie_retaddr
