#include "amiberry_filesys.hpp"

#include "sysconfig.h"
#include "sysdeps.h"
#include "fsdb.h"

#if defined REDQUARK
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include "include/crc32.h"
#endif

string prefix_with_application_directory_path(string currentpath)
{
#if (defined ANDROID) || (defined REDQUARK)
    char *efd = getenv("EXTERNAL_FILES_DIR");
    if( efd != NULL ) {
        return efd + ("/" + currentpath);
    } else {
	    return currentpath;
    }
#else
	return currentpath;
#endif
}

int my_setcurrentdir(const TCHAR* curdir, TCHAR* oldcur)
{
	const auto ret = 0;
	if (oldcur)
		getcwd(oldcur, MAX_DPATH);
	if (curdir)
		chdir(curdir);
	return ret;
}


int my_mkdir(const char* name)
{
	return mkdir(name, 0777);
}


int my_rmdir(const char* name)
{
	return rmdir(name);
}


int my_unlink(const char* name)
{
	return unlink(name);
}


int my_rename(const char* oldname, const char* newname)
{
	return rename(oldname, newname);
}

struct my_opendir_s* my_opendir(const char* name)
{
	auto * mod = xmalloc (struct my_opendir_s, 1);
	if (!mod)
		return nullptr;
	mod->h = opendir(name);
	if (mod->h == nullptr)
	{
		xfree (mod);
		return nullptr;
	}
	return mod;
}


void my_closedir(struct my_opendir_s* mod)
{
	if (mod)
		closedir(static_cast<DIR *>(mod->h));
	xfree (mod);
}


int my_readdir(struct my_opendir_s* mod, char* name)
{
	if (!mod)
		return 0;

	auto* de = readdir(static_cast<DIR *>(mod->h));
	if (de == nullptr)
		return 0;
	strncpy(name, de->d_name, MAX_DPATH - 1);
	return 1;
}


void my_close(struct my_openfile_s* mos)
{
	if (mos)
		close(mos->h);
	xfree (mos);
}


uae_s64 my_lseek(struct my_openfile_s* mos,const uae_s64 offset, const int pos)
{
	return lseek(mos->h, offset, pos);
}


uae_s64 my_fsize(struct my_openfile_s* mos)
{
	const auto pos = lseek(mos->h, 0, SEEK_CUR);
	const auto size = lseek(mos->h, 0, SEEK_END);
	lseek(static_cast<int>(mos->h), pos, SEEK_SET);
	return size;
}


unsigned int my_read(struct my_openfile_s* mos, void* b, unsigned int size)
{
	const auto bytes_read = read(mos->h, b, size);
	if (bytes_read == -1)
	{
		write_log("WARNING: my_read failed (-1)\n");
		return 0;
	}
	return static_cast<unsigned int>(bytes_read);
}


unsigned int my_write(struct my_openfile_s* mos, void* b, unsigned int size)
{
	const auto bytes_written = write(mos->h, b, size);
	if (bytes_written == -1)
	{
		write_log("WARNING: my_write failed (-1) fd=%d buffer=%p size=%d\n",
			mos->h, b, size);
		write_log("errno %d\n", errno);
		write_log("  mos %p -> h=%d\n", mos, mos->h);
		return 0;
	}
	return static_cast<unsigned int>(bytes_written);
}

int my_existslink(const char* name)
{
	struct stat st{};
	if (lstat(name, &st) == -1)
	{
		return 0;
	}
	if (S_ISLNK(st.st_mode))
		return 1;
	return 0;
}

int my_existsfile(const char* name)
{
	struct stat st{};
	if (lstat(name, &st) == -1)
	{
		return 0;
	}
	if (!S_ISDIR(st.st_mode))
		return 1;
	return 0;
}


int my_existsdir(const char* name)
{
	struct stat st{};
	if (lstat(name, &st) == -1)
	{
		return 0;
	}
	if (S_ISDIR(st.st_mode))
		return 1;
	return 0;
}


struct my_openfile_s* my_open(const TCHAR* name, const int flags)
{
	auto * mos = xmalloc (struct my_openfile_s, 1);
	if (!mos)
		return nullptr;
	if (flags & O_CREAT)
		mos->h = open(name, flags, 0660);
	else
		mos->h = open(name, flags);
	if (!mos->h)
	{
		xfree (mos);
		mos = nullptr;
	}
	return mos;
}


int my_truncate(const TCHAR* name, uae_u64 len)
{
	return truncate(name, len);
}


int my_getvolumeinfo(const char* root)
{
	struct stat st{};
	const auto ret = 0;

	if (lstat(root, &st) == -1)
		return -1;
	if (!S_ISDIR(st.st_mode))
		return -2;
	return ret;
}


FILE* my_opentext(const TCHAR* name)
{
	return fopen(name, "re");
}


bool my_issamepath(const TCHAR* path1, const TCHAR* path2)
{
	return _tcsicmp(path1, path2) == 0;
}


const TCHAR* my_getfilepart(const TCHAR* filename)
{
	const auto* p = _tcsrchr(filename, '\\');
	if (p)
		return p + 1;
	p = _tcsrchr(filename, '/');
	if (p)
		return p + 1;
	return p;
}


/* Returns 1 if an actual volume-name was found, 2 if no volume-name (so uses some defaults) */
int target_get_volume_name(struct uaedev_mount_info* mtinf, struct uaedev_config_info* ci, bool inserted,
                           bool fullcheck, int cnt)
{
	sprintf(ci->volname, "DH_%c", ci->rootdir[0]);
	return 2;
}

const TCHAR * my_get_sha1_of_file( const char *filepath )
{
#if defined REDQUARK
    void *   mem;
    struct  stat sb;
    int     fd = -1;
    int     ret = 0;
    int len;

    if( (fd = open( (char*)filepath, O_RDONLY ) ) < 0 ) ret = (-3);
    if( ret == 0 && fstat( fd, &sb ) < 0 ) ret = (-2);

    if( ret == 0 ) {
        if( (mem = mmap( (caddr_t)0, (int)sb.st_size,
                        PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0 )) != MAP_FAILED ) {
            len = (int)sb.st_size;
        } else ret = (-1);
    }

    if( ret > (-3) ) close( fd );

    if( ret < 0 ) return NULL;

    const TCHAR *sha1 = get_sha1_txt( mem, len );

    munmap( (caddr_t)mem, (size_t)len );

    return sha1; 
#else
    return NULL; // TODO Other platforms
#endif
}
