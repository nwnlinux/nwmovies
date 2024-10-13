/* https://stackoverflow.com/questions/78485769/cannot-assemble-my-code-waring-warning-relocation-in-read-only-section-text */

	.text
	.align	4,0x90

/* Things that need to be visible external to this code.  */
	.globl	NWMovies_playmovie
	.globl  _NWMovies_ESI

	.globl  NWMovies_link_store_ESI
	.globl  NWMovies_link_playmovie2
	.globl  NWMovies_link_exit_jmp

NWMovies_playmovie:
	push	%eax				/* save EAX */

NWMovies_link_store_ESI:
	movl	$0x00000000, %eax		/* Overwrite the 0x0 address, with the address of NWMovies_ESI  */
	mov	%esi, (%eax)			/* Save ESI into NWMovies_ESI */

	pop	%eax				/* restore EAX */

/* replicate code we overwrote with the jump to NWMovies_playmovie */
	push	%esi
	lea	0xffffffe8(%ebp),%ebx
	push	%ebx

/* call our internal playmovie routine */
	pusha
NWMovies_link_playmovie2:
	call	0x00000000			/* Call playmovie2 (working) play movie code -- overwitten by setup_memory */
	popa

/* jmp to the return address */
NWMovies_link_exit_jmp:
	jmp	*0x00000000			/* Overwrite the 0x0 address with the address stored in NWM_movie_retaddr */

/* Get the current EIP address into EAX 	*/
/* _NWMovies_geteip: 				*/
/* 	mov	(%esp), %eax			*/
/* 	ret					*/

.data 	
	.type	_NWMovies_ESI, @object
	.size	_NWMovies_ESI, 4
_NWMovies_ESI: 	.long 0xdeadbeef
