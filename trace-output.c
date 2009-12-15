#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>

#include "trace-cmd.h"

struct tracecmd_output {
	int		fd;
	int		page_size;
	int		cpus;
	char		*tracing_dir;
};

static int do_write(struct tracecmd_output *handle, void *data, int size)
{
	int tot = 0;
	int w;

	do {
		w = write(handle->fd, data, size - tot);
		tot += w;

		if (!w)
			break;
		if (w < 0)
			return w;
	} while (tot != size);

	return tot;
}

static int
do_write_check(struct tracecmd_output *handle, void *data, int size)
{
	int ret;

	ret = do_write(handle, data, size);
	if (ret < 0)
		return ret;
	if (ret != size)
		return -1;

	return 0;
}

void tracecmd_output_close(struct tracecmd_output *handle)
{
	if (!handle)
		return;

	if (handle->fd >= 0) {
		close(handle->fd);
		handle->fd = -1;
	}

	if (handle->tracing_dir)
		free(handle->tracing_dir);

	free(handle);
}

static unsigned long get_size_fd(int fd)
{
	unsigned long long size = 0;
	char buf[BUFSIZ];
	int r;

	do {
		r = read(fd, buf, BUFSIZ);
		if (r > 0)
			size += r;
	} while (r > 0);

	lseek(fd, 0, SEEK_SET);

	return size;
}

static unsigned long get_size(const char *file)
{
	unsigned long long size = 0;
	int fd;

	fd = open(file, O_RDONLY);
	if (fd < 0)
		die("Can't read '%s'", file);
	size = get_size_fd(fd);
	close(fd);

	return size;
}

static unsigned long long copy_file_fd(struct tracecmd_output *handle, int fd)
{
	unsigned long long size = 0;
	char buf[BUFSIZ];
	int r;

	do {
		r = read(fd, buf, BUFSIZ);
		if (r > 0) {
			size += r;
			if (do_write_check(handle, buf, r))
				return 0;
		}
	} while (r > 0);

	return size;
}

static unsigned long long copy_file(struct tracecmd_output *handle,
				    const char *file)
{
	unsigned long long size = 0;
	int fd;

	fd = open(file, O_RDONLY);
	if (fd < 0)
		die("Can't read '%s'", file);
	size = copy_file_fd(handle, fd);
	close(fd);

	return size;
}

/*
 * Finds the path to the debugfs/tracing
 * Allocates the string and stores it.
 */
static const char *find_tracing_dir(struct tracecmd_output *handle)
{
	if (!handle->tracing_dir)
		handle->tracing_dir = tracecmd_find_tracing_dir();

	return handle->tracing_dir;
}

static char *get_tracing_file(struct tracecmd_output *handle, const char *name)
{
	const char *tracing;
	char *file;

	tracing = find_tracing_dir(handle);
	if (!tracing)
		return NULL;

	file = malloc_or_die(strlen(tracing) + strlen(name) + 2);
	if (!file)
		return NULL;

	sprintf(file, "%s/%s", tracing, name);
	return file;
}

static void put_tracing_file(char *file)
{
	free(file);
}

int tracecmd_ftrace_enable(int set)
{
	struct stat buf;
	char *path = "/proc/sys/kernel/ftrace_enabled";
	int fd;
	char *val = set ? "1" : "0";
	int ret = 0;

	/* if ftace_enable does not exist, simply ignore it */
	fd = stat(path, &buf);
	if (fd < 0)
		return ENODEV;

	fd = open(path, O_WRONLY);
	if (fd < 0)
		die ("Can't %s ftrace", set ? "enable" : "disable");

	if (write(fd, val, 1) < 0)
		ret = -1;
	close(fd);

	return ret;
}

