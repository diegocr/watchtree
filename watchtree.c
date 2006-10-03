/* ***** BEGIN LICENSE BLOCK *****
 * Version: MIT/X11 License
 * 
 * Copyright (c) 2006 Diego Casorran
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 * 
 * Contributor(s):
 *   Diego Casorran <dcasorran@gmail.com> (Original Author)
 * 
 * ***** END LICENSE BLOCK ***** */


/**
 * :ts=4
 * 
 * $Id: watchtree.c,v 0.2 2006/10/03 21:34:11 diegocr Exp $
 * 
 ****************************************************************************
 * 
 * ChangeLog
 * 
 * - 0.1 20051003  First Public Version
 * - 0.2 20061003  Added avility to report updated/new files ONLY
 * 					(hey, just a year after!, i can't believe =)
 * 
 * 
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <dirent.h>
#include <stdarg.h>
#include <getopt.h>
#include <unistd.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <sys/time.h>

#ifdef AMIGA
# include <exec/types.h>
#else
typedef unsigned long ULONG;
typedef unsigned short UWORD;
typedef unsigned char *STRPTR;
typedef char BYTE;
#endif

#define PROGRAM_NAME				"watchtree"
#define PROGRAM_VERSION				"0.2"
#define DEFAULT_DBFILE				"." PROGRAM_NAME ".db"
#define DEFAULT_LOGFILE				"." PROGRAM_NAME ".log"
#define DEFAULT_INITIAL_DBLEN		1024

#define DEFAULT_LOGNEWFILE_FTIME	"%b %d %Y, %X"
#define DEFAULT_LOGCHANGED_FTIME	"%b %d %Y, %X"
#define DEFAULT_REMFILE_FTIME		DEFAULT_LOGCHANGED_FTIME
#define DEFAULT_LOGOPEN_FTIME		"- %x, %X:\n"

#ifndef MAKE_ID
# define MAKE_ID(a,b,c,d)	\
	((ULONG) (a)<<24 | (ULONG) (b)<<16 | (ULONG) (c)<<8 | (ULONG) (d))
#endif

#define MAX_PATHLEN		8192 /* hmmm.. */

#define dbfill( src, type )			\
	do {							\
		if((cdbl + sizeof(type)) >= db_length) {	\
			db_length *= 2;			\
			dbdata = realloc( dbdata-cdbl, db_length );	\
			dbdata += cdbl;			\
		}							\
		*((type *) dbdata ) = src;	\
		dbdata += sizeof( type );	\
		cdbl += sizeof( type );		\
	} while(0)

#define dbfillcpy( src, length )	\
	do {							\
		if((cdbl + (length)) >= db_length) {	\
			db_length += ((length) + 512);	\
			dbdata = realloc( dbdata-cdbl, db_length );	\
			dbdata += cdbl;			\
		}							\
		memcpy( dbdata, src, length );	\
		dbdata += length;			\
		cdbl += length;				\
	} while(0)

#define dbget( dest, type )			\
	do {							\
		dest = *((type *) dbdata );	\
		dbdata += sizeof( type );	\
	} while(0)

#define dbgetcpy( dest, size )		\
	do {							\
		size_t len = (size);		\
		dest = malloc( len + 2 );	\
		assert(dest != NULL);		\
									\
		memcpy( dest, dbdata, len);	\
		((STRPTR) dest) [len] = 0;	\
		dbdata += len;				\
									\
	} while(0)

