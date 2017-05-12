#include "filesystem.h"
#include "util.h"

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <limits.h>

/* file_t info items */
#define FILE_T_OFFSET 0

int free_space_start = -1;

typedef struct {
    char name[MAX_FILENAME];
    unsigned int size;
} zero_sector_t;

/* file header entry */
typedef struct {
    char name[MAX_FILENAME];
    int first_data_sector; /* prvy data segment*/
    int next; /* dalsi file */
} file_header;

/* DATA entry */
typedef struct {
    int size;
    int next;
} data_block;

/* SPACE entry */
typedef struct {
    int size; /* kolko sectorov z sebou je volnych */
    int next;
} space_block;


/* nacitam hlavicku zo segmentu na pozicii 'pos' a vratim ju */
file_header read_header(int pos) {
    uint8_t buffer[SECTOR_SIZE];
    file_header fh;
    hdd_read(pos, buffer);
    memcpy(&fh, buffer, sizeof(file_header));
    return fh;
}


/* zapisem file_header do nacitaneho sectoru v pamati */
void write_header(int pos, file_header fh) {
    uint8_t buffer[SECTOR_SIZE];
    memcpy(buffer, &fh, sizeof(file_header));
    hdd_write(pos, buffer);
}

space_block read_space(int idx) {
    uint8_t buffer[SECTOR_SIZE];
    hdd_read(idx, buffer);
    space_block sb;
    memcpy(&sb, buffer, sizeof(space_block));
    return sb;
}

void write_space(int idx, space_block sb) {
    uint8_t buffer[SECTOR_SIZE];
    memcpy(buffer, &sb, sizeof(space_block));
    hdd_write(idx, buffer);
}

/* alokujem novy sector */
int allocate_sector() {
    space_block init = read_space(1);
    space_block act = read_space(init.next);

    if (act.size == 1) {
        init.next = act.next;
        write_space(1, init);
        return init.next;
    }
    act.size--;
    write_space(init.next, act);
    return init.next + act.size;
}

/**
 * Naformatovanie disku.
 *
 * Zavola sa vzdy, ked sa vytvara novy obraz disku. Mozete predpokladat, ze je
 * cely vynulovany.
 */

void fs_format() {
    int hdd_sz = hdd_size() / SECTOR_SIZE;
    uint8_t buffer[SECTOR_SIZE];

    /* vytvorim inicializacny subor na disku */
    file_header fh = {.name = "/", .first_data_sector = -1, .next = -1};
    write_header(0, fh);

    /* vytvorim a zapisem mantinely oznacujuce volne sectory */
    space_block init = {.size = 0, .next = 2};
    space_block middle = {.size = hdd_sz-3, .next = hdd_sz-1};
    space_block term = {.size = 0, .next = -1};

    memcpy(buffer, &init, sizeof(space_block));
    hdd_write(1, buffer);
    memcpy(buffer, &middle, sizeof(space_block));
    hdd_write(2, buffer);
    memcpy(buffer, &term, sizeof(space_block));
    hdd_write(hdd_sz-1, buffer);
}

/**
 * Vytvorenie suboru.
 *
 * Volanie vytvori v suborovom systeme na zadanej ceste novy subor a vrati
 * handle nan. Ak subor uz existoval, bude skrateny na 0. Pozicia v subore bude
 * nastavena na 0ty byte. Ak adresar, v ktorom subor ma byt ulozeny neexistuje,
 * vrati FAIL (sam nevytvara adresarovu strukturu, moze vytvarat iba subory).
 */

file_t *fs_creat(const char *path) {
        int header_sz = sizeof(file_header);
        char name[MAX_FILENAME];
        for (int i = 1; i < strlen(path); i++) {
            name[i - 1] = path[i];
	    /* adresare zahadzujem :D */
            if (path[i] == '/') return (file_t*)FAIL;
        }

        if (strrchr(path, PATHSEP) != path) return (file_t*) FAIL;

	/* skontrolujeme, ci existuje subor s rovnakym menom */
	uint8_t buffer[SECTOR_SIZE];
        int next_sector = 0;
        int prev_sector = -1;
        file_header fh;
        do {
            /* nacitam aktualny sector */
            hdd_read(next_sector, buffer);
            memcpy(&fh, buffer, header_sz);
            prev_sector = next_sector;
            next_sector = fh.next;
            fprintf(stderr, "%d\n", next_sector);
            /* ak subor uz existuje, skratim ho na 0 */
            if (strcmp(fh.name, path) == 0) return (file_t*)FAIL;
        } while (next_sector != -1);

        /* alokujem novy sector pre tento file */
        int addr = allocate_sector();
        fprintf(stderr, "%d\n", addr);
        file_header new_file = {.name = *name, .first_data_sector = -1, .next = -1};
        write_header(addr, new_file);
        fh.next = addr;
        write_header(prev_sector, fh);

        file_t *fd;
        fd = fd_alloc();
        fd->info[0] = addr;
        fd->info[1] = -1;
        fd->info[2] = -1;
        fd->info[3] = -1;
        return fd;
}