static int read_header_files(struct tracecmd_output *handle)
{
	unsigned long long size, check_size;
	struct stat st;
	char *path;
	int fd;
	int ret;

	path = get_tracing_file(handle, "events/header_page");
	if (!path)
		return -1;

	ret = stat(path, &st);
	if (ret < 0) {
		/* old style did not show this info, just add zero */
		put_tracing_file(path);
		if (do_write_check(handle, "header_page", 12))
			return -1;
		size = 0;
		if (do_write_check(handle, &size, 8))
			return -1;
		if (do_write_check(handle, "header_event", 13))
			return -1;
		if (do_write_check(handle, &size, 8))
			return -1;
		return 0;
	}

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		warning("can't read '%s'", path);
		return -1;
	}

	/* unfortunately, you can not stat debugfs files for size */
	size = get_size_fd(fd);

	if (do_write_check(handle, "header_page", 12))
		goto out_close;
	if (do_write_check(handle, &size, 8))
		goto out_close;
	check_size = copy_file_fd(handle, fd);
	close(fd);
	if (size != check_size) {
		warning("wrong size for '%s' size=%lld read=%lld",
			path, size, check_size);
		errno = EINVAL;
		return -1;
	}
	put_tracing_file(path);

	path = get_tracing_file(handle, "events/header_event");
	if (!path)
		return -1;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		die("can't read '%s'", path);

	size = get_size_fd(fd);

	if (do_write_check(handle, "header_event", 13))
		goto out_close;
	if (do_write_check(handle, &size, 8))
		goto out_close;
	check_size = copy_file_fd(handle, fd);
	close(fd);
	if (size != check_size) {
		warning("wrong size for '%s'", path);
		return -1;
	}
	put_tracing_file(path);
	return 0;

 out_close:
	close(fd);
	return -1;
}

static int copy_event_system(struct tracecmd_output *handle, const char *sys)
{
	unsigned long long size, check_size;
	struct dirent *dent;
	struct stat st;
	char *format;
	DIR *dir;
	int count = 0;
	int ret;

	dir = opendir(sys);
	if (!dir) {
		warning("can't read directory '%s'", sys);
		return -1;
	}

	while ((dent = readdir(dir))) {
		if (strcmp(dent->d_name, ".") == 0 ||
		    strcmp(dent->d_name, "..") == 0)
			continue;
		format = malloc_or_die(strlen(sys) + strlen(dent->d_name) + 10);
		if (!format)
			return -1;
		sprintf(format, "%s/%s/format", sys, dent->d_name);
		ret = stat(format, &st);
		free(format);
		if (ret < 0)
			continue;
		count++;
	}

	if (do_write_check(handle, &count, 4))
		return -1;
	
	rewinddir(dir);
	while ((dent = readdir(dir))) {
		if (strcmp(dent->d_name, ".") == 0 ||
		    strcmp(dent->d_name, "..") == 0)
			continue;
		format = malloc_or_die(strlen(sys) + strlen(dent->d_name) + 10);
		if (!format)
			return -1;
		sprintf(format, "%s/%s/format", sys, dent->d_name);
		ret = stat(format, &st);

		if (ret >= 0) {
			/* unfortunately, you can not stat debugfs files for size */
			size = get_size(format);
			if (do_write_check(handle, &size, 8))
				goto out_free;
			check_size = copy_file(handle, format);
			if (size != check_size) {
				warning("error in size of file '%s'", format);
				goto out_free;
			}
		}

		free(format);
	}

	return 0;

 out_free:
	free(format);
	return -1;
}

static int read_ftrace_files(struct tracecmd_output *handle)
{
	char *path;
	int ret;

	path = get_tracing_file(handle, "events/ftrace");
	if (!path)
		return -1;

	ret = copy_event_system(handle, path);

	put_tracing_file(path);

	return ret;
}