#define VERBOSE( fmt, args... )		\
	if( verbose ) do {				\
		log_output( "\e[1m" ## fmt ## "\e[0m\n", ## args );	\
	} while(0)



typedef struct __database_field {
	STRPTR file;	// filename (full qualified path)
	time_t time;		// file's datestamp (seconds, st->st_mtime)
	
	int filelen;	// filename length
	size_t filesize;	// file size in bytes
	
	void *datafile;	// file's contents (for when using diff)
	
	// TODO: lots more... (diff implementation, etc)
	
	struct __database_field *next;
} * db_entry;


/** db entrys: 
 ** local is readed on startup, next new tree is scaned, later both compared
 **/
static db_entry loaddb = NULL;		// last saved db
static db_entry writedb = NULL;		// current tree

const ULONG __dbid  = MAKE_ID( '<','.','d','b' );
const ULONG __dbend = MAKE_ID( '>','E','O','F' );

FILE *log_fd = (FILE *) NULL;
char  log_db[512];

// getopt options:
int useleadingdotfiles = 0;
int addfilestodb = 0;
char *diffcmd = NULL;
char *restorebackedfiles = NULL;
char *ignpattern = NULL;
int optquiet = 0;
int cvsignore = 0;
int listdbcontents = 0;
int verbose = 0;

/* miscs functions */
char *format_time(const char *fmt, time_t *time)
{
	char string[256];
	struct tm *punt = localtime( time );
	
	strftime( string, sizeof(string)-1, fmt, punt );
	
	return strdup( string );
}

void *FileToMem( const char *filename, size_t *size )
{
	FILE *fd;
	int fsize = -1;
	void *data = NULL;
	struct stat st;
	
	if(!(fd = fopen( filename, "r")))
		return NULL;
	
#if 0
	// fixme..
	fseek( fd, 0, SEEK_END );
	fsize = ftell( fd );
	fseek( fd, 0, SEEK_SET );
#else
	stat( filename, &st );
	fsize = st.st_size;
#endif
	assert(fsize >= 0);
	
	if(size)
		*size = fsize;
	
	if((data = malloc( fsize ))) {
		
		int r = fread( data, sizeof(*data), fsize, fd );
		fclose( fd );
		
		if(r != (int)fsize) {
			free(data);
			data = NULL;
		}
	}
	
	return data;
}

void write_file( const char *filename, void *data, size_t size )
{
	FILE *fd;
	int w;
	
	fd = fopen( filename, "w");
	assert( fd );
	
	w = fwrite( data, 1, size, fd );
	assert(w == (int)size);
	
	fclose(fd);
}

/* logging functions */
void log_open( void )
{
	if(log_fd == NULL) {
		
		char *fecha;
		time_t lt;
		int r, w;
		
		log_fd = fopen( log_db, "a");
		assert( log_fd );
		
		lt = time(NULL);
		
		fecha = format_time( DEFAULT_LOGOPEN_FTIME, &lt );
		
		r = strlen(fecha);
		w = fwrite( fecha, 1, r, log_fd );
		assert(r == w);
	}
}

void log_close( void )
{
	if(log_fd) {
		
		fwrite( "\n\x1B\n", 1, 3, log_fd );
		
		fclose(log_fd);
		log_fd = NULL;
	}
}

void log_output( char *fmt, ... )
{
	time_t t;
	char *hora;
	va_list args;
	
	if( optquiet )
		return;
	
	t = time(NULL);
	hora = format_time( "[%X] ", & t );
	printf( hora );
	
	va_start( args, fmt );
	vprintf( fmt, args );
	va_end( args );
	
	free( hora );
}

void log_write( char *fmt, ... )
{
	int w, r;
	va_list args;
	char string[MAX_PATHLEN+32];
	
	log_open ( ) ;
	
	va_start( args, fmt );
	vsnprintf( string, sizeof(string)-1, fmt, args );
	va_end( args );
	
	r = strlen(string);
	w = fwrite( string, 1, r, log_fd );
	assert(r == w);
}

void log_newfile( db_entry db )
{
	char *timestr = format_time( DEFAULT_LOGNEWFILE_FTIME, &db->time);
	
	VERBOSE(">>> file \"%s\" has been added.", db->file );
	
	assert(timestr != NULL);
	
	log_write("\t* [N] %s,\n", db->file );
	log_write("\t   \\- datestamp: %s\n", timestr );
	
	free(timestr);
}

void log_remfile( db_entry db )
{
	char *timestr = format_time("%b %d %Y, %X", &db->time);
	
	VERBOSE(">>> file \"%s\" has been removed.", db->file );
	
	assert(timestr != NULL);
	
	log_write("\t* [R] %s,\n", db->file );
	log_write("\t   \\- last modified on %s\n", timestr );
	
	free(timestr);
}

void log_changed( db_entry old, db_entry new )
{
	int diffbytes = (new->filesize) - (old->filesize);
	
	VERBOSE(">>> file \"%s\" has been changed.", old->file );
	
	log_write("\t* [U] %s, %s%ld bytes\n", old->file, 
		 (diffbytes > 0) ? "+":"", diffbytes );
	
	if( ! diffcmd ) {
		char *timestr = format_time( DEFAULT_LOGCHANGED_FTIME, &old->time);
		char *timestr2 = format_time(DEFAULT_LOGCHANGED_FTIME, &new->time);
		
		assert(timestr != NULL);
		assert(timestr2 != NULL);
		
		log_write("\t   \\- datestamp old: %s, new: %s\n", timestr, timestr2 );
		
		free(timestr); free(timestr2);
	}
	else {
		char command_line[512], *tmpfile = "/tmp/." PROGRAM_NAME ".tmp",
			*outfile = "/tmp/." PROGRAM_NAME ".out", *tmpoutfile;
		
		size_t w, fsize;
		
		write_file( tmpfile, old->datafile, old->filesize );
		
		snprintf( command_line, sizeof(command_line)-1,
			"%s \"%s\" \"%s\" >%s", diffcmd, tmpfile, new->file, outfile );
		
		system( command_line );
		
		tmpoutfile = FileToMem( outfile, & fsize );
		assert(tmpoutfile != NULL);
		
		unlink( outfile );
		
		log_write("\t   \\- diff patch follow:\n");
		
		w = fwrite( tmpoutfile, 1, fsize, log_fd );
		assert(w == fsize);
		
		free(tmpoutfile);
		
		log_write("\n");
	}
}


/* DB Functions */
int dbinsert( const char *file, struct stat *st, db_entry *destdb, void *datafile )
{
	int filelen;
	db_entry db;
	
	VERBOSE("inserting database entry for file \"%s\" (%p, %p, %p)", file, st, (*destdb), datafile );
	
	assert(file != NULL);
	assert( st != NULL );
	
	filelen = strlen(file);
	assert(filelen > 0 );
	
	db = malloc(sizeof(*db));
	assert(db != NULL);
	
	db->file = strdup( file );
	assert(db->file != NULL);
	
	db->filelen = filelen;
	db->filesize = st->st_size;
	db->time = st->st_mtime;
	db->datafile = datafile;
	
	db->next = (*destdb);
	(*destdb) = db;
	
	return 0;
}

int dbread( const char *file, db_entry * db )
{
	size_t size;
	STRPTR data, dbdata;
	
	VERBOSE("reading database from file \"%s\", to %p...", file, (*db));
	
	data = dbdata = FileToMem( file, &size );
	assert(data != NULL);
	
	assert(*((ULONG *) dbdata ) == __dbid );
	
	dbdata += sizeof(ULONG);
	
	do {
		int filelen;
		struct stat st;
		char *file = NULL;
		BYTE isfileincluded;
		void *datafile = NULL;
		
		if(*((ULONG *) dbdata ) == __dbend)
			break;
		
		dbget( filelen, UWORD );
		dbgetcpy( file, filelen );
		
		dbget( st.st_mtime, ULONG );
		dbget( st.st_size,  ULONG );
		
		dbget( isfileincluded, BYTE );
		
		assert(isfileincluded == '1' || isfileincluded == '0');
		
		if(isfileincluded == '1')
		{
			dbgetcpy( datafile, st.st_size );
		}
		
		dbinsert( file, &st, db, datafile );
		free( file );
		
		assert((*db)->filelen == filelen);
		
	} while(1);
	
	free(data);
	
	return 0;
}

int dbwrite( const char *file, db_entry * db )
{
	ULONG db_length = DEFAULT_INITIAL_DBLEN, cdbl=0;
	db_entry d;
	FILE *fh;
	
	STRPTR dbdata;
	
	VERBOSE("writing database %p, to \"%s\"...", (*db), file );
	
	if(!(fh = fopen( file, "w" ))) {
		printf("cannot open \"%s\": %s\n", file, strerror(errno));
		return -1;
	}
	
	dbdata = malloc(db_length);
	assert(dbdata != NULL);
	
	dbfill( __dbid, ULONG );
	
	for( d = (*db) ; d ; d = d->next )
	{
		STRPTR str=d->file;
		
		dbfill( d->filelen, UWORD );
		
		#if 0
		do {
			dbfill( *str++, BYTE );
			
		} while( *str );
		#else
		dbfillcpy( str, d->filelen );
		#endif
		
		dbfill( d->time, ULONG );
		dbfill( d->filesize, ULONG);
		
		if(addfilestodb)
		{
			dbfill( '1', BYTE );
			dbfillcpy( d->datafile, d->filesize );
		}
		else {
			dbfill( '0', BYTE );
		}
	}
	
	dbfill( __dbend, ULONG );
	
	dbdata -= cdbl;
	fwrite( dbdata, sizeof(*dbdata), cdbl, fh );
	fclose( fh );
	
	free(dbdata);
	
	return 0;
}

db_entry dblook( const char *file, db_entry *db )
{
	db_entry d = (*db);
	
	assert(file != NULL);
	
	for( ; d ; d = d->next )
	{
		if(!strcmp( file, d->file ))
			return d;
	}
	
	return NULL;
}

int dbcompare( db_entry * old, db_entry * new )
{
	db_entry d = (*new);
	int res = 0;
	
	VERBOSE("comparing databases %p and %p...", (*old), (*new));
	
	assert((*new) != NULL);
	assert((*old) != NULL);
	
	// check changed/added files
	for( ; d ; d = d->next )
	{
		db_entry n = dblook( d->file, old );
		
		if(n == NULL) {
			log_newfile( d );	res = 1 ;
		}
		else if(d->time != n->time) {
			log_changed( n, d );	res = 1 ;
		}
	}
	
	// check removed files
	for( d = (*old); d ; d = d->next )
	{
		db_entry n = dblook( d->file, new );
		
		if(n == NULL) {
			log_remfile( d );	res = 1 ;
		}
	}
	
	return res;
}

void dbrestore( db_entry * db, STRPTR root )
{
	db_entry d;
	
	assert((*db) != NULL);
	assert(root != NULL);
	
	VERBOSE("restoring database %p, to \"%s\"...", (*db), root );
	
	for( d = (*db) ;  d ; d = d->next )
	{
		char new_path[MAX_PATHLEN], *o=d->file;
		
		assert(d->datafile != NULL);
		assert(d->file != NULL);
		assert(*d->file == '/' || *d->file == '.');
		
		for( ++o ; *o && *o != '/' ; o++ );
		
		snprintf( new_path, sizeof(new_path)-1, "%s/%s", root, o );
		
		write_file( new_path, d->datafile, d->filesize );
	}
}

void dblist( db_entry * db )
{
	db_entry d;
	
	VERBOSE("listing database %p...", (*db));
	
	assert((*db) != NULL);
	
	for( d = (*db); d ; d = d->next )
	{
		char *hora = format_time( "%b %d %Y %H:%M", & d->time );
		
		printf("%16ld %s %s\n", d->filesize, hora, d->file );
		free( hora );
	}
}

void log_updated( db_entry a, db_entry b )
{
	if( b == NULL ) // theres a new file
	{
		log_output("New File:\t%s\n", a->file );
	}
	else if( a == NULL ) // theres a removed file
	{
		log_output("Removed:\t%s\n", b->file );
	}
	else // both suplied, a and b, which means the file has changed
	{
		log_output("Updated:\t%s\n", a->file );
	}
}

void dbshowupdated( db_entry * old, db_entry * new )
{
	db_entry d = (*new);
	
	assert((*new) != NULL);
	assert((*old) != NULL);
	
	// check changed/added files
	for( ; d ; d = d->next )
	{
		db_entry n = dblook( d->file, old );
		
		if(n == NULL) {
			log_updated( d, NULL );
		}
		else if(d->time != n->time) {
			log_updated( n, d );
		}
	}
	
	// check removed files
	for( d = (*old); d ; d = d->next )
	{
		db_entry n = dblook( d->file, new );
		
		if(n == NULL) {
			log_updated( NULL, d );
		}
	}
}

void dbfree( db_entry *db )
{
	db_entry d, next;
	
	VERBOSE("freeing database %p...", (*db));
	
	for( d = (*db) ; d ; d = next ) 
	{
		next = d->next;
		
		if(d->datafile)
			free(d->datafile);
		
		free(d->file);
		free( d );
	}
}



static __inline__ char *__strsep(char **string, const char *delim)
{
	char *iter, *str;
	
	if (!string || !*string || !**string)
		return NULL;

	str = *string;

	if((iter = strstr(str, delim)))
	{
		*iter = 0;
		iter += strlen(delim);
	}

	*string = iter;

	return str;

}

static __inline__ int readcvsignore( const char *path, char *patterns[] )
{
	char *pf, *f, *pat, file[MAX_PATHLEN];
	int x = 0;
	
	if(ignpattern) {
		patterns[x] = strdup( ignpattern );
		assert(patterns[x] != NULL);	x++;
	}
	
	patterns[x] = NULL;
	
	if( ! cvsignore )
		return 0;
	
	snprintf( file, sizeof(file)-1, "%s/%s", path, ".cvsignore" );
	
	if(!(f = pf = FileToMem( file, NULL )))
		return 0;
	
	while((pat = __strsep( &f, "\n")))
	{
		patterns[x] = strdup( pat );
		assert(patterns[x] != NULL);	x++;
	}
	
	patterns[x] = NULL;
	
	free( pf );
	
	return 1;
}

static __inline__ int checkpatterns( const char *filename, char *patterns[] )
{
	int x;
	
	for( x = 0; patterns[x]; x++ )
	{
		if(fnmatch( patterns[x], filename, 0 ) != FNM_NOMATCH )
			return 1;
	}
	
	return 0;
}

static __inline__ void freepatterns( char *patterns[] )
{
	int x;
	
	for( x = 0; patterns[x]; x++ )
	{
		free ( patterns[x] );
	}
}



int read_tree( const char *path )
{
	DIR *dir;
	struct dirent *d;
	struct stat st;
	char *patterns[99];
	
	VERBOSE("reading tree at \"%s\"...", path );
	
	if(!(dir = opendir (path)))
		return -1;
	
	readcvsignore( path, patterns );
	
	while((d = readdir (dir)))
	{
		char fullpath[MAX_PATHLEN];
		
		if( checkpatterns( d->d_name, patterns )) {
			
			VERBOSE("- \"%s/%s\" do not match, skipping...\n", path, d->d_name );
			
			continue;
		}
		
		if( useleadingdotfiles ) {
			if(!strcmp (d->d_name, ".") || !strcmp (d->d_name, ".."))
				continue;
			
			if(!strcmp(d->d_name,DEFAULT_DBFILE)||!strcmp(d->d_name,DEFAULT_LOGFILE))
				continue;
		}
		else {
			if(*d->d_name == '.')
				continue;
		}
		
		snprintf (fullpath, sizeof (fullpath) - 1, "%s/%s", path, d->d_name);
		
		if( stat(fullpath, &st) == -1 )
		{
			printf("stat failed on %s: %s\n", fullpath, strerror(errno));
			continue;
		}
		
		if(S_ISDIR (st.st_mode))
		{
			if(read_tree( fullpath ) == -1)
				return -1;
		}
		else
		{
			void *datafile = NULL;
			
			if(addfilestodb)
			{
				datafile = FileToMem( fullpath, NULL );
				assert(datafile != NULL);
			}
			
			if(dbinsert(fullpath, &st, &writedb, datafile) < 0 )
				return -2;
		}
	}
	
	closedir (dir);
	
	freepatterns( patterns );
	
	return 0;
}

int main( int argc, char *argv[] )
{
	int opt, opthelp=0, optShowUpdated=0;
	struct stat st;
	char *path = NULL, dbpath[512];
	
	while((opt = getopt( argc, argv, "hp:tld:aqr:i:vcVs")) != -1)
	{
		switch( opt )
		{
			case 'h':
				opthelp = 1;
				break;
			
			case 'p':
				path = strdup(optarg);
				assert(path != NULL);
				log_output("> Using \"%s\" root tree\n", path );
				break;
			
			case 't':
				useleadingdotfiles = 1;
				log_output("> dot files aren't ignored\n");
				break;
			
			case 'd':
				diffcmd = strdup(optarg);
				assert(diffcmd != NULL);
				log_output("> Using diff command \"%s\"\n", diffcmd );
			
			case 'a':
				addfilestodb = 1;
				log_output("> Adding/backuping files into db\n");
				break;
			
			case 'q':
				optquiet = 1;
				break;
			
			case 'r':
				restorebackedfiles = strdup(optarg);
				assert(restorebackedfiles != NULL);
				break;
			
			case 'i':
				ignpattern = strdup(optarg);
				assert(ignpattern != NULL);
				break;
			
			case 'c':
				cvsignore = 1;
				break;
			
			case 'l':
				listdbcontents = 1;
				break;
			
			case 'V':
				printf( PROGRAM_NAME " v" PROGRAM_VERSION " (c)2005 Diego Casorran.\n");
				printf("Compiled for " BUILD_SYSTEM " On " __DATE__ ", " __TIME__ "\n");
				return EXIT_SUCCESS;
			
			case 'v':
				verbose = 1;
				break;
			
			case 's':
				optShowUpdated = 1;
				break;
			
			default:
				opthelp = 1;
				break;
		}
	}
	
	if(/*optind >= argc ||*/ opthelp) {
		printf("Usage: %s [options]\n", argv[0] );
		printf("Options:\n");
		printf("  -h            This help message\n");
		printf("  -p<path>      Use <path> as root tree\n");
		printf("  -l            list databse contents\n");
		printf("  -t            accept leading dot files\n");
		printf("  -a            add files to db (backup)\n");
		printf("  -r<path>      restore backed files to <path>\n");
		printf("  -d<diffcmd>   use <diffcmd> to log changes (assumes -a switch)\n");
		printf("  -i<pattern>   ignore files/dirs which match with <pattern>\n");
		printf("  -c            use .cvsignore to get patterns to files/dirs to ignore\n");
		printf("  -s            Show Tree's Changes (Updated/Removed/New files)\n");
		printf("  -q            quiet (no do print to stdout)\n");
		printf("  -V            print version and build info\n");
		printf("  -v            verbose output\n");
		return EXIT_FAILURE;
	}
	
	if( path == NULL )
		path = strdup(".");
	
	snprintf( dbpath, sizeof(dbpath)-1, "%s/%s", path, DEFAULT_DBFILE );
	snprintf( log_db, sizeof(log_db)-1, "%s/%s", path, DEFAULT_LOGFILE);
	
	if(stat( dbpath, &st) != -1) {
		
		log_output("> Reading database from \"%s\"\n", dbpath );
		
		dbread( dbpath, & loaddb );
	}
	
	if(listdbcontents) {
		
		dblist( & loaddb );
	}
	else
	if(restorebackedfiles) {
		
		log_output("Restoring files to \"%s\"\n", restorebackedfiles );
		
		dbrestore( & loaddb, restorebackedfiles );
	}
	else
	if(read_tree( path ) || (writedb == NULL)) {
		
		perror("error reading tree");
	}
	else {
		int db_has_changed = 1; /* threat as new */
		
		if( loaddb ) {
			
			if( optShowUpdated )
			{
				optquiet = 0;
				
				log_output("Checking Tree's changes...\n");
				
				dbshowupdated( & loaddb, & writedb );
				db_has_changed = 0;
			}
			else
			{
				log_output("comparing databases...\n");
				
				db_has_changed = dbcompare( & loaddb, & writedb );
			}
		}
		
		if(db_has_changed) {
			log_output("writing updated database...\n");
			
			dbwrite( dbpath, & writedb );
		}
		else if(!optShowUpdated && loaddb) {
			log_output("*** no changed/added/removed file(s).\n");
		}
		
		dbfree( & writedb );
	}
	
	log_close ( ) ;
	free( path );
	
	if(diffcmd)
		free(diffcmd);
	
	if(restorebackedfiles)
		free(restorebackedfiles);
	
	if( loaddb )
		dbfree( & loaddb );
	
	return EXIT_SUCCESS;
}

