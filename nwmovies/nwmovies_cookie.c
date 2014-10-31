#define _GNU_SOURCE		/* Needed so dlfcn.h defines the right stuff */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <dlfcn.h>
#include <errno.h>

#include <sys/mman.h>
#include <limits.h>

#include <elf.h>
#include <libelf.h>

#include <link.h>

#include "libdis.h"

#ifndef PAGESIZE
#define PAGESIZE 4096
#endif

#define NWM_PROTECTED 1
#define NWM_UNPROTECTED 0

static char *NWMovies_cookies[] = { 
 	"setnz",		/* setne */
	"dec",
	"and",
	"add",
	"mov",
	"mov",
	"mov",
	"mov",
	NULL,
};

/* This find a _LOT_ of false positives, however, the key thing is the 
   Address being pushed by the last push, should be the address of a string containing "NewButton"
 */

static char *NWMovies_cookies2[] = { 
	"push",
	"mov",
	"push",
	"push",
	"push",
	"sub",
	"mov",
	"push",
	NULL, 
}; 


int *NWMovies_findcookie(char *filename)
{
	Elf			*elf_ptr; 
	int			fd; 
	Elf32_Ehdr		*ehdr; 
	Elf_Scn			*section; 
	Elf32_Shdr		*section_header;
	Elf32_Shdr		*code_header;
	Elf32_Shdr		*rodata_header; 
	char			*section_name; 
	unsigned int		i, xx, instruction_size, matches_found; 
	unsigned char		*buffer, *entry; 
	unsigned char		*buffer_ptr; 
	unsigned char		*cookie_address; 
	unsigned long		push_address; 
	unsigned long		push_address2; 			/* Second word push, signals end of routine. */
	struct 		instr 	current_instruction;
	struct		stat	statbuf; 
	float			pct_complete;

	int			call_counter; 
	static	int		calls[7]; 			/* Return things in this array */
	int			ret_wait; 

	unsigned int		prev_instruction; 

	unsigned int		call_destination; 
	unsigned int		call1_addr, call2_addr, call3_addr; 
	unsigned int		final_call; 

	/* Dynamic Linking, we only use this stuff if we need it */

	int			(*disassemble_init_ptr)(int, int); 
	int			(*disassemble_address_ptr)(unsigned char *, struct instr *);
	int			(*disassemble_cleanup_ptr)(void); 
	void			*dlhandle; 

	dlhandle = dlopen("nwmovies/libdis/libdisasm.so", RTLD_NOW); 
	if( !dlhandle ) { 
		fprintf(stderr, "ERROR: NWMovies: (cookie) dlopen of libdisasm.so failed: %s\n", dlerror()); 
		abort(); 
	}

	disassemble_init_ptr = dlsym(dlhandle, "disassemble_init"); 
	if( disassemble_init_ptr == NULL ) { 
		fprintf(stderr, "ERROR: NWMovies: (cookie) dlsym(disassemble_init) failed: %s\n", dlerror()); 
		abort(); 
	}
	disassemble_address_ptr = dlsym(dlhandle, "disassemble_address"); 
	if( disassemble_address_ptr == NULL ) { 
		fprintf(stderr, "ERROR: NWMovies: (cookie) dlsym(disassemble_address) failed: %s\n", dlerror()); 
		abort(); 
	}
	disassemble_cleanup_ptr = dlsym(dlhandle, "disassemble_cleanup"); 
	if( disassemble_cleanup_ptr == NULL ) { 
		fprintf(stderr, "ERROR: NWMovies: (cookie) dlsym(disassemble_cleanup) failed: %s\n", dlerror()); 
		abort(); 
	}
	disassemble_init_ptr(0, INTEL_SYNTAX); 
		
/* Initialize the elves. */

	if (elf_version(EV_CURRENT) == EV_NONE) {
		fprintf(stderr, "ERROR: NWMovies: (cookie) libelf version mismatch.\n"); 
		abort(); 
	}

/* open library */ 	

	fd = open(filename, O_RDONLY); 
	if( fd < 0 ) { 
		fprintf(stderr, "ERROR: NWMovies: (cookie) Unable to open shared library: %s (%d)\n", filename, errno); 
		abort(); 
	}
	if( fstat(fd, &statbuf) < 0 ) { 
		fprintf(stderr, "ERROR: NWMovies: (cookie) Unable to stat shared library: %s (%d) Howd that happen?\n", filename, errno); 
		abort(); 
	}
	buffer = mmap(0, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if( buffer < 0 ) { 
		fprintf(stderr, "ERROR: NWMovies: (cookie) Unable to mmap executable: %s (%d)\n", filename, errno); 
		abort(); 
	}
	elf_ptr = elf_begin(fd, ELF_C_READ, (Elf *)0);
	if( elf_ptr == NULL) { 
		fprintf(stderr, "ERROR: NWMovies: (cookie) elf_begin failed: %s.\n", elf_errmsg(elf_errno())); 
		abort(); 
	} 

	/* Get the Header */
	if ( (ehdr = elf32_getehdr(elf_ptr)) == NULL) { 
		fprintf(stderr, "ERROR: NWMovies: (cookie) Unable to get Elf header: %s\n",  elf_errmsg(elf_errno()) ); 
		abort(); 
	}
	code_header = NULL; 
	section = 0; 
	while( (section = elf_nextscn( elf_ptr, section )) ) { 
		section_header = elf32_getshdr(section); 
		if( section_header != NULL ) { 
			section_name = elf_strptr(elf_ptr, ehdr->e_shstrndx, (size_t)section_header->sh_name);
			if( section_name != NULL && !strcmp( section_name, ".text" ) ) { 
				code_header = section_header; 
				break; 
			}
		}
	}
	if( code_header == NULL ) { 
		fprintf(stderr, "ERROR: NWMovies: (cookie) Unable to locate appropriate code section.\n"); 
		abort(); 
	}

	rodata_header = NULL; 
	section = 0; 
	while( (section = elf_nextscn( elf_ptr, section )) ) { 
		section_header = elf32_getshdr(section); 
		if( section_header != NULL ) { 
   	  		section_name = elf_strptr(elf_ptr, ehdr->e_shstrndx, (size_t)section_header->sh_name);
			if( section_name != NULL && !strcmp( section_name, ".rodata" ) ) { 
				rodata_header = section_header; 
				break; 
			}
		}
	}

	if( rodata_header == NULL ) { 
		fprintf(stderr, "ERROR: NWMovies: (cookie) Unable to locate appropriate rodata section.\n"); 
		abort(); 
	}

	/* Found start of program */
	entry = (unsigned char *)ehdr->e_entry - (code_header->sh_addr - code_header->sh_offset);
	fprintf(stderr, "NOTICE: NWMovies: (cookie) Entry point determined: %p\n", entry); 
	buffer_ptr = (unsigned char *) (int)entry + (int)buffer; 

	i=0; matches_found = 0; 
	cookie_address = NULL; 
	pct_complete = ((float)i / ((int)code_header->sh_size)) * 100.0; 
	fprintf(stderr, "NOTICE: NWMovies: (cookie) Searching executable: %02d", (int)pct_complete); 
	while( i < (int)code_header->sh_size ) { 
		pct_complete = ((float)i / ((int)code_header->sh_size)) * 100.0; 
		if( ((int)pct_complete % 4) == 0 ) { 
			printf("%02d", (int)pct_complete); 
		}
		memset(&current_instruction, 0, sizeof(struct code));
		instruction_size = disassemble_address_ptr( buffer_ptr + i, &current_instruction ); 
		if( instruction_size ) { 

#ifdef _NOTDEF_
				for (xx = 0; xx < 12; xx++) {
					if (xx < instruction_size) printf("%02x ", buffer_ptr[i + xx]);
					else printf("   ");
				}

				printf("%s", current_instruction.mnemonic);
				if (current_instruction.dest[0] != 0) printf("\t%s", current_instruction.dest);
				if (current_instruction.src[0] != 0) printf(", %s", current_instruction.src);
				if (current_instruction.aux[0] != 0) printf(", %s", current_instruction.aux);
				printf("\n");
#endif
			
			if( strcmp(current_instruction.mnemonic, (char *)NWMovies_cookies[matches_found]) == 0 ) { 
				matches_found++; 
				if( NWMovies_cookies[matches_found] == NULL ) { 
					cookie_address = (unsigned char *)i; 
					break; 
				}
			} else { 
				matches_found = 0; 
			}
			i += instruction_size; 
		} else { 
			fprintf(stderr, "\nERROR: NWMovies: (cookie) Invalid instruction disassembled: %08x\n", i); 
			fprintf(stderr, "ERROR: NWMovies: (cookie) Probably a bug in libdis.\n"); 
			fprintf(stderr, "ERROR: NWMovies: (cookie)    Please contact David Holland (david.w.holland@gmail.com)\n"); 
			abort();
		}
	}
	fprintf(stderr, "\n"); /* Clean up after percent display */
	if( cookie_address == NULL ) { 
		fprintf(stderr, "SERIOUS FATAL ERROR: NWMovies: (cookie) Magic cookie not found.\n"); 
		fprintf(stderr, "SERIOUS FATAL ERROR: NWMovies: (cookie)    Please contact David Holland (david.w.holland@gmail.com)\n"); 
		abort(); 
	}

	fprintf(stderr, "NOTICE: NWMovies: (cookie) Cookie location: %p\n", cookie_address); 
	
	call_counter = 0; 
	ret_wait = 1; 
	i = (int)cookie_address; 
	pct_complete = ((float)i / ((int)code_header->sh_size)) * 100.0; 
	fprintf(stderr, "NOTICE: NWMovies: (cookie) Searching executable: %02d", (int)pct_complete); 
	while( i < (int)code_header->sh_size ) {
		pct_complete = ((float)i / ((int)code_header->sh_size)) * 100.0; 
		if( ((int)pct_complete % 4) == 0 ) { 
			printf("%02d", (int)pct_complete); 
		}
		memset(&current_instruction, 0, sizeof(struct code));
		memset(&current_instruction, 0, sizeof(struct code));
		instruction_size = disassemble_address_ptr( buffer_ptr + i, &current_instruction );
		if( instruction_size ) {
			if( strcmp(current_instruction.mnemonic, "ret") == 0 ) { 
				ret_wait = 0; 
			} 
			if( !ret_wait ) { 
				if( strcmp(current_instruction.mnemonic, "call") == 0 ) { 
					calls[call_counter++] = i + current_instruction.size + strtol(current_instruction.dest, NULL, 0); 
				}
				if( call_counter == 5 ) { 
					break; 
				}
			}
			i += instruction_size; 
		} else { 
			fprintf(stderr, "\nERROR: NWMovies: (cookie) Invalid instruction disassembled: %08x\n", i); 
			fprintf(stderr, "ERROR: NWMovies: (cookie) Probably a bug in libdis.\n"); 
			fprintf(stderr, "ERROR: NWMovies: (cookie)    Please contact David Holland (david.w.holland@gmail.com)\n"); 
			abort();
		}
	}
	fprintf(stderr, "\n"); /* Clean up after percent display */

	fprintf(stderr, "NOTICE: NWMovies: (cookie) Call Location #1: %08x\n", calls[0]); 
	fprintf(stderr, "NOTICE: NWMovies: (cookie) Call Location #2: %08x\n", calls[1]); 
	fprintf(stderr, "NOTICE: NWMovies: (cookie) Call Location #3: %08x\n", calls[4]); 

/* Search for the difficult ones. */

	call1_addr = call2_addr = call3_addr = 0; 
	i = 0; 
	pct_complete = ((float)i / ((int)code_header->sh_size)) * 100.0; 
	fprintf(stderr, "NOTICE: NWMovies: (cookie) Searching executable: %02d", (int)pct_complete); 
	while( i < (int)code_header->sh_size ) { 
		pct_complete = ((float)i / ((int)code_header->sh_size)) * 100.0; 
		if( ((int)pct_complete % 4) == 0 ) { 
			printf("%02d", (int)pct_complete); 
		}
		memset(&current_instruction, 0, sizeof(struct code)); 
		instruction_size = disassemble_address_ptr( buffer_ptr + i, &current_instruction ); 
		if( instruction_size ) { 
			if( strcmp(current_instruction.mnemonic, "call") == 0 ) { 
				call_destination = i + current_instruction.size + strtol(current_instruction.dest, NULL, 0); 
				if( call_destination == calls[4] ) { 
					final_call = i; 
					break; 
				}
				call1_addr = call2_addr; 
				call2_addr = call3_addr; 
				call3_addr = i; 
			}
		} else { 
			fprintf(stderr, "\nERROR: NWMovies: (cookie) Invalid instruction disassembled: %08x\n", i); 
			fprintf(stderr, "ERROR: NWMovies: (cookie) Probably a bug in libdis.\n"); 
			fprintf(stderr, "ERROR: NWMovies: (cookie)    Please contact David Holland (david.w.holland@gmail.com)\n"); 
			abort();
		}
		i += instruction_size; 
	}
	fprintf(stderr, "\n"); /* Clean up after percent display */

	memset(&current_instruction, 0, sizeof(struct code)); 
	disassemble_address_ptr( buffer_ptr + call1_addr, &current_instruction ); 
	if( strcmp(current_instruction.mnemonic, "call") != 0 ||
		call1_addr + current_instruction.size + strtol(current_instruction.dest, NULL, 0) != calls[0] ) { 
		fprintf(stderr, "ERROR: NWMovies: (cookie) Call destination address, for patch 2 is invalid. %s: %s\n", 
			current_instruction.mnemonic, current_instruction.dest); 
		fprintf(stderr, "ERROR: NWMovies: (cookie)    Please contact David Holland (david.w.holland@gmail.com)\n"); 
		abort(); 
	}

/* Find offsets for patch 0, and 1 */

	for( xx = 0; xx < 2; xx++ ) { 
	prev_instruction = 0; 
	i = calls[xx]; 
	pct_complete = ((float)i / ((int)code_header->sh_size)) * 100.0; 
	fprintf(stderr, "NOTICE: NWMovies: (cookie) Searching executable: %02d", (int)pct_complete); 
	while( i< (int)code_header->sh_size ) { 
		pct_complete = ((float)i / ((int)code_header->sh_size)) * 100.0; 
		if( ((int)pct_complete % 4) == 0 ) { 
			printf("%02d", (int)pct_complete); 
		}
		memset(&current_instruction, 0, sizeof(struct code));
		memset(&current_instruction, 0, sizeof(struct code)); 
		instruction_size = disassemble_address_ptr( buffer_ptr + i, &current_instruction ); 
		if( instruction_size ) { 
			if( strcmp(current_instruction.mnemonic, "pop") == 0 ) { 
				break; 
			} else { 
				prev_instruction = i; 
			}
		} else { 
			fprintf(stderr, "\nERROR: NWMovies: (cookie) Invalid instruction disassembled: %08x\n", i); 
			fprintf(stderr, "ERROR: NWMovies: (cookie) Probably a bug in libdis.\n"); 
			fprintf(stderr, "ERROR: NWMovies: (cookie)    Please contact David Holland (david.w.holland@gmail.com)\n"); 
			abort();
		}
		i += instruction_size; 
	}
	fprintf(stderr, "\n"); /* Clean up after percent display */
	if( prev_instruction != 0 ) { 
		memset(&current_instruction, 0, sizeof(struct code)); 
		disassemble_address_ptr( buffer_ptr + prev_instruction, &current_instruction ); 
	}
	if( 	prev_instruction == 0 || 
		strcmp(current_instruction.mnemonic, "mov") != 0 || strcmp(current_instruction.dest, "eax") != 0 ||
		current_instruction.size != 6 ) { 
		fprintf(stderr, "ERROR: NWMovies: (cookie) Call %d Instruction mismatch. %08x\n", xx, prev_instruction); 
		fprintf(stderr, "ERROR: NWMovies: (cookie)    Please contact David Holland (david.w.holland@gmail.com)\n"); 
		abort(); 
	}
	calls[xx] = prev_instruction; 
	}

/* Find offsets patches 2, and 3 based on 'final_call' */

	calls[2] = 0; calls[3] = 0; 

	memset(&current_instruction, 0, sizeof(struct code)); 
	instruction_size = disassemble_address_ptr( buffer_ptr + final_call, &current_instruction ); 
	i = final_call + instruction_size; 

	pct_complete = ((float)i / ((int)code_header->sh_size)) * 100.0; 
	fprintf(stderr, "NOTICE: NWMovies: (cookie) Searching executable: %02d", (int)pct_complete); 
	while( i < (int)code_header->sh_size ) { 
		pct_complete = ((float)i / ((int)code_header->sh_size)) * 100.0; 
		if( ((int)pct_complete % 4) == 0 ) { 
			printf("%02d", (int)pct_complete); 
		}
		memset(&current_instruction, 0, sizeof(struct code)); 
		instruction_size = disassemble_address_ptr( buffer_ptr + i, &current_instruction ); 
		if( instruction_size ) { 
			if( 	calls[2] == 0 && 
				strcmp(current_instruction.mnemonic, "call") == 0 && 
				current_instruction.size == 5 ) { 
					calls[2] = i; 
			} 

			if(	calls[3] == 0 &&
				strcmp(current_instruction.mnemonic, "jmp") == 0) { 
					calls[3] = i; 
					break; 
			} 
		} else { 
			fprintf(stderr, "\nERROR: NWMovies: (cookie) Invalid instruction disassembled: %08x\n", i); 
			fprintf(stderr, "ERROR: NWMovies: (cookie) Probably a bug in libdis.\n"); 
			fprintf(stderr, "ERROR: NWMovies: (cookie)    Please contact David Holland (david.w.holland@gmail.com)\n"); 
			abort();
		} 
		i += instruction_size; 
	}
	fprintf(stderr, "\n"); /* Clean up after percent display */
	if( calls[2] == 0 || calls[3] == 0 ) { 
		fprintf(stderr, "ERROR: NWMovies: (cookie) Calls 2 and 3 still zero.\n"); 
		fprintf(stderr, "ERROR: NWMovies: (cookie)    Please contact David Holland (david.w.holland@gmail.com)\n"); 
		abort(); 
	}

/* break point */

	prev_instruction = 0; 
	i = calls[4]; 
	pct_complete = ((float)i / ((int)code_header->sh_size)) * 100.0; 
	fprintf(stderr, "NOTICE: NWMovies: (cookie) Searching executable: %02d", (int)pct_complete); 
	while( i < (int)code_header->sh_size) { 
		pct_complete = ((float)i / ((int)code_header->sh_size)) * 100.0; 
		if( ((int)pct_complete % 4) == 0 ) { 
			printf("%02d", (int)pct_complete); 
		}
		memset(&current_instruction, 0, sizeof(struct code)); 
		memset(&current_instruction, 0, sizeof(struct code)); 
		instruction_size = disassemble_address_ptr( buffer_ptr + i, &current_instruction ); 
		if( instruction_size ) { 
			if( strcmp(current_instruction.mnemonic, "lea") == 0 && strcmp(current_instruction.dest, "ebx") == 0 ) { 
				break; 
			} else { 
				prev_instruction = i; 
			}
		} else { 
			fprintf(stderr, "\nERROR: NWMovies: (cookie) Invalid instruction disassembled: %08x\n", i); 
			fprintf(stderr, "ERROR: NWMovies: (cookie) Probably a bug in libdis.\n"); 
			fprintf(stderr, "ERROR: NWMovies: (cookie)    Please contact David Holland (david.w.holland@gmail.com)\n"); 
			abort();
		}
		i += instruction_size; 
	}
	fprintf(stderr, "\n"); /* Clean up after percent display */

	if( prev_instruction != 0 ) { 
		memset(&current_instruction, 0, sizeof(struct code)); 
		disassemble_address_ptr( buffer_ptr + prev_instruction, &current_instruction ); 
	}
	if( 	prev_instruction == 0 || 
		strcmp(current_instruction.mnemonic, "push") != 0 || strcmp(current_instruction.dest, "esi") != 0 ||
		current_instruction.size != 1 ) { 
		fprintf(stderr, "ERROR: NWMovies: (cookie) Call %d Instruction mismatch. %08x\n", xx, prev_instruction); 
		fprintf(stderr, "ERROR: NWMovies: (cookie)    Please contact David Holland (david.w.holland@gmail.com)\n"); 
		abort(); 
	}
	calls[4] = prev_instruction; 

/* Figure out where the disable-enable Movies button bit of code is.  */

	i=0; matches_found =0; 
	push_address = 0; 
	pct_complete = ((float)i / ((int)code_header->sh_size)) * 100.0; 
	fprintf(stderr, "NOTICE: NWMovies: (movies) Searching executable: %02d", (int)pct_complete); 
	while( i < (int)code_header->sh_size ) { 
		pct_complete = ((float)i / ((int)code_header->sh_size)) * 100.0; 
		if( ((int)pct_complete % 4) == 0 ) { 
			printf("%02d", (int)pct_complete); 
		}
		memset(&current_instruction, 0, sizeof(struct code));
		instruction_size = disassemble_address_ptr( buffer_ptr + i, &current_instruction ); 
		if( instruction_size ) { 

#ifdef _NOTDEF_
				for (xx = 0; xx < 12; xx++) {
					if (xx < instruction_size) printf("%02x ", buffer_ptr[i + xx]);
					else printf("   ");
				}

				printf("%s", current_instruction.mnemonic);
				if (current_instruction.dest[0] != 0) printf("\t%s", current_instruction.dest);
				if (current_instruction.src[0] != 0) printf(", %s", current_instruction.src);
				if (current_instruction.aux[0] != 0) printf(", %s", current_instruction.aux);
				printf("\n");
#endif
		

// The string is in the read-only data segment. 
	
			if( strcmp(current_instruction.mnemonic, (char *)NWMovies_cookies2[matches_found]) == 0 ) { 
				matches_found++; 
				if( 	NWMovies_cookies2[matches_found] == NULL ) { 
					if( current_instruction.destType == ( OP_WORD | OP_IMM | OP_R ) ) { 
						push_address = strtol( current_instruction.dest, NULL, 0 ); 
						if( push_address >= rodata_header->sh_addr && push_address <= (rodata_header->sh_addr + rodata_header->sh_size) && !strcmp( (char *)push_address, "NewButton" ) ) { 
							push_address = i; 
							break; 
						}
					}
					matches_found = 0; 
				} 
			} else { 
				matches_found = 0; 
			}
			i += instruction_size; 
		} else { 
			fprintf(stderr, "\nERROR: NWMovies: (movies) Invalid instruction disassembled: %08x\n", i); 
			fprintf(stderr, "ERROR: NWMovies: (movies) Probably a bug in libdis.\n"); 
			fprintf(stderr, "ERROR: NWMovies: (movies)    Please contact David Holland (david.w.holland@gmail.com)\n"); 
			abort();
		}
	}
	fprintf(stderr, "\n"); /* Clean up after percent display */
	if( push_address == 0 ) { 
		fprintf(stderr, "SERIOUS FATAL ERROR: NWMovies: (movies) Magic cookie not found.\n"); 
		fprintf(stderr, "SERIOUS FATAL ERROR: NWMovies: (movies)    Please contact David Holland (david.w.holland@gmail.com)\n"); 
		abort(); 
	}

	/* Ok, we've found the first push, and the start of the appropriate routine. Lets start looking at the pushes, and find the 
	   MoviesButton */

	i=push_address; matches_found = 0; 
	pct_complete = ((float)i / ((int)code_header->sh_size)) * 100.0; 
	fprintf(stderr, "NOTICE: NWMovies: (movies) Searching executable: %02d", (int)pct_complete); 
	while( i < (int)code_header->sh_size ) { 
		pct_complete = ((float)i / ((int)code_header->sh_size)) * 100.0; 
		if( ((int)pct_complete % 4) == 0 ) { 
			printf("%02d", (int)pct_complete); 
		}
		memset(&current_instruction, 0, sizeof(struct code));
		instruction_size = disassemble_address_ptr( buffer_ptr + i, &current_instruction ); 
		if( instruction_size ) { 

#ifdef _NOTDEF_
				for (xx = 0; xx < 12; xx++) {
					if (xx < instruction_size) printf("%02x ", buffer_ptr[i + xx]);
					else printf("   ");
				}

				printf("%s", current_instruction.mnemonic);
				if (current_instruction.dest[0] != 0) printf("\t%s", current_instruction.dest);
				if (current_instruction.src[0] != 0) printf(", %s", current_instruction.src);
				if (current_instruction.aux[0] != 0) printf(", %s", current_instruction.aux);
				printf("\n");
#endif
		

// The string is in the read-only data segment. 

			if( !strcmp(current_instruction.mnemonic, "push") && 
				( current_instruction.destType == ( OP_WORD | OP_IMM | OP_R ) ||
				  current_instruction.destType == ( OP_BYTE | OP_IMM | OP_R ) )
			) {
					push_address = strtol( current_instruction.dest, NULL, 0 ); 
					if( push_address >= rodata_header->sh_addr && push_address <= (rodata_header->sh_addr + rodata_header->sh_size) ) {
						if( !strcmp( (char *)push_address, "MoviesButton" ) ) { 
							matches_found = 1; 
						} else if( !strcmp( (char *)push_address, "OptionsButton" ) ) { 
							fprintf(stderr, "\nNOTICE: This version of %s appears not to have the Movies button disabled.\n", filename); 
							push_address = 1; 
							break; 
						}
					} else if( matches_found && push_address == 0 ) { 
							push_address = i; 
							break; 
					}
				}
			i += instruction_size; 
		} else { 
			fprintf(stderr, "\nERROR: NWMovies: (movies) Invalid instruction disassembled: %08x\n", i); 
			fprintf(stderr, "ERROR: NWMovies: (movies) Probably a bug in libdis.\n"); 
			fprintf(stderr, "ERROR: NWMovies: (movies)    Please contact David Holland (david.w.holland@gmail.com)\n"); 
			abort();
		}
	}
	fprintf(stderr, "\n"); /* Clean up after percent display */
	if( push_address == 0 ) { 
		fprintf(stderr, "SERIOUS FATAL ERROR: NWMovies: (movies) Magic cookie not found.\n"); 
		fprintf(stderr, "SERIOUS FATAL ERROR: NWMovies: (movies)    Please contact David Holland (david.w.holland@gmail.com)\n"); 
		abort(); 
	}

	fprintf(stderr, "NOTICE: NWMovies: (movies) Cookie start_location: %p\n", (void *)push_address); 
	calls[5] = push_address; 

/* Lets locate the end of the bit movie button code so we can not hard code the number of instructions to no-op out */

	if( calls[5] != 1 ) { 

		i=calls[5]; 
		pct_complete = ((float)i / ((int)code_header->sh_size)) * 100.0; 
		fprintf(stderr, "NOTICE: NWMovies: (movies) Searching executable: %02d", (int)pct_complete); 
		while( i < (int)code_header->sh_size ) { 
			pct_complete = ((float)i / ((int)code_header->sh_size)) * 100.0; 
			if( ((int)pct_complete % 4) == 0 ) { 
				printf("%02d", (int)pct_complete); 
			}
			memset(&current_instruction, 0, sizeof(struct code));
			instruction_size = disassemble_address_ptr( buffer_ptr + i, &current_instruction ); 
			if( instruction_size ) { 

				if( !strcmp(current_instruction.mnemonic, "push") && current_instruction.destType == ( OP_WORD | OP_IMM | OP_R ) ) {
					push_address = strtol( current_instruction.dest, NULL, 0 ); 
					if( push_address >= rodata_header->sh_addr && 
					    push_address <= (rodata_header->sh_addr + rodata_header->sh_size) &&
					    !strcmp( (char *)push_address, "OptionsButton" ) 
					) {
						push_address2 = i; 
						break; 
					}
				}
				i += instruction_size; 
			} else { 
				fprintf(stderr, "\nERROR: NWMovies: (movies) Invalid instruction disassembled: %08x\n", i); 
				fprintf(stderr, "ERROR: NWMovies: (movies) Probably a bug in libdis.\n"); 
				fprintf(stderr, "ERROR: NWMovies: (movies)    Please contact David Holland (david.w.holland@gmail.com)\n"); 
				abort();
			}
		}
		fprintf(stderr, "\n"); /* Clean up after percent display */
		if( push_address == 0 ) { 
			fprintf(stderr, "SERIOUS FATAL ERROR: NWMovies: (movies) Magic end cookie not found.\n"); 
			fprintf(stderr, "SERIOUS FATAL ERROR: NWMovies: (movies)    Please contact David Holland (david.w.holland@gmail.com)\n"); 
			abort(); 
		}

		fprintf(stderr, "NOTICE: NWMovies: (movies) Cookie end_location: %p\n", (void *)push_address2); 
		calls[6] = push_address2; 
	} else { 
		calls[6] = 1; 
	}

/* Calls "loaded".  Correct into virtual addresses */

	calls[0] = calls[0] + code_header->sh_addr; 
	calls[1] = calls[1] + code_header->sh_addr; 
	calls[2] = calls[2] + code_header->sh_addr; 
	calls[3] = calls[3] + code_header->sh_addr; 
	calls[4] = calls[4] + code_header->sh_addr; 
	if( calls[5] != 1 ) { 		/*  Movies Button. */
		calls[5] = calls[5] + code_header->sh_addr; 
		calls[6] = calls[6] + code_header->sh_addr; 
	}

	fprintf(stderr, "NOTICE: NWMovies: (cookie) Recalculated calls 0: %08x\n", calls[0]); 
	fprintf(stderr, "NOTICE: NWMovies: (cookie) Recalculated calls 1: %08x\n", calls[1]); 
	fprintf(stderr, "NOTICE: NWMovies: (cookie) Recalculated calls 2: %08x\n", calls[2]); 
	fprintf(stderr, "NOTICE: NWMovies: (cookie) Recalculated calls 3: %08x\n", calls[3]); 
	fprintf(stderr, "NOTICE: NWMovies: (cookie) Recalculated calls 4: %08x\n", calls[4]); 
	fprintf(stderr, "NOTICE: NWMovies: (movies) Recalculated calls 5: %08x\n", calls[5]); 
	fprintf(stderr, "NOTICE: NWMovies: (movies) Recalculated calls 6: %08x\n", calls[6]); 
		
	elf_end(elf_ptr); 
	munmap(buffer, statbuf.st_size ); 
	close(fd); 
	dlclose(dlhandle); 
	return(calls); 
}