static int read_event_files(struct tracecmd_output *handle)
{
	struct dirent *dent;
	struct stat st;
	char *path;
	char *sys;
	DIR *dir;
	int count = 0;
	int ret;

	path = get_tracing_file(handle, "events");
	if (!path)
		return -1;

	dir = opendir(path);
	if (!dir)
		die("can't read directory '%s'", path);

	while ((dent = readdir(dir))) {
		if (strcmp(dent->d_name, ".") == 0 ||
		    strcmp(dent->d_name, "..") == 0 ||
		    strcmp(dent->d_name, "ftrace") == 0)
			continue;
		ret = -1;
		sys = malloc_or_die(strlen(path) + strlen(dent->d_name) + 2);
		if (!sys)
			goto out_close_dir;
		sprintf(sys, "%s/%s", path, dent->d_name);
		ret = stat(sys, &st);
		free(sys);
		if (ret < 0)
			continue;
		if (S_ISDIR(st.st_mode))
			count++;
	}

	ret = -1;
	if (do_write_check(handle, &count, 4))
		goto out_close_dir;

	rewinddir(dir);
	while ((dent = readdir(dir))) {
		if (strcmp(dent->d_name, ".") == 0 ||
		    strcmp(dent->d_name, "..") == 0 ||
		    strcmp(dent->d_name, "ftrace") == 0)
			continue;
		ret = -1;
		sys = malloc_or_die(strlen(path) + strlen(dent->d_name) + 2);
		if (!sys)
			goto out_close_dir;

		sprintf(sys, "%s/%s", path, dent->d_name);
		ret = stat(sys, &st);
		if (ret >= 0) {
			if (S_ISDIR(st.st_mode)) {
				if (do_write_check(handle, dent->d_name,
						   strlen(dent->d_name) + 1)) {
					free(sys);
					ret = -1;
					goto out_close_dir;
				}
				copy_event_system(handle, sys);
			}
		}
		free(sys);
	}

	put_tracing_file(path);

	ret = 0;

 out_close_dir:
	closedir(dir);
	return ret;
}

static int read_proc_kallsyms(struct tracecmd_output *handle)
{
	unsigned int size, check_size;
	const char *path = "/proc/kallsyms";
	struct stat st;
	int ret;

	ret = stat(path, &st);
	if (ret < 0) {
		/* not found */
		size = 0;
		if (do_write_check(handle, &size, 4))
			return -1;
		return 0;
	}
	size = get_size(path);
	if (do_write_check(handle, &size, 4))
		return -1;
	check_size = copy_file(handle, path);
	if (size != check_size) {
		errno = EINVAL;
		warning("error in size of file '%s'", path);
		return -1;
	}

	return 0;
}

static int read_ftrace_printk(struct tracecmd_output *handle)
{
	unsigned int size, check_size;
	const char *path;
	struct stat st;
	int ret;

	path = get_tracing_file(handle, "printk_formats");
	if (!path)
		return -1;

	ret = stat(path, &st);
	if (ret < 0) {
		/* not found */
		size = 0;
		if (do_write_check(handle, &size, 4))
			return -1;
		return 0;
	}
	size = get_size(path);
	if (do_write_check(handle, &size, 4))
		return -1;
	check_size = copy_file(handle, path);
	if (size != check_size) {
		errno = EINVAL;
		warning("error in size of file '%s'", path);
		return -1;
	}

	return 0;
}

static struct tracecmd_output *create_file(const char *output_file, int cpus)
{
	struct tracecmd_output *handle;
	char buf[BUFSIZ];
	char *file = NULL;
	struct stat st;
	off64_t check_size;
	off64_t size;
	int ret;

	handle = malloc(sizeof(*handle));
	if (!handle)
		return NULL;
	memset(handle, 0, sizeof(*handle));

	handle->fd = open(output_file, O_RDWR | O_CREAT | O_TRUNC | O_LARGEFILE, 0644);
	if (handle->fd < 0)
		goto out_free;

	buf[0] = 23;
	buf[1] = 8;
	buf[2] = 68;
	memcpy(buf + 3, "tracing", 7);

	if (do_write_check(handle, buf, 10))
		goto out_free;

	if (do_write_check(handle, TRACECMD_VERSION, strlen(TRACECMD_VERSION) + 1))
		goto out_free;

	/* save endian */
	if (tracecmd_host_bigendian())
		buf[0] = 1;
	else
		buf[0] = 0;

	if (do_write_check(handle, buf, 1))
		goto out_free;

	/* save size of long (this may not be what the kernel is) */
	buf[0] = sizeof(long);
	if (do_write_check(handle, buf, 1))
		goto out_free;

	/* save page_size */
	handle->page_size = getpagesize();
	if (do_write_check(handle, &handle->page_size, 4))
		goto out_free;

