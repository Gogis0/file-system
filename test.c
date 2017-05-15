#include "util.h"
#include "filesystem.h"

int main(void)
{
	file_t *fd;
	util_init("disk.bin", 65536);
	fs_format();

	fd = fs_creat("/test.txt");
	fs_write(fd,
        "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!",
        50);
        fs_write(fd, "aaaaaaaaa", 78);
	fs_close(fd);

	util_end();
}
