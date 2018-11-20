#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#define FILEPATH "/root/Dropbox/lo_rubystrano.scap"
#define NUMCHARS  (44843008)
#define FILESIZE (NUMCHARS)

int main(int argc, char *argv[])
{
	int i;
	int fd;
	char *map;  /* mmapped array of int's */
	int a;

	fd = open(FILEPATH, O_RDONLY);

	if (fd == -1)
	{
		perror("Error opening file for reading");
		exit(EXIT_FAILURE);
	}

	map = mmap(0, FILESIZE, PROT_READ, MAP_SHARED, fd, 0);

	if (map == MAP_FAILED)
	{
		close(fd);
		perror("Error mmapping the file");
		exit(EXIT_FAILURE);
	}

	/* Read the file int-by-int from the mmap
	 */
	for (i = 1; i <=NUMCHARS; ++i)
	{
//if(i % 100 == 0)
//	printf("%d ", i);
		a = (int)map[i];
	}

	if (munmap(map, FILESIZE) == -1)
	{
		perror("Error un-mmapping the file");
	}
	sleep(1);
	close(fd);
	return 0;
}