	if (read_header_files(handle))
		goto out_free;
	if (read_ftrace_files(handle))
		goto out_free;
	if (read_event_files(handle))
		goto out_free;
	if (read_proc_kallsyms(handle))
		goto out_free;
	if (read_ftrace_printk(handle))
		goto out_free;

	/*
	 * Save the command lines;
	 */
	file = get_tracing_file(handle, "saved_cmdlines");
	ret = stat(file, &st);
	if (ret >= 0) {
		size = get_size(file);
		if (do_write_check(handle, &size, 8))
			goto out_free;
		check_size = copy_file(handle, file);
		if (size != check_size) {
			errno = EINVAL;
			warning("error in size of file '%s'", file);
			goto out_free;
		}
	} else {
		size = 0;
		if (do_write_check(handle, &size, 8))
			goto out_free;
	}
	put_tracing_file(file);
	file = NULL;

	if (do_write_check(handle, &cpus, 4))
		goto out_free;

	return handle;

 out_free:
	tracecmd_output_close(handle);
	return NULL;
}

struct tracecmd_output *tracecmd_create_file_latency(const char *output_file, int cpus)
{
	struct tracecmd_output *handle;
	char *path;

	handle = create_file(output_file, cpus);
	if (!handle)
		return NULL;

	if (do_write_check(handle, "latency  ", 10))
		goto out_free;

	path = get_tracing_file(handle, "trace");
	if (!path)
		goto out_free;

	copy_file(handle, path);

	put_tracing_file(path);

	return handle;

out_free:
	tracecmd_output_close(handle);
	return NULL;
}

struct tracecmd_output *tracecmd_create_file(const char *output_file,
					     int cpus, char * const *cpu_data_files)
{
	unsigned long long *offsets = NULL;
	unsigned long long *sizes = NULL;
	struct tracecmd_output *handle;
	unsigned long long offset;
	off64_t check_size;
	char *file = NULL;
	struct stat st;
	int ret;
	int i;

	handle = create_file(output_file, cpus);
	if (!handle)
		return NULL;

	if (do_write_check(handle, "flyrecord", 10))
		goto out_free;

	offsets = malloc_or_die(sizeof(*offsets) * cpus);
	if (!offsets)
		goto out_free;
	sizes = malloc_or_die(sizeof(*sizes) * cpus);
	if (!sizes)
		goto out_free;

	offset = lseek(handle->fd, 0, SEEK_CUR);

	/* hold any extra data for data */
	offset += cpus * (16);
	offset = (offset + (handle->page_size - 1)) & ~(handle->page_size - 1);

	for (i = 0; i < cpus; i++) {
		file = malloc_or_die(strlen(output_file) + 20);
		if (!file)
			goto out_free;
		sprintf(file, "%s.cpu%d", output_file, i);
		ret = stat(file, &st);
		if (ret < 0) {
			warning("can not stat '%s'", file);
			goto out_free;
		}
		free(file);
		file = NULL;
		offsets[i] = offset;
		sizes[i] = st.st_size;
		offset += st.st_size;
		offset = (offset + (handle->page_size - 1)) & ~(handle->page_size - 1);

		if (do_write_check(handle, &offsets[i], 8))
			goto out_free;
		if (do_write_check(handle, &sizes[i], 8))
			goto out_free;
	}

	for (i = 0; i < cpus; i++) {
		fprintf(stderr, "offset=%llx\n", offsets[i]);
		ret = lseek64(handle->fd, offsets[i], SEEK_SET);
		if (ret < 0) {
			warning("could not seek to %lld\n", offsets[i]);
			goto out_free;
		}
		check_size = copy_file(handle, cpu_data_files[i]);
		if (check_size != sizes[i]) {
			errno = EINVAL;
			warning("did not match size of %lld to %lld",
			    check_size, sizes[i]);
			goto out_free;
		}
	}

	free(offsets);
	free(sizes);

	return handle;

 out_free:
	free(file);
	free(offsets);
	free(sizes);

	tracecmd_output_close(handle);
	return NULL;
}