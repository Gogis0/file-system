#ifndef WRAPPER_H
#define WRAPPER_H

#include "filesystem.h"

unsigned int hdd_size();
void hdd_read(unsigned int sector, void *buffer);
void hdd_write(unsigned int sector, void *buffer);

file_t *fd_alloc();
void fd_free(file_t *fd);

/* Nasledujuce API je pre wrapper, nepouzivajte ho */

void util_reset_counters();
unsigned long util_get_reads();
unsigned long util_get_writes();
	
int util_init(const char *disk_path, unsigned int my_size);
void util_end();

#endif
