#define _GNU_SOURCE		/* Needed so dlfcn.h defines the right stuff */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include <dlfcn.h>
#include <errno.h>

#include <sys/mman.h>
#include <limits.h>

#include <elf.h>
#include <libelf.h>

#include <link.h>

#ifndef PAGESIZE
#define PAGESIZE 4096
#endif

#define NWM_PROTECTED 1
#define NWM_UNPROTECTED 0

void *NWMovies_lookup_symbol(char *filename, char *function)
{
	Elf		*elf_ptr; 
	int		fd; 
	Elf32_Ehdr	*ehdr; 
	Elf_Scn		*section; 
	Elf32_Shdr	*section_header;
	Elf32_Sym	*symtab_start, *symtab_current; 
	int		symtab_count, i; 
	char		*strtab; 
	int		strtab_type; 

	char		*symstrtab; 
	void		*return_val; 
		
/* Initialize the elves. */

	if (elf_version(EV_CURRENT) == EV_NONE) {
		fprintf(stderr, "ERROR: NWMovies: libelf version mismatch.\n"); 
		abort(); 
	}

/* open shared library */ 	

	fd = open(filename, O_RDONLY); 
	if( fd < 0 ) { 
		fprintf(stderr, "ERROR: NWMovies: Unable to open shared library: %s (%d)\n", filename, errno); 
		abort(); 
	}
	elf_ptr = elf_begin(fd, ELF_C_READ, (Elf *)0);
	if( elf_ptr == NULL) { 
		fprintf(stderr, "ERROR: NWMovies: elf_begin failed: %s.\n", elf_errmsg(elf_errno())); 
		abort(); 
	} 

	/* Get the Header */
	if ( (ehdr = elf32_getehdr(elf_ptr)) == NULL) { 
		fprintf(stderr, "ERROR: NWMovies: Unable to get Elf header: %s\n",  elf_errmsg(elf_errno()) ); 
		abort(); 
	}
	/* Find the section string table */
	section = elf_getscn(elf_ptr, ehdr->e_shstrndx); 
	symstrtab = elf_getdata(section, NULL)->d_buf; 

	section = 0; 
	symtab_start = NULL; 
	strtab = NULL; 
	strtab_type = -1; 
	while( (section = elf_nextscn( elf_ptr, section )) ) { 
		section_header = elf32_getshdr(section); 
		/* DYNSYM is better than nothing */
		if( symtab_start == NULL && section_header->sh_type == SHT_DYNSYM ) { 
			symtab_start = elf_getdata(section, NULL)->d_buf; 
			symtab_count = section_header->sh_size / section_header->sh_entsize;
			strtab_type = 0; 
		} 
		/* However, we prefer SYMTAB */ 
		if(  section_header->sh_type == SHT_SYMTAB ) {
			symtab_start = elf_getdata(section, NULL)->d_buf;
			symtab_count = section_header->sh_size / section_header->sh_entsize;
			strtab_type = 1; 
		}
	}

	if( strtab_type == -1 ) {
		fprintf(stderr, "ERROR: NWMovies: didn't find any symbol tables. Positively won't work.\n");
		fprintf(stderr, "ERROR: NWMovies: Try a different %s library\n", filename); 
		abort(); 
	}

	section = 0; 
	while((section = elf_nextscn(elf_ptr, section))) { 
		section_header = elf32_getshdr(section); 
		if( section_header->sh_type == SHT_STRTAB ) { 
			if( strtab_type == 0 && strcmp(section_header->sh_name + symstrtab, ".dynstr") == 0 ) { 
				strtab = elf_getdata(section, NULL)->d_buf; 
				break; 
			}
			if( strtab_type == 1 && strcmp(section_header->sh_name + symstrtab, ".strtab") == 0 ) { 
				strtab = elf_getdata(section, NULL)->d_buf; 
				break;
			}
		}
	}
	symtab_current = symtab_start; 
	for(i=0; i<symtab_count; i++) { 
		// fprintf(stderr, "DEBUG: INDEX: %d: %d\n", i, symtab_current->st_name); 
		if( symtab_current->st_name != 0 ) { 
			// fprintf(stderr, "DEBUG: Testing: %s\n", symtab_current->st_name + strtab); 
			if( ! strcmp(symtab_current->st_name+strtab,function) ) { 
				break; 
			}
		}
		symtab_current++;
	}
	if( i >= symtab_count ) {
			elf_end(elf_ptr); 
			close(fd); 
			return(NULL); 
	} else { 
		return_val = (void *)symtab_current->st_value; 
		elf_end(elf_ptr); 
		close(fd); 
		return(return_val); 
	}
}