/**
 * Otvorenie existujuceho suboru.
 *
 * Ak zadany subor existuje funkcia ho otvori a vrati handle nan. Pozicia v
 * subore bude nastavena na 0ty bajt. Ak subor neexistuje, vrati FAIL. Struktura
 * file_t sa musi alokovat jedine pomocou fd_alloc.
 */
file_t *fs_open(const char *path)
{
	uint8_t buffer[SECTOR_SIZE];
	file_t *fd;

	hdd_read(0, &buffer);

	/* Skontrolujeme, ci v prvom sektore je ulozene meno nasho suboru */
	if (memcmp(buffer, path, strnlen(path,12)))
			return (file_t*)FAIL;

	/* Subor existuje, alokujeme pren deskriptor */
	fd = fd_alloc();

	/* mame iba jeden jediny subor, deskriptor vyplnime samymi nulami */
	fd->info[FILE_T_OFFSET] = 0;
	fd->info[1] = 0;
	fd->info[2] = 0;
	fd->info[3] = 0;

	return fd;
}

/**
 * Zatvori otvoreny file handle.
 *
 * Funkcia zatvori handle, ktory bol vytvoreny pomocou volania 'open' alebo
 * 'creat' a uvolni prostriedky, ktore su s nim spojene. V pripade akehokolvek
 * zlyhania vrati FAIL. Struktura file_t musi byt uvolnena jedine pomocou
 * fd_free.
 */
int fs_close(file_t *fd)
{
	/* Uvolnime filedescriptor, aby sme neleakovali pamat */
	fd_free(fd);
	return OK;
}

/**
 * Odstrani subor na ceste 'path'.
 *
 * Ak zadana cesta existuje a je to subor, odstrani subor z disku; nemeni
 * adresarovu strukturu. V pripade chyby vracia FAIL.
 */
int fs_unlink(const char *path) { return FAIL;};

/**
 * Premenuje/presunie polozku v suborovom systeme z 'oldpath' na 'newpath'.
 *
 * Po uspesnom vykonani tejto funkcie bude subor, ktory doteraz existoval na
 * 'oldpath' dostupny cez 'newpath' a 'oldpath' prestane existovat. Opat,
 * funkcia nemanipuluje s adresarovou strukturou (nevytvara nove adresare z cesty newpath okrem posledneho).
 * V pripade zlyhania vracia FAIL.
 */
int fs_rename(const char *oldpath, const char *newpath) { return FAIL; };

/**
 * Nacita z aktualnej pozicie vo 'fd' do bufferu 'bytes' najviac 'size' bajtov.
 *
 * Z aktualnej pozicie v subore precita funkcia najviac 'size' bajtov; na konci
 * suboru funkcia vracia 0. Po nacitani dat zodpovedajuco upravi poziciu v
 * subore. Vrati pocet precitanych bajtov z 'bytes', alebo FAIL v pripade
 * zlyhania. Existujuci subor prepise.
 */
int fs_read(file_t *fd, uint8_t *bytes, unsigned int size)
{
	/* Podporujeme iba subory s maximalnou velkostou SECTOR_SIZE */
	uint8_t buffer[SECTOR_SIZE] = { 0 };
	/* Vo filedescriptore je ulozena nasa aktualna pozicia v subore */
	int offset = fd->info[FILE_T_OFFSET];
	int file_size;

	/* Nacitame celkovu velkost suboru na disku */
	hdd_read(0, buffer);
	file_size = ((zero_sector_t*)buffer)->size;

	hdd_read(1, buffer);
	int i;
	for (i = 0; (i < size) && ((i + offset) < file_size); i++) {
		bytes[i] = buffer[offset + i];
	}	

	/* Aktualizujeme offset, na ktorom sme teraz */
	fd->info[FILE_T_OFFSET] += i;

	/* Vratime pocet precitanych bajtov */
	return i;
}

/**
 * Zapise do 'fd' na aktualnu poziciu 'size' bajtov z 'bytes'.
 *
 * Na aktualnu poziciu v subore zapise 'size' bajtov z 'bytes'. Ak zapis
 * presahuje hranice suboru, subor sa zvacsi; ak to nie je mozne, zapise sa
 * maximalny mozny pocet bajtov. Po zapise korektne upravi aktualnu poziciu v
 * subore a vracia pocet zapisanych bajtov z 'bytes'.
 */

