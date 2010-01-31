/*
* UAE - The Un*x Amiga Emulator
*
* Library of functions to make emulated filesystem as independent as
* possible of the host filesystem's capabilities.
* This is the Win32 version.
*
* Copyright 1997 Mathias Ortmann
* Copyright 1999 Bernd Schmidt
*/

#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "memory.h"

#include "fsdb.h"
#include "win32.h"
#include <windows.h>

#define TRACING_ENABLED 0
#if TRACING_ENABLED
#define TRACE(x) do { write_log x; } while(0)
#else
#define TRACE(x)
#endif

static int fsdb_debug = 0;

/* these are deadly (but I think allowed on the Amiga): */
#define NUM_EVILCHARS 7
static TCHAR evilchars[NUM_EVILCHARS] = { '\\', '*', '?', '\"', '<', '>', '|' };

#define UAEFSDB_BEGINS L"__uae___"
#define UAEFSDB_BEGINSX L"__uae___*"
#define UAEFSDB_LEN 604
#define UAEFSDB2_LEN 1632

/* The on-disk format is as follows:
* Offset 0, 1 byte, valid
* Offset 1, 4 bytes, mode
* Offset 5, 257 bytes, aname
* Offset 262, 257 bytes, nname
* Offset 519, 81 bytes, comment
* Offset 600, 4 bytes, Windows-side mode
*
* 1.6.0+ Unicode data
* 
* Offset  604, 257 * 2 bytes, aname
* Offset 1118, 257 * 2 bytes, nname
*        1632
*/

static TCHAR *make_uaefsdbpath (const TCHAR *dir, const TCHAR *name)
{
	int len;
	TCHAR *p;

	len = _tcslen (dir) + 1 + 1;
	if (name)
		len += 1 + _tcslen (name);
	len += 1 + _tcslen (FSDB_FILE);
	p = xmalloc (len * sizeof (TCHAR));
	if (!p)
		return NULL;
	if (name)
		_stprintf (p, L"%s\\%s:%s", dir, name, FSDB_FILE);
	else
		_stprintf (p, L"%s:%s", dir, FSDB_FILE);
	return p;
}

