#include "filesystem.h"
#include "util.h"

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <limits.h>

/* file_t info items */
#define FILE_T_OFFSET 0
#define DATA_SZ (SECTOR_SIZE - MAX_FILENAME - 1 - sizeof(char) - sizeof(int))

/* file header entry */
typedef struct {
    char name[MAX_FILENAME + 1];
    char size; /* prvy data segment*/
    int next; /* dalsi file */
    uint8_t data[DATA_SZ];
} file_header;

/* SPACE entry */
typedef struct {
    int size; /* kolko sectorov z sebou je volnych */
    int next;
} space_block;

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

/* uvolnim sector, ktory dany file zabera */
int free_sector(int addr) {
    space_block init = read_space(1);
    space_block new = {.next = init.next};
    init.next = addr;
    write_space(1, init);
    write_space(addr, new);
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
    file_header fh = {.name = "/", .size = 0, .next = -1};
    hdd_write(0, &fh);

    /* vytvorim a zapisem mantinely oznacujuce volne sectory */
    space_block init = {.size = 0, .next = 2};
    space_block middle = {.size = hdd_sz-3, .next = hdd_sz-1};
    space_block term = {.size = 0, .next = -1};

    hdd_write(1, &init);
    hdd_write(2, &middle);
    hdd_write(hdd_sz-1, &term);
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
        if (strrchr(path, PATHSEP) != path) return (file_t*)FAIL; 

	/* skontrolujeme, ci existuje subor s rovnakym menom */
        int act_addr = 0;
        int prev_addr = -1;
        file_header fh;
        do {
            /* nacitam aktualny sector */
            hdd_read(act_addr, &fh);
            prev_addr = act_addr;

            /* fprintf(stderr, "%s\n", fh.name); */
            /* ak subor uz existuje, skratim ho na 0 */
            if (strcmp(fh.name, &(path[1])) == 0) {
                /* fprintf(stderr, "%d\n", fh.size & (1 << 7)); */
                fh.size = 0;
                hdd_write(act_addr, &fh);
                file_t *fd;
                fd = fd_alloc();
                fd->info[0] = act_addr;
                return fd;
            }
            act_addr = fh.next;
        } while (act_addr != -1);

        /* subor neexistuje, alokujem novy sector pre tento file */
        int addr = allocate_sector();
        file_header new_file = {.size= 0, .next = -1};
        strcpy(new_file.name, &(path[1]));
        hdd_write(addr, &new_file);
        fh.next = addr;
        hdd_write(prev_addr, &fh);

        file_t *fd;
        fd = fd_alloc();
        fd->info[0] = addr;
        fd->info[1] = 0;
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
file_t *fs_open(const char *path) {
        if (strrchr(path, PATHSEP) != path) return (file_t*)FAIL; 

	/* skontrolujeme, ci existuje subor s rovnakym menom */
        int act_addr = 0;
        file_header fh;
        do {
            hdd_read(act_addr, &fh);
            /* fprintf(stderr, "%s\n", fh.name); */
            /* nasiel som hladany subor, otvorim ho  */
            if (strcmp(fh.name, &(path[1])) == 0) {
                /* fprintf(stderr, "%d\n", (int)fh.size & (1<<7)); */
                if ((fh.size & (1 << 7)) > 0) return (file_t*)FAIL;
                /* poznacim, ze je otvoreny */
                fh.size |= (1 << 7);
                hdd_write(act_addr, &fh);
                /* vratim handle na otvoreny subor */
                file_t *fd;
                fd = fd_alloc();
                fd->info[0] = act_addr;
                fd->info[1] = 0;
                fd->info[2] = -1;
                fd->info[3] = -1;
                return fd;
            }
            act_addr = fh.next;
        } while (act_addr != -1);

        return (file_t*)FAIL;
}

/**
 * Zatvori otvoreny file handle.
 *
 * Funkcia zatvori handle, ktory bol vytvoreny pomocou volania 'open' alebo
 * 'creat' a uvolni prostriedky, ktore su s nim spojene. V pripade akehokolvek
 * zlyhania vrati FAIL. Struktura file_t musi byt uvolnena jedine pomocou
 * fd_free.
 */
int fs_close(file_t *fd) {
        file_header fh;
        hdd_read(fd->info[0], &fh);
        if (fh.size & (1 << 7) == 0) return FAIL;
        fh.size &= ~(1 << 7);

        hdd_write(fd->info[0], &fh);
	fd_free(fd);
	return OK;
}

/**
 * Odstrani subor na ceste 'path'.
 *
 * Ak zadana cesta existuje a je to subor, odstrani subor z disku; nemeni
 * adresarovu strukturu. V pripade chyby vracia FAIL.
 */
int fs_unlink(const char *path) {
        if (strrchr(path, PATHSEP) != path) return FAIL; 

	/* skontrolujeme, ci existuje subor s danym menom */
        int act_addr = 0, prev_addr = -1;
        file_header fh, prev;
        do {
            hdd_read(act_addr, &fh);
            /* nasiel som subor, odstranim ho */
            if (strcmp(fh.name, &(path[1])) == 0) {
                prev.next = fh.next;
                hdd_write(prev_addr, &prev);
                free_sector(act_addr);
                return OK;
            }
            prev = fh;
            prev_addr = act_addr;
            act_addr = fh.next;
        } while (act_addr != -1);

        return FAIL;
}

/**
 * Premenuje/presunie polozku v suborovom systeme z 'oldpath' na 'newpath'.
 *
 * Po uspesnom vykonani tejto funkcie bude subor, ktory doteraz existoval na
 * 'oldpath' dostupny cez 'newpath' a 'oldpath' prestane existovat. Opat,
 * funkcia nemanipuluje s adresarovou strukturou (nevytvara nove adresare z cesty newpath okrem posledneho).
 * V pripade zlyhania vracia FAIL.
 */
int fs_rename(const char *oldpath, const char *newpath) {
    if (strrchr(oldpath, PATHSEP) != oldpath) return FAIL; 
    if (strrchr(newpath, PATHSEP) != newpath) return FAIL; 

    /* najdeme subor, ktory treba premenovat */
    int act_addr = 0;
    file_header fh;
    do {
        hdd_read(act_addr, &fh);
        /* nasiel som hladany subor, premenujem ho*/
        if (strcmp(fh.name, &(oldpath[1])) == 0) {
            strcpy(fh.name, &(newpath[1]));
            hdd_write(act_addr, &fh);
            return OK;
        }
        act_addr = fh.next;
    } while (act_addr != -1);

    return FAIL;
}

/**
 * Nacita z aktualnej pozicie vo 'fd' do bufferu 'bytes' najviac 'size' bajtov.
 *
 * Z aktualnej pozicie v subore precita funkcia najviac 'size' bajtov; na konci
 * suboru funkcia vracia 0. Po nacitani dat zodpovedajuco upravi poziciu v
 * subore. Vrati pocet precitanych bajtov z 'bytes', alebo FAIL v pripade
 * zlyhania. Existujuci subor prepise.
 */
int fs_read(file_t *fd, uint8_t *bytes, unsigned int size) {
    file_header fh;
    hdd_read(fd->info[0], &fh);
    int pos = fd->info[1];
    /* ak subor nie je otvoreny */
    if (fh.size & (1 << 7) == 0) return FAIL;
    fh.size &= ~(1 << 7);
    if (pos + size > fh.size) size = fh.size - pos;
    memcpy(bytes, &fh.data + fd->info[1], size);
    fd->info[1] += size;
    return size;
}

/**
 * Zapise do 'fd' na aktualnu poziciu 'size' bajtov z 'bytes'.
 *
 * Na aktualnu poziciu v subore zapise 'size' bajtov z 'bytes'. Ak zapis
 * presahuje hranice suboru, subor sa zvacsi; ak to nie je mozne, zapise sa
 * maximalny mozny pocet bajtov. Po zapise korektne upravi aktualnu poziciu v
 * subore a vracia pocet zapisanych bajtov z 'bytes'.
 */

int fs_write(file_t *fd, const uint8_t *bytes, unsigned int size) {
    file_header fh;
    hdd_read(fd->info[0], &fh);
    int pos = fd->info[1];
    /* ak subor nie je otvoreny */
    if (fh.size & (1 << 7) == 0) return FAIL;
    fh.size &= ~(1 << 7);
    if (pos + size >= DATA_SZ) size = DATA_SZ - pos;
    /* fprintf(stderr, "DATA_SZ: %d\n size:%u pos:%d\n", DATA_SZ,  size, pos); */
    /* zapisem a updatnem size suboru */
    memcpy(&fh.data[pos], bytes, size);
    if (pos + size > fh.size) fh.size = pos + size;
    fh.size |= (1 << 7);
    hdd_write(fd->info[0], &fh);
    fd->info[1] += size;
    return size;
}

/**
 * Zmeni aktualnu poziciu v subore na 'pos'-ty byte.
 *
 * Upravi aktualnu poziciu; ak je 'pos' mimo hranic suboru, vrati FAIL a pozicia
 * sa nezmeni, inac vracia OK.
 */


int fs_seek(file_t *fd, unsigned int pos) {
    if (pos >= SECTOR_SIZE - sizeof(file_header)) return FAIL;
    fd->info[1] = pos;
    return OK;
}


/**
 * Vrati aktualnu poziciu v subore.
 */

unsigned int fs_tell(file_t *fd) {
	return fd->info[1];
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
    if (strrchr(path, PATHSEP) != path) return FAIL; 

    file_header fh;
    int act_addr = 0;
    do {
        hdd_read(act_addr, &fh);
        if (strcmp(fh.name, &(path[1])) == 0) {
            fs_stat->st_size = fh.size & ~(1 << 7);
            fs_stat->st_nlink = 1;
            fs_stat->st_type = ST_TYPE_FILE;
            return OK;
        }
        act_addr = fh.next;
    } while (act_addr != -1);
    return FAIL; 
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