int fs_write(file_t *fd, const uint8_t *bytes, unsigned int size)
{
	uint8_t buffer[SECTOR_SIZE] = { 0 };
	/* Vo filedescriptore je ulozena nasa aktualna pozicia v subore */
	int offset = fd->info[FILE_T_OFFSET];
	int file_size;

	/* Nacitame celkovu velkost suboru na disku */
	hdd_read(0, buffer);
	file_size = ((zero_sector_t*)buffer)->size;

	/* Nacitame stare data do buffera a prepiseme ich novymi */
	hdd_read(1, buffer);
	int i;
	for (i = 0; (i < size) && ((i + offset) < SECTOR_SIZE); i++) {
		buffer[offset + i] = bytes[i];
	}
	hdd_write(1, buffer);

	/* Ak subor narastol, aktualizujeme velkost */

	if (file_size < offset + i) {
		hdd_read(0, buffer);
		((zero_sector_t*)buffer)->size = offset + i;
		hdd_write(0, buffer);
	}

	/* Aktualizujeme offset, na ktorom sme */
	fd->info[FILE_T_OFFSET] += i;

	/* Vratime pocet precitanych bajtov */
	return i;
}

/**
 * Zmeni aktualnu poziciu v subore na 'pos'-ty byte.
 *
 * Upravi aktualnu poziciu; ak je 'pos' mimo hranic suboru, vrati FAIL a pozicia
 * sa nezmeni, inac vracia OK.
 */


int fs_seek(file_t *fd, unsigned int pos)
{
	uint8_t buffer[SECTOR_SIZE] = { 0 };
	int file_size;

	/* Nacitaj velkost suboru z disku */
	hdd_read(0, buffer);
	file_size = ((zero_sector_t*)buffer)->size;

	/* Nemozeme seekovat za velkost suboru */
	if (pos > file_size) {
		fprintf(stderr, "Can not seek: %d > %d\n", pos, file_size);
		return FAIL;
	}

	fd->info[FILE_T_OFFSET] = pos;

	return OK;
}


/**
 * Vrati aktualnu poziciu v subore.
 */

unsigned int fs_tell(file_t *fd) {
	return fd->info[FILE_T_OFFSET];
}


/**
 * Vrati informacie o 'path'.
 *
 * Funkcia vrati FAIL ak cesta neexistuje, alebo vyplni v strukture 'fs_stat'
 * polozky a vrati OK:
 *  - st_size: velkost suboru v byte-och
 *  - st_nlink: pocet hardlinkov na subor (ak neimplementujete hardlinky, tak 1)
 *  - st_type: hodnota podla makier v hlavickovom subore: ST_TYPE_FILE,
 *  ST_TYPE_DIR, ST_TYPE_SYMLINK
 *
 */

int fs_stat(const char *path, struct fs_stat *fs_stat) { 
	uint8_t buffer[SECTOR_SIZE] = { 0 };
	int file_size;
	
	/* Nacitaj velkost suboru z disku */
	hdd_read(0, buffer);

	/* Ak subor neexistuje, FAIL */
	if (buffer[0] == 0)
		return FAIL;

	file_size = ((zero_sector_t*)buffer)->size;
	fs_stat->st_size = file_size;
	fs_stat->st_nlink = 1;
	fs_stat->st_type = ST_TYPE_FILE;

	return OK; 
};

/* Level 3 */
/**
 * Vytvori adresar 'path'.
 *
 * Ak cesta, v ktorej adresar ma byt, neexistuje, vrati FAIL (vytvara najviac
 * jeden adresar), pri korektnom vytvoreni OK.
 */
int fs_mkdir(const char *path) { return FAIL; };

/**
 * Odstrani adresar 'path'.
 *
 * Odstrani adresar, na ktory ukazuje 'path'; ak neexistuje alebo nie je
 * adresar, vrati FAIL; po uspesnom dokonceni vrati OK.
 */
int fs_rmdir(const char *path) { return FAIL; };

/**
 * Otvori adresar 'path' (na citanie poloziek)
 *
 * Vrati handle na otvoreny adresar s poziciou nastavenou na 0; alebo FAIL v
 * pripade zlyhania.
 */
file_t *fs_opendir(const char *path) { return (file_t*)FAIL; };

/**
 * Nacita nazov dalsej polozky z adresara.
 *
 * Do dodaneho buffera ulozi nazov polozky v adresari a posunie aktualnu
 * poziciu na dalsiu polozku. V pripade problemu, alebo nemoznosti precitat
 * polozku (dalsia neexistuje) vracia FAIL.
 */
int fs_readdir(file_t *dir, char *item) {return FAIL; };

/** 
 * Zatvori otvoreny adresar.
 */
int fs_closedir(file_t *dir) { return FAIL; };

/* Level 4 */
/**
 * Vytvori hardlink zo suboru 'path' na 'linkpath'.
 */
int fs_link(const char *path, const char *linkpath) { return FAIL; };

/**
 * Vytvori symlink z 'path' na 'linkpath'.
 */
int fs_symlink(const char *path, const char *linkpath) { return FAIL; };