static int read_uaefsdb (const TCHAR *dir, const TCHAR *name, uae_u8 *fsdb)
{
	TCHAR *p;
	HANDLE h;
	DWORD read;

	read = 0;
	p = make_uaefsdbpath (dir, name);
	h = CreateFile (p, GENERIC_READ, 0,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (fsdb_debug)
		write_log (L"read_uaefsdb '%s' = %x\n", p, h);
	xfree (p);
	if (h != INVALID_HANDLE_VALUE) {
		memset (fsdb, 0, UAEFSDB2_LEN);
		ReadFile (h, fsdb, UAEFSDB2_LEN, &read, NULL);
		CloseHandle (h);
		if (read == UAEFSDB_LEN || read == UAEFSDB2_LEN) {
			if (fsdb_debug) {
				TCHAR *an, *nn, *co;
				write_log (L"->ok\n");
				an = aucp (fsdb + 5, currprefs.win32_fscodepage);
				nn = aucp (fsdb + 262, currprefs.win32_fscodepage);
				co = aucp (fsdb + 519, currprefs.win32_fscodepage);
				write_log (L"v=%02x flags=%08x an='%s' nn='%s' c='%s'\n",
					fsdb[0], ((uae_u32*)(fsdb+1))[0], an, nn, co);
				xfree (co);
				xfree (nn);
				xfree (an);
			}
			return 1;
		}
	}
	if (fsdb_debug)
		write_log (L"->fail %d, %d\n", read, GetLastError ());
	memset (fsdb, 0, UAEFSDB2_LEN);
	return 0;
}

static int delete_uaefsdb (const TCHAR *dir)
{
	TCHAR *p;
	int ret;

	p = make_uaefsdbpath (dir, NULL);
	ret = DeleteFile (p);
	if (fsdb_debug)
		write_log (L"delete_uaefsdb '%s' = %d\n", p, ret);
	xfree (p);
	return ret;
}

static int write_uaefsdb (const TCHAR *dir, uae_u8 *fsdb)
{
	TCHAR *p;
	HANDLE h;
	DWORD written = 0, dirflag, dirattr;
	DWORD attr = INVALID_FILE_ATTRIBUTES;
	FILETIME t1, t2, t3;
	int time_valid = FALSE;
	int ret = 0;

	p = make_uaefsdbpath (dir, NULL);
	dirattr = GetFileAttributes (dir);
	dirflag = FILE_ATTRIBUTE_NORMAL;
	if (dirattr != INVALID_FILE_ATTRIBUTES && (dirattr & FILE_ATTRIBUTE_DIRECTORY))
		dirflag = FILE_FLAG_BACKUP_SEMANTICS; /* argh... */
	h = CreateFile (dir, GENERIC_READ, 0,
		NULL, OPEN_EXISTING, dirflag, NULL);
	if (h != INVALID_HANDLE_VALUE) {
		if (GetFileTime (h, &t1, &t2, &t3))
			time_valid = TRUE;
		CloseHandle (h);
	}
	h = CreateFile (p, GENERIC_WRITE, 0,
		NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (fsdb_debug) {
		TCHAR *an, *nn, *co;
		an = aucp (fsdb + 5, currprefs.win32_fscodepage);
		nn = aucp (fsdb + 262, currprefs.win32_fscodepage);
		co = aucp (fsdb + 519, currprefs.win32_fscodepage);
		write_log (L"write_uaefsdb '%s' = %x\n", p, h);
		write_log (L"v=%02x flags=%08x an='%s' nn='%s' c='%s'\n",
			fsdb[0], ((uae_u32*)(fsdb+1))[0], an, nn, co);
		xfree (co);
		xfree (nn);
		xfree (an);
	}
	if (h == INVALID_HANDLE_VALUE && GetLastError () == ERROR_ACCESS_DENIED) {
		attr = GetFileAttributes (p);
		if (attr != INVALID_FILE_ATTRIBUTES) {
			if (attr & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN)) {
				SetFileAttributes (p, attr & ~(FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN));
				h = CreateFile (p, GENERIC_WRITE, 0,
					NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
				if (fsdb_debug)
					write_log (L"write_uaefsdb (2) '%s' = %x\n", p, h);
			}
		}
	}
	if (h != INVALID_HANDLE_VALUE) {
		WriteFile (h, fsdb, UAEFSDB2_LEN, &written, NULL);
		CloseHandle (h);
		if (written == UAEFSDB2_LEN) {
			if (fsdb_debug)
				write_log (L"->ok\n");
			ret = 1;
			goto end;
		}
	}
	if (fsdb_debug)
		write_log (L"->fail %d, %d\n", written, GetLastError ());

	DeleteFile (p);
end:
	if (attr != INVALID_FILE_ATTRIBUTES)
		SetFileAttributes (p, attr);
	if (time_valid) {
		h = CreateFile (dir, GENERIC_WRITE, 0,
			NULL, OPEN_EXISTING, dirflag, NULL);
		if (h != INVALID_HANDLE_VALUE) {
			SetFileTime (h, &t1, &t2, &t3);
			CloseHandle (h);
		}
	}
	xfree (p);
	return ret;
}

static void create_uaefsdb (a_inode *aino, uae_u8 *buf, int winmode)
{
	TCHAR *nn;
	char *s;
	buf[0] = 1;
	do_put_mem_long ((uae_u32 *)(buf + 1), aino->amigaos_mode);
	s = uacp (aino->aname, currprefs.win32_fscodepage);
	strncpy (buf + 5, s, 256);
	buf[5 + 256] = '\0';
	xfree (s);
	nn = nname_begin (aino->nname);
	s = uacp (nn, currprefs.win32_fscodepage);
	strncpy (buf + 5 + 257, s, 256);
	buf[5 + 257 + 256] = '\0';
	xfree (s);
	s = uacp (aino->comment ? aino->comment : L"", currprefs.win32_fscodepage);
	strncpy (buf + 5 + 2 * 257, s, 80);
	buf[5 + 2 * 257 + 80] = '\0';
	xfree (s);
	do_put_mem_long ((uae_u32 *)(buf + 5 + 2 * 257 + 81), winmode);
	_tcsncpy ((TCHAR*)(buf + 604), aino->aname, 256);
	_tcsncpy ((TCHAR*)(buf + 1118), nn, 256);
	aino->has_dbentry = 0;
}

static a_inode *aino_from_buf (a_inode *base, uae_u8 *buf, int *winmode)
{
	uae_u32 mode;
	a_inode *aino = xcalloc (sizeof (a_inode), 1);
	uae_u8 *buf2;
	TCHAR *s;

	buf2 = buf + 604;
	mode = do_get_mem_long ((uae_u32 *)(buf + 1));
	buf += 5;
	if (buf2[0]) {
		aino->aname = my_strdup ((TCHAR*)buf2);
	} else {
		aino->aname = aucp (buf, currprefs.win32_fscodepage);
	}
	buf += 257;
	buf2 += 257 * 2;
	if (buf2[0]) {
		aino->nname = build_nname (base->nname, (TCHAR*)buf2);
	} else {
		s = aucp (buf, currprefs.win32_fscodepage);
		aino->nname = build_nname (base->nname, s);
		xfree (s);
	}
	buf += 257;
	aino->comment = *buf != '\0' ? my_strdup_ansi (buf) : 0;
	buf += 81;
	aino->amigaos_mode = mode;
	*winmode = do_get_mem_long ((uae_u32 *)buf);
	aino->dir = ((*winmode) & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
	*winmode &= FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN;
	aino->has_dbentry = 0;
	aino->dirty = 0;
	aino->db_offset = 0;
	if((mode = GetFileAttributes (aino->nname)) == INVALID_FILE_ATTRIBUTES) {
		write_log (L"xGetFileAttributes('%s') failed! error=%d, aino=%p\n",
			aino->nname, GetLastError (), aino);
		return aino;
	}
	aino->dir = (mode & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
	return aino;
}

/* Return nonzero for any name we can't create on the native filesystem.  */
int fsdb_name_invalid (const TCHAR *n)
{
	int i;
	TCHAR a = n[0];
	TCHAR b = (a == '\0' ? a : n[1]);
	TCHAR c = (b == '\0' ? b : n[2]);
	TCHAR d = (c == '\0' ? c : n[3]);
	int l = _tcslen (n), ll;

	if (a >= 'a' && a <= 'z')
		a -= 32;
	if (b >= 'a' && b <= 'z')
		b -= 32;
	if (c >= 'a' && c <= 'z')
		c -= 32;

	/* reserved dos devices */
	ll = 0;
	if (a == 'A' && b == 'U' && c == 'X') ll = 3; /* AUX  */
	if (a == 'C' && b == 'O' && c == 'N') ll = 3; /* CON  */
	if (a == 'P' && b == 'R' && c == 'N') ll = 3; /* PRN  */
	if (a == 'N' && b == 'U' && c == 'L') ll = 3; /* NUL  */
	if (a == 'L' && b == 'P' && c == 'T'  && (d >= '0' && d <= '9')) ll = 4;  /* LPT# */
	if (a == 'C' && b == 'O' && c == 'M'  && (d >= '0' && d <= '9')) ll = 4; /* COM# */
	/* AUX.anything, CON.anything etc.. are also illegal names */
	if (ll && (l == ll || (l > ll && n[ll] == '.')))
		return 1;

	/* spaces and periods at the end are a no-no */
	i = l - 1;
	if (n[i] == '.' || n[i] == ' ')
		return 1;

	/* these characters are *never* allowed */
	for (i = 0; i < NUM_EVILCHARS; i++) {
		if (_tcschr (n, evilchars[i]) != 0)
			return 1;
	}

	/* the reserved fsdb filename */
	if (_tcscmp (n, FSDB_FILE) == 0)
		return 1;
	return 0; /* the filename passed all checks, now it should be ok */
}

uae_u32 filesys_parse_mask(uae_u32 mask)
{
	return mask ^ 0xf;
}

int fsdb_exists (TCHAR *nname)
{
	if (GetFileAttributes(nname) == INVALID_FILE_ATTRIBUTES)
		return 0;
	return 1;
}

/* For an a_inode we have newly created based on a filename we found on the
* native fs, fill in information about this file/directory.  */
int fsdb_fill_file_attrs (a_inode *base, a_inode *aino)
{
	int mode, winmode, oldamode;
	uae_u8 fsdb[UAEFSDB2_LEN];
	int reset = 0;

	if((mode = GetFileAttributes(aino->nname)) == INVALID_FILE_ATTRIBUTES) {
		write_log (L"GetFileAttributes('%s') failed! error=%d, aino=%p dir=%d\n",
			aino->nname, GetLastError(), aino, aino->dir);
		return 0;
	}
	aino->dir = (mode & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
	mode &= FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN;

	if ((base->volflags & MYVOLUMEINFO_STREAMS) && read_uaefsdb (aino->nname, NULL, fsdb)) {
		aino->amigaos_mode = do_get_mem_long ((uae_u32 *)(fsdb + 1));
		xfree (aino->comment);
		aino->comment = NULL;
		if (fsdb[5 + 2 * 257])
			aino->comment = my_strdup_ansi (fsdb + 5 + 2 * 257);
		xfree (aino_from_buf (base, fsdb, &winmode));
		if (winmode == mode) /* no Windows-side editing? */
			return 1;
		write_log (L"FS: '%s' protection flags edited from Windows-side\n", aino->nname);
		reset = 1;
		/* edited from Windows-side -> use Windows side flags instead */
	}

	oldamode = aino->amigaos_mode;
	aino->amigaos_mode = A_FIBF_EXECUTE | A_FIBF_READ;
	if (!(FILE_ATTRIBUTE_ARCHIVE & mode))
		aino->amigaos_mode |= A_FIBF_ARCHIVE;
	if (!(FILE_ATTRIBUTE_READONLY & mode))
		aino->amigaos_mode |= A_FIBF_WRITE | A_FIBF_DELETE;
	if (FILE_ATTRIBUTE_SYSTEM & mode)
		aino->amigaos_mode |= A_FIBF_PURE;
	if (FILE_ATTRIBUTE_HIDDEN & mode)
		aino->amigaos_mode |= A_FIBF_HIDDEN;
	aino->amigaos_mode = filesys_parse_mask(aino->amigaos_mode);
	aino->amigaos_mode |= oldamode & A_FIBF_SCRIPT;
	if (reset && (base->volflags & MYVOLUMEINFO_STREAMS)) {
		create_uaefsdb (aino, fsdb, mode);
		write_uaefsdb (aino->nname, fsdb);
	}
	return 1;
}

static int needs_fsdb (a_inode *aino)
{
	const TCHAR *nn_begin;

	if (aino->deleted)
		return 0;

	if (!fsdb_mode_representable_p (aino, aino->amigaos_mode) || aino->comment != 0)
		return 1;

	nn_begin = nname_begin (aino->nname);
	return _tcscmp (nn_begin, aino->aname) != 0;
}

int fsdb_set_file_attrs (a_inode *aino)
{
	uae_u32 tmpmask;
	uae_u8 fsdb[UAEFSDB2_LEN];
	uae_u32 mode;

	tmpmask = filesys_parse_mask (aino->amigaos_mode);

	mode = GetFileAttributes (aino->nname);
	if (mode == INVALID_FILE_ATTRIBUTES)
		return ERROR_OBJECT_NOT_AROUND;
	mode &= FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN;

	mode = 0;
	if ((tmpmask & (A_FIBF_WRITE | A_FIBF_DELETE)) == 0)
		mode |= FILE_ATTRIBUTE_READONLY;
	if (!(tmpmask & A_FIBF_ARCHIVE))
		mode |= FILE_ATTRIBUTE_ARCHIVE;
	if (tmpmask & A_FIBF_PURE)
		mode |= FILE_ATTRIBUTE_SYSTEM;
	if (tmpmask & A_FIBF_HIDDEN)
		mode |= FILE_ATTRIBUTE_HIDDEN;
	SetFileAttributes (aino->nname, mode);

	aino->dirty = 1;
	if (aino->volflags & MYVOLUMEINFO_STREAMS) {
		if (needs_fsdb (aino)) {
			create_uaefsdb (aino, fsdb, mode);
			write_uaefsdb (aino->nname, fsdb);
		} else {
			delete_uaefsdb (aino->nname);
		}
	}
	return 0;
}

/* return supported combination */
int fsdb_mode_supported (const a_inode *aino)
{
	int mask = aino->amigaos_mode;
	if (0 && aino->dir)
		return 0;
	if (fsdb_mode_representable_p (aino, mask))
		return mask;
	mask &= ~(A_FIBF_SCRIPT | A_FIBF_READ | A_FIBF_EXECUTE);
	if (fsdb_mode_representable_p (aino, mask))
		return mask;
	mask &= ~A_FIBF_WRITE;
	if (fsdb_mode_representable_p (aino, mask))
		return mask;
	mask &= ~A_FIBF_DELETE;
	if (fsdb_mode_representable_p (aino, mask))
		return mask;
	return 0;
}

/* Return nonzero if we can represent the amigaos_mode of AINO within the
* native FS.  Return zero if that is not possible.  */
int fsdb_mode_representable_p (const a_inode *aino, int amigaos_mode)
{
	int mask = amigaos_mode ^ 15;

	if (0 && aino->dir)
		return amigaos_mode == 0;

	if (mask & A_FIBF_SCRIPT) /* script */
		return 0;
	if ((mask & 15) == 15) /* xxxxRWED == OK */
		return 1;
	if (!(mask & A_FIBF_EXECUTE)) /* not executable */
		return 0;
	if (!(mask & A_FIBF_READ)) /* not readable */
		return 0;
	if ((mask & 15) == (A_FIBF_READ | A_FIBF_EXECUTE)) /* ----RxEx == ReadOnly */
		return 1;
	return 0;
}

TCHAR *fsdb_create_unique_nname (a_inode *base, const TCHAR *suggestion)
{
	TCHAR *c;
	TCHAR tmp[256] = UAEFSDB_BEGINS;
	int i;

	_tcsncat (tmp, suggestion, 240);

	/* replace the evil ones... */
	for (i = 0; i < NUM_EVILCHARS; i++)
		while ((c = _tcschr (tmp, evilchars[i])) != 0)
			*c = '_';

	while ((c = _tcschr (tmp, '.')) != 0)
		*c = '_';
	while ((c = _tcschr (tmp, ' ')) != 0)
		*c = '_';

	for (;;) {
		TCHAR *p = build_nname (base->nname, tmp);
		if (!fsdb_exists (p)) {
			write_log (L"unique name: %s\n", p);
			return p;
		}
		xfree (p);
		/* tmpnam isn't reentrant and I don't really want to hack configure
		* right now to see whether tmpnam_r is available...  */
		for (i = 0; i < 8; i++) {
			tmp[i+8] = "_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"[rand () % 63];
		}
	}
}

TCHAR *fsdb_search_dir (const TCHAR *dirname, TCHAR *rel)
{
	WIN32_FIND_DATA fd;
	HANDLE h;
	TCHAR *tmp, *p = 0;

	tmp = build_nname (dirname, rel);
	h = FindFirstFile (tmp, &fd);
	if (h != INVALID_HANDLE_VALUE) {
		if (_tcscmp (fd.cFileName, rel) == 0)
			p = rel;
		else
			p = my_strdup (fd.cFileName);
		FindClose (h);
	}
	xfree (tmp);
	return p;
}

static a_inode *custom_fsdb_lookup_aino (a_inode *base, const TCHAR *aname, int offset, int dontcreate)
{
	uae_u8 fsdb[UAEFSDB2_LEN];
	TCHAR *tmp1;
	HANDLE h;
	WIN32_FIND_DATA fd;
	static a_inode dummy;

	tmp1 = build_nname (base->nname, UAEFSDB_BEGINSX);
	if (!tmp1)
		return NULL;
	h = FindFirstFile (tmp1, &fd);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			if (read_uaefsdb (base->nname, fd.cFileName, fsdb)) {
				TCHAR *s = au (fsdb + offset);
				if (same_aname (s, aname)) {
					int winmode;
					FindClose (h);
					xfree (tmp1);
					xfree (s);
					if (dontcreate)
						return &dummy;
					return aino_from_buf (base, fsdb, &winmode);
				}
				xfree (s);
			}
		} while (FindNextFile (h, &fd));
		FindClose (h);
	}
	xfree (tmp1);
	return NULL;
}

a_inode *custom_fsdb_lookup_aino_aname (a_inode *base, const TCHAR *aname)
{
	return custom_fsdb_lookup_aino (base, aname, 5, 0);
}
a_inode *custom_fsdb_lookup_aino_nname (a_inode *base, const TCHAR *nname)
{
	return custom_fsdb_lookup_aino (base, nname, 5 + 257, 0);
}

int custom_fsdb_used_as_nname (a_inode *base, const TCHAR *nname)
{
	if (custom_fsdb_lookup_aino (base, nname, 5 + 257, 1))
		return 1;
	return 0;
}

