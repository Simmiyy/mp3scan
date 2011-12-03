#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdarg.h>
#include <id3/tag.h>

#ifdef __MYSQL
	#include <mysql.h>
#endif
#ifdef __SQLITE
	#include <sqlite3.h>
#endif

//TODO 
// - check every field of sql query to protect special char like '

/*
 S_IFMT       File type mask

 S_IFDIR      Directory

 S_IFIFO      FIFO special

 S_IFCHR      Character special

 S_IFBLK      Block special

 S_IFREG      Regular file

 S_IREAD      Owner can read

 S_IWRITE     Owner can write

 S_IEXEC      Owner can execute
*/


/*
Usage: mp3_scan [OPTIONS] PATH
Scan a directory to find mp3 files and insert the infos into a database

  PATH					directory to scan for mp3 files

Options:
  -v, --version			display program version and exit
  -h, --help			display this help and exit
  -V, --verbose			verbose output
  -f, --fsize			print the sum of the size of all mp3 files found
  -r, --recursive		recursively search mp3 files in subdirectories
  -c, --createtab TAB	create a new or use an existent table in database named TAB
  -p, --relativepath	insert into database relative paths instead of absolute
  -u, --usecolor		use color for command line output
  -i, --interactive		ask user when found an inconsistency
  -1, --ID3V1			use only informations from ID3v1 tag (default is use v1 and v2)
  -2, --ID3V2			use only informations from ID3v2 tag (default is use v1 and v2)
  
Filename:
Use file name information if no ID3 TAG found

  -n, --usefilename SEQTAG
  -s, --spacechar   SEPLIST
  
  SEQTAG				file name schema: A = Artist, T = Title, Y = Year, M = Album, * = Separator
  SEPLIST				list of characters to replace with space 

  Examples: --usefilename AT- --spacechar _ with filenames like: Ludwig_van_Beethoven_-_Fur_Elise.mp3
            --usefilename TAY- with filenames like: Fur Elise - Ludwig van Beethoven - 1810.mp3

MySQL:
Use mysql as default database to store mp3 files' info

  -m, --mysql HOST USER [PASSWORD] DATABASE
  
  HOST					IP address od the MySQL database
  USER					username used for login
  PASSWORD				password used for login -optional-
  DATABASE				name of database to use

SQLite:
Use sqlite as default database to store mp3 files' info

  -l, --sqlite FILENAME
  
  FILENAME				filename for the database
*/

#define FALSE 0
#define TRUE  1
#define ID3v1 0x01
#define ID3v2 0x02

#define USAGE "Usage: mp3_scan [OPTIONS] PATH\nScan a directory to find mp3 files and insert the infos into a database\n\n  PATH\t\t\t\tdirectory to scan for mp3 files\n\nOptions:\n  -v, --version\t\t\tdisplay program version and exit\n  -h, --help\t\t\tdisplay this help and exit\n  -V, --verbose\t\t\tverbose output\n  -f, --fsize\t\t\tprint the sum of the size of all mp3 files found\n  -r, --recursive\t\trecursively search mp3 files in subdirectories\n  -c, --createtab TAB\t\tcreate a new or use an existent table in database named TAB\n  -p, --relativepath\t\tinsert into database relative paths instead of absolute\n  -u, --usecolor\t\tuse color for command line output\n  -i, --interactive\t\task user when found an inconsistency\n  -1, --ID3V1\t\t\tuse only informations from ID3v1 tag (default is use v1 and v2)\n  -2, --ID3V2\t\t\tuse only informations from ID3v2 tag (default is use v1 and v2)\n\nFilename:\nUse file name information if no ID3 TAG found\n\n  -n, --usefilename SEQTAG\n  -s, --spacechar   SEPLIST\n\n  SEQTAG\t\t\tfile name schema: A = Artist, T = Title, Y = Year, M = Album, * = Separator\n  SEPLIST\t\t\tlist of characters to replace with space\n\n  Examples: --usefilename AT- --spacechar _ with filenames like: Ludwig_van_Beethoven_-_Fur_Elise.mp3\n            --usefilename TAY- with filenames like: Fur Elise - Ludwig van Beethoven - 1810.mp3\n\nMySQL:\nUse mysql as default database to store mp3 files' info\n\n  -m, --mysql HOST USER [PASSWORD] DATABASE\n\n  HOST\t\t\t\tIP address od the MySQL database\n  USER\t\t\t\tusername used for login\n  PASSWORD\t\t\tpassword used for login -optional-\n  DATABASE\t\t\tname of database to use\n\nSQLite:\nUse sqlite as default database to store mp3 files' info\n\n  -l, --sqlite FILENAME\n\n  FILENAME\t\t\tfilename for the database\n"

#define COPYRIGHT "\nmp3_scan v1.0, Copyright (c) 2009 - by Simmiyy^ (simmiyy@gmail.com)\n"
#define VERSION "This is free software. You may redistribute copies of it under the terms of\nthe GNU General Public License <http://www.gnu.org/licenses/gpl.html>.\nThere is NO WARRANTY, to the extent permitted by law.\n\n"

#ifndef PATH_MAX
#define PATH_MAX 256
#endif

#define VERBOSE_LOG( A ) { if( bVerbose ) print_message( STATUS, A ); }
#define VERBOSE_LOG1( A, B ) { if( bVerbose ) print_message( STATUS, A, B ); }
#define CHECKBUFFERS( A, B, C, D ) \
	if( A[0] != '\0' && B[0] != '\0' && strcmp( A, B ) != 0 && bInteractive && chose_field( C, A, B, D ) ) A[0] = '\0'; \
	if( A[0] == '\0' && B[0] != '\0' ) strcpy( A, B );

typedef enum {
	TOO_MANY_DB = 0,
	NO_DB_SELECTED,
	NO_FLAG_SPECIFIED,
	NO_TABLE_NAME,
	MYSQL_PARAM_ERROR,
	SQLITE_PARAM_ERROR,
	CREATE_TABLE_ERROR,
	OPENDIR_ERROR,
	CHDIR_ERROR,
	FOPEN_ERROR,
	DBOPEN_ERROR,
	DBCLOSE_ERROR,
	DBUNKNOWN_ERROR,
	VERSION_USAGE_OUTPUTTED,
	PRINT_VERSION,
	PRINT_HELP,
	UNKNOW_PARAM,
	PARAM_OK,
	DBOPENED,
	DBCLOSED,
	END_LOOP,
	BAD_FORMAT,
	FNFORMAT_PARAM_ERROR,
	SPACECHAR_PARAM_ERROR
} RETURNCODE;

typedef enum {
	USE_MYSQL,
	USE_SQLITE
} SELDB;

typedef enum {
	ERROR,
	STATUS,
	OUTPUT,
	WARNING
} MSGCODE;

#ifndef __cplusplus
	typedef unsigned int bool;
#endif
typedef unsigned char byte;

/*
 * Global Variables
 */

bool  bRecursive;
bool  bVerbose;
bool  bCreateTab;
bool  bFsInfo;
bool  bNoSpaceAvailable;
bool  bUseColor;
bool  bRelPath;
bool  bUseFileName;
bool  bUseSpaceChar;
bool  bInteractive;
byte  TagVersion;

union handle_db {
#ifdef __MYSQL
	MYSQL *mysql_handle;
#endif
#ifdef __SQLITE
	sqlite3 *sqlite_handle;
#endif
	bool init;
} DB_handle;

const char* pPath;
const char* pHost;
const char* pUser;
const char* pPass;
const char* pDB;
const char* pFilename;
const char* pTabname;
const char* pProgramName;
const char* pError;
const char* pFileNameFormat;
const char* pSpaceChar;

char  szCurrentPath[PATH_MAX];
char  szRelativePath[PATH_MAX];
int   Mp3Counter;

const char* pTBName = "MP3";

SELDB UseDB;

/*
 * Prototype specifications
 */

RETURNCODE check_flag( int argc, const char* argv[] );
RETURNCODE mp3_scan_loop( bool bScanOnly );
RETURNCODE OpenDBConnection();
RETURNCODE CloseDBConnection();
bool is_mp3_file( char* pFileName );
void filename_to_field( const char *pFileName, char *szTitle, char *szArtist, char *szAlbum, char *szYear );
void sql_insert( const char* Title, const char* Artist, const char* Album, const char* Year, const char* FileName, const char* Path, off_t Size );
void get_tags( const char *filename, char *title, char *artist, char *album, char *year );
void print_message( MSGCODE code, const char* szFormat, ... );
void get_id3_tag( const char* pFileName, off_t size );
int chose_field( const char *filename, const char *field1, const char *field2, int fieldname );
void replace_char( char *str );
void print_error( RETURNCODE code );
void create_table();
void size_count( off_t Size );
void init();

/*
 * Procedures
 */


// main 
int main( int argc, const char* argv[] ){
	
	RETURNCODE ret;
	pProgramName = argv[0];
	bNoSpaceAvailable = FALSE;
	Mp3Counter = 0;
	strcpy( szRelativePath, "" );								// initialize the relative path string
	int mysql_check=0;
	int sqlite_check=0;

#ifndef __MYSQL
    mysql_check=1;
#endif

#ifndef __SQLITE
    sqlite_check=1;
#endif

if(mysql_check && sqlite_check){
	print_message( ERROR, "Program compiled without DB support. Unable to continue.\n");
	exit( 0 );
}

	init();														// init all global variables
	
	if( ( ret = check_flag( argc, argv ) ) != PARAM_OK )
		print_error( ret );										// output the right message for the error code 

	if( !bNoSpaceAvailable ){
	
		getcwd( szCurrentPath, PATH_MAX );						// save current path
		
		VERBOSE_LOG( "Opening DB connection\n" );

		if( ( ret = OpenDBConnection() ) != DBOPENED )			// Open DB Connection
			print_error( ret );

		VERBOSE_LOG( "DB connection succeded\n" );

		if( bCreateTab ){
			
			VERBOSE_LOG( "Creating new table into DB\n" );
			create_table();
			VERBOSE_LOG( "Table creation succeded\n" );
		}

		VERBOSE_LOG1( "Changing to %s\n", pPath );

		if( chdir( pPath ) != 0 )								// change to pPath
			print_error( CHDIR_ERROR );

		VERBOSE_LOG( "Changed directory\n" );
		VERBOSE_LOG( "Start files counting\n" );

		if( ( ret = mp3_scan_loop( TRUE ) ) != END_LOOP )		// scan only loop
			print_error( ret );

		if( Mp3Counter > 0 ){

			VERBOSE_LOG1( "Found %d file(s)\n", Mp3Counter );
			VERBOSE_LOG( "Starting files scan\n" );
			
			if(  ( ret = mp3_scan_loop( FALSE ) ) != END_LOOP )	// mp3 scan loop
				print_error( ret );
		
			VERBOSE_LOG( "Files scan terminated\n" );
			
			if( bFsInfo )
				size_count( -1 );								// Print Total files size
			
		} else {
			
			print_message( ERROR, "No MP3 file found\n" );
		}
		
		VERBOSE_LOG( "Closing DB connection\n" );
		
		if( ( ret = CloseDBConnection() ) != DBCLOSED )			// close DB connection
			print_error( ret );
		
		VERBOSE_LOG( "DB connection closed\n" );
		VERBOSE_LOG1( "Changing back to %s\n", szCurrentPath );
			
		chdir( szCurrentPath );									// change to initial path

	} else {
		
		print_message( ERROR, "No space available on hard drive to store mp3 infos" );
	}
	
	if( bUseColor )
		printf( "\x1B[0m" );									// reset the shell color scheme
		
	exit( 0 );
}


// init global variables
void init(){

	bRecursive = FALSE;
	bVerbose = FALSE;
	bCreateTab = TRUE;
	bFsInfo = FALSE;
	bNoSpaceAvailable = FALSE;
	bUseColor = FALSE;
	bRelPath = FALSE;
	bUseFileName = FALSE;
	bUseSpaceChar = FALSE;
	bInteractive = FALSE;
	TagVersion = 0;
}

// main scan loop
RETURNCODE mp3_scan_loop( bool bScanOnly ){

	DIR	*dir;
	struct dirent *file;
	struct stat finfo;

	if( (dir = opendir(".")) == NULL )
		return OPENDIR_ERROR;
	
	while( ( file = readdir( dir ) ) != NULL ){

		if( strcmp( ".", file->d_name ) != 0 && strcmp( "..", file->d_name ) != 0 ){

			stat( file->d_name, &finfo );						// get file info and check the type

			if( finfo.st_mode & S_IFREG ){						// is a file

				if( is_mp3_file( file->d_name ) ){				// if it's a MP3 file
					if( bScanOnly )
						Mp3Counter++;
					else
						get_id3_tag( file->d_name, finfo.st_size );
				}
			
			} else if( finfo.st_mode & S_IFDIR ){				// is a directory

				if( bRecursive ){
					chdir( file->d_name );
					strcat( szRelativePath, file->d_name );		// append directory name
					strcat( szRelativePath, "/" );				// and a '/'

					mp3_scan_loop( bScanOnly );
																// remove directory name
					szRelativePath[(strlen(szRelativePath) - strlen(file->d_name)) - 1] = '\0';
					chdir("..");
				}
			}
		} 														// .. and . check
	} 															// while loop end
	
	closedir( dir );											// close the dir handle
	
	return END_LOOP;
}

// Check if the file is an MP3 file - based on file extension
bool is_mp3_file( char* pFileName ){

	char *ptr = rindex( pFileName, '.' );
		
	if(  ptr != NULL &&
		(strcmp( ptr, ".MP3" ) == 0 || strcmp( ptr, ".mp3" ) == 0 ||
		 strcmp( ptr, ".Mp3" ) == 0 || strcmp( ptr, ".mP3" ) == 0 ) )
		return TRUE;

	return FALSE;
}


// get the ID3tag from the file
void get_id3_tag( const char* pFileName, off_t size ){

	static char szTitle[31]      = {'\0'};
	static char szArtist[31]     = {'\0'};
	static char szAlbum[31]      = {'\0'};
	static char szYear[5]        = {'\0'};
	static char szPath[PATH_MAX] = {'\0'};

	if( bRelPath )
		strcpy( szPath, szRelativePath );
	else
		getcwd( szPath, PATH_MAX );
			
	if( bFsInfo )													// Save file size
		size_count( size );

	get_tags( pFileName, szTitle, szArtist, szAlbum, szYear );		// Try to get all tags

	if( szTitle[0] == '\0' && szArtist[0] == '\0' && szAlbum[0] == '\0' && szYear[0] == '\0' ){
		
		if( bUseFileName ){											// Use file name to get song infos			
			filename_to_field( pFileName, szTitle, szArtist, szAlbum, szYear );
				
		} else {													// print a warning message and return
			if( bVerbose )
				print_message( WARNING, "%s has no id3 Tag\n", pFileName );
		}
	}

	sql_insert( szTitle, szArtist, szAlbum, szYear, pFileName, szPath, size );
}


// Gets all necessary field from the mp3 file
void get_tags( const char *filename, char *title, char *artist, char *album, char *year ){

	ID3_Tag Version1;														// Create ID3v1 and ID3v2 object
	ID3_Tag Version2;
	ID3_Frame *Frame1;
	ID3_Frame *Frame2;

	/* empty each buffer */
	title[0]  = '\0';
	artist[0] = '\0';
	album[0]  = '\0';
	year[0]   = '\0';

	char TmpBuffer[31] = {'\0'};

	Version1.Link( filename, ID3TT_ID3V1 );									// Read ID3 info from input file
	Version2.Link( filename, ID3TT_ID3V2 );

	if( TagVersion & ID3v1 ){												// Get the track title from ID3v1
		Frame1 = Version1.Find( ID3FID_TITLE );
		if( Frame1 != NULL )
			Frame1->Field( ID3FN_TEXT ).Get( title, 31 );	
	}

	if( TagVersion & ID3v2 ){												// Get the track title from ID3v2
		Frame2 = Version2.Find( ID3FID_TITLE );
		if( Frame2 != NULL )
			Frame2->Field( ID3FN_TEXT ).Get( TmpBuffer, 31 );
	}
	
	CHECKBUFFERS( title, TmpBuffer, filename, 0 );							// Check the title

	if( TagVersion & ID3v1 ){												// Get the artist title from ID3v1
		Frame1 = Version1.Find( ID3FID_LEADARTIST );
		if( Frame1 != NULL )
			Frame1->Field( ID3FN_TEXT ).Get( artist, 31 );
	}
	
	if( TagVersion & ID3v2 ){												// Get the artist title from ID3v2
		Frame2 = Version2.Find( ID3FID_LEADARTIST );
		if( Frame2 != NULL )
			Frame2->Field( ID3FN_TEXT ).Get( TmpBuffer, 31 );
	}

	CHECKBUFFERS( artist, TmpBuffer, filename, 1 );							// Check the artist

	if( TagVersion & ID3v1 ){												// Get the album title from ID3v1
		Frame1 = Version1.Find( ID3FID_ALBUM );
		if( Frame1 != NULL )
			Frame1->Field( ID3FN_TEXT ).Get( album, 31 );
	}

	if( TagVersion & ID3v2 ){												// Get the album title from ID3v2
		Frame2 = Version2.Find( ID3FID_ALBUM );
		if( Frame2 != NULL )
			Frame2->Field( ID3FN_TEXT ).Get( TmpBuffer, 31 );
	}
	
	CHECKBUFFERS( album, TmpBuffer, filename, 2 );							// Check the album

	if( TagVersion & ID3v1 ){												// Get the year from ID3v1
		Frame1 = Version1.Find( ID3FID_YEAR );
		if( Frame1 != NULL )
			Frame1->Field( ID3FN_TEXT ).Get( year, 5 );
	}

	if( TagVersion & ID3v1 ){												// Get the year from ID3v1
		Frame2 = Version2.Find( ID3FID_YEAR );	
		if( Frame2 != NULL )
			Frame2->Field( ID3FN_TEXT ).Get( TmpBuffer, 5 );
	}
	
	CHECKBUFFERS( year, TmpBuffer, filename, 3 );							// Check the year
}


// query infos into db
void sql_insert( const char* Title, const char* Artist, const char* Album, const char* Year, const char* FileName, const char* Path, off_t Size ){

#ifdef __MYSQL
    #ifndef szQuery
	    static char szQuery[1024] = {'\0'};
	#endif
#endif

#ifdef __SQLITE
    #ifndef szQuery
	    static char szQuery[1024] = {'\0'};
	#endif
#endif

	switch( UseDB ){
		
#ifdef __MYSQL	
	case USE_MYSQL:
	
		snprintf( szQuery, 1024, "INSERT INTO %s( artist, title, album, year, filename, path, size ) VALUES ( \"%s\", \"%s\", \"%s\", \'%s\', \"%s\", \"%s\", \"%d\" )", pTabname, Artist, Title, Album, Year, FileName, Path, (int)Size );
		
		if ( mysql_query( DB_handle.mysql_handle, szQuery ) && bVerbose )
			print_message( WARNING, "%s\n", mysql_error( DB_handle.mysql_handle ) );
		break;
#endif		
		
#ifdef __SQLITE	
	case USE_SQLITE:
	
		snprintf( szQuery, 1024, "INSERT INTO %s VALUES ( NULL, \"%s\", \"%s\", \"%s\", \'%s\', \"%s\", \"%s\", \"%d\" )", pTabname, Artist, Title, Album, Year, FileName, Path, (int)Size );
		
		if( sqlite3_exec( DB_handle.sqlite_handle, szQuery, NULL, NULL, NULL )  != SQLITE_OK && bVerbose )
			print_message( WARNING, "%s\n", sqlite3_errmsg(DB_handle.sqlite_handle) );
		break;
#endif
	default:
		print_error( NO_DB_SELECTED );
		break;
	}
}

// Open a DB Connection
RETURNCODE OpenDBConnection(){
	
	switch( UseDB ){
		
#ifdef __MYSQL
	case USE_MYSQL:
		DB_handle.mysql_handle = mysql_init( NULL );
		
		if( !mysql_real_connect( DB_handle.mysql_handle, pHost, pUser, pPass, pDB, 0, NULL, 0 ) )
			return DBOPEN_ERROR;
		break;
#endif

#ifdef __SQLITE
	case USE_SQLITE:
		if( sqlite3_open( pFilename, &DB_handle.sqlite_handle ) != SQLITE_OK )
			return DBOPEN_ERROR;
		break;
#endif

	default:
		return DBUNKNOWN_ERROR;
		
	}
	
	return DBOPENED;
}


// Close the DB Connection
RETURNCODE CloseDBConnection(){

	switch( UseDB ){

#ifdef __MYSQL
	case USE_MYSQL: 								// Close MySQL connection
		
		mysql_close( DB_handle.mysql_handle );
		break;
#endif

#ifdef __SQLITE	
	case USE_SQLITE:								// Close SQLite connection

		sqlite3_close( DB_handle.sqlite_handle );
		break;
#endif
	
	default:
		return DBUNKNOWN_ERROR;
	}
	
	return DBCLOSED;
}


// if required create the standard table 
void create_table(){

#ifdef __MYSQL
    #ifndef szBuffer
	    char szBuffer[512];
	#endif
#endif

#ifdef __SQLITE
    #ifndef szBuffer
	    char szBuffer[512];
	#endif
#endif

	switch( UseDB ){

#ifdef __MYSQL
	case USE_MYSQL:
		
		sprintf( szBuffer, "CREATE TABLE IF NOT EXISTS %s ( id INT NOT NULL AUTO_INCREMENT PRIMARY KEY, artist VARCHAR(35) NULL, title VARCHAR(35) NULL, album VARCHAR(35) NULL, year VARCHAR(5) NULL, filename TEXT NULL, path TEXT NULL, size VARCHAR(20) NULL ) ENGINE = MYISAM", pTabname );

		if( mysql_query( DB_handle.mysql_handle, szBuffer ) ){
			print_message( ERROR, "%s\nUnable to continue\n", mysql_error( DB_handle.mysql_handle ) );
			CloseDBConnection();
			exit( 0 );
		}
		break;
#endif

#ifdef __SQLITE
	 case USE_SQLITE:

		sprintf( szBuffer, "CREATE TABLE IF NOT EXISTS %s ( id INTEGER PRIMARY KEY, artist VARCHAR(35), title VARCHAR(35), album VARCHAR(35), year VARCHAR(5), filename TEXT, path TEXT, size VARCHAR(20) )", pTabname );

	 	if( sqlite3_exec( DB_handle.sqlite_handle, szBuffer, NULL, NULL, NULL ) != SQLITE_OK ){
			
			print_message( ERROR, "%s\nUnable to continue", sqlite3_errmsg( DB_handle.sqlite_handle ) );
			CloseDBConnection();
			exit( 0 );
		}
		break;
#endif

	default:
		break;
	 }
}


// Sum the size of all mp3 files 
void size_count( off_t Size ){
	
	static long long unsigned int TotalSize = 0;
	const char *p[] = { "B", "KB", "MB", "GB", "TB" };
	int index = 0;

	if( Size >= 0 ){
		TotalSize += Size;
	} else {

		char buff[512];
		float temp = (float)TotalSize;
		while( temp > 1024 ){
			index++;
			temp /= 1024;
		}
		if( index > 4 ) index = 4;
		sprintf( buff, "Total files size: %.1f %s\n", temp, p[index] );
		print_message( STATUS, buff );
	}
}


// Read from filename all information available
void filename_to_field( const char *pFileName, char *szTitle, char *szArtist, char *szAlbum, char *szYear ){

	int i, len, last = strlen(pFileNameFormat) - 1;
	char Buffer[256];
	bool stop = FALSE;
	char *lastC;										// end field pointer, point to the last char of the string to copy
	char *firstC = Buffer;								// start field pointer, point to the first char of the string to copy
	
	strcpy( Buffer, pFileName );						// copy the filename into an internal buffer
	replace_char( Buffer );								// replace all provided characters with space
	
	for( i = 0; i < last && !stop; i++ ){

		// pFileNameFormat[last] is the field separator
		if( (lastC = index( firstC, pFileNameFormat[last] )) == NULL ) {
		
			lastC = &firstC[strlen(firstC)-4];			// get the end field pointer
			stop = TRUE;									// force to be the last loop
		}
		
		len = (lastC - firstC) / sizeof(char);			// get the length of the field found
		while( firstC[0] == 0x20 ){						// delete all spaces at the beginning of the string
			
			firstC ++;
			len--;
		}

		// Una volta che ho controllato il tutto posso fare la copia
		switch( pFileNameFormat[i] ){
				
				case 'A':
					strncpy( szArtist, firstC, len>=31?30:len );
					if( szArtist[len-1] == 0x20 )					// remove, if present, a space at the end of the string
						szArtist[len-1] = '\0';
					else
						szArtist[len] = '\0';
					break;
				case 'M':
					strncpy( szAlbum,  firstC, len>=31?30:len );
					if( szAlbum[len-1] == 0x20 )					// remove, if present, a space at the end of the string
						szAlbum[len-1] = '\0';
					else
						szAlbum[len] = '\0';
					break;
				case 'T':
					strncpy( szTitle,  firstC, len>=31?30:len );
					if( szTitle[len-1] == 0x20 )					// remove, if present, a space at the end of the string
						szTitle[len-1] = '\0';
					else
						szTitle[len] = '\0';
					break;
				case 'Y':
					strncpy( szYear,   firstC, len>=5?4:len );
					szYear[5] = '\0';
					break;
				default:
					CloseDBConnection();
					print_error( BAD_FORMAT );
		}
		
		// set the pointer to first position to actual last + 1
		firstC = lastC + 1;
	}
}



// Replace all SpaceChar in str with the space character
void replace_char( char *str ){
	
	if( bUseSpaceChar ){
		unsigned int  i;
		
		for( i = 0; i < strlen(str); i++ ){
			if( strchr( pSpaceChar, str[i] ) != NULL )
				str[i] = 0x20;								// replace the SpaceChar with space ( " " )
		}
	}
}


// Prompt to user the two strings found and ask to make a choice
// Return code 0 = user chose field1, 1 = user chose field2
int chose_field( const char *filename, const char *field1, const char *field2, int fieldname ){
	
	int res = 0;
	const char *fieldtag[] = { "title", "artist", "album", "year" };
	printf( "\nInconsistencies found in tag '%s' for '%s' file\n\t1. %s\n\t2. %s\nwhich one should be used [1/2]: ", 
																			fieldtag[fieldname], filename, field1, field2 );
	
	if( (char)getchar() == '2' )
		res = 1;
	getchar();
		
	return res;
}


// Print a generic message to standard output
// procedure can handle up to seven input parameters
void print_message( MSGCODE code, const char* szFormat, ... ) {

	va_list ap;
	int i;
	void* args[5];
	char szBuff[256];
	
	const char *pErroMsg;
	const char *pStatMsg;
	const char *pOutpMsg;
	const char *pWarnMsg;

	if( bUseColor ){
		pErroMsg = "\x1B[0;31m ERROR:\x1B[1;37m ";
		pStatMsg = "\x1B[0;32m STATUS:\x1B[1;37m ";
		pOutpMsg = "\x1B[0;34m OUTPUT:\x1B[1;37m ";
		pWarnMsg = "\x1B[0;33m WARNING:\x1B[1;37m ";
	} else {
		pErroMsg = " ERROR: ";
		pStatMsg = " STATUS: ";
		pOutpMsg = " OUTPUT: ";
		pWarnMsg = " WARNING: ";
	}

	szBuff[0] = '\0';

	switch(code){

	case ERROR:
		strcat(szBuff, pErroMsg);
		break;

	case STATUS:
		strcat(szBuff, pStatMsg);
		break;

	case OUTPUT:
		strcat(szBuff, pOutpMsg);
		break;

	case WARNING:
		strcat(szBuff, pWarnMsg);
		break;
	}
	strcat(szBuff, szFormat);

	va_start( ap, szFormat );
	for( i=0; i<5; i++ )
		args[i] = va_arg( ap, void* );
	va_end( ap );

	printf( szBuff, args[0], args[1], args[2], args[3], args[4] );
}


// Print the right error message to standard output
void print_error( RETURNCODE code ){

	const char *pErrorMsg;
	
	if( bUseColor )
		pErrorMsg = "\x1B[0;31m ERROR:\x1B[1;37m";
	else
		pErrorMsg = " ERROR:";
	
	switch( code ){


	case NO_DB_SELECTED:
		printf("%s no database selected, please use one.\n", pErrorMsg);
		break;

	case NO_FLAG_SPECIFIED:
		printf("%s no flags specified, cannot continue.\n", pErrorMsg);
		break;
	
	case TOO_MANY_DB:
		printf("%s Too many Database selected, please use only one.\n", pErrorMsg);
		break;

	case MYSQL_PARAM_ERROR:
		printf("%s MySQL invalid parameters, please see the help menu.\n", pErrorMsg);
		break;

	case SQLITE_PARAM_ERROR:
		printf("%s SQLite invalid parameters, please see the help menu.\n", pErrorMsg);
		break;

	case CREATE_TABLE_ERROR:
		printf("%s Create table invalid parameter, please see the help menu.\n", pErrorMsg);
		break;

	case UNKNOW_PARAM:
		printf("%s: invalid option: %s\nTry `%s --help' for more information.\n", pProgramName, pError, pProgramName);
		break;

	case OPENDIR_ERROR:
		printf("%s While opening source directory.\n", pErrorMsg);
		break;
	
	case VERSION_USAGE_OUTPUTTED:
		// do nothing
		break;
		
	case BAD_FORMAT:
		printf( "%s Bad filenameformat entered: %s\n", pErrorMsg, pFileNameFormat );
		break;
		
	case FNFORMAT_PARAM_ERROR:
		printf("%s Filename invalid parameters, please see the help menu.\n", pErrorMsg);
		break;
		
	case SPACECHAR_PARAM_ERROR:
		printf("%s Space character invalid parameters, please see the help menu.\n", pErrorMsg);
		break;
		
	case DBOPEN_ERROR:
		switch( UseDB ){
#ifdef __MYSQL
		case USE_MYSQL:
			printf( "%s %s\n", pErrorMsg, mysql_error( DB_handle.mysql_handle ) );
			break;
#endif
#ifdef __SQLITE
		case USE_SQLITE:
			printf( "%s Unable to open SQLite database file.\n", pErrorMsg );
			break;
#endif
		default:
			break;
		}
		break;

	case DBUNKNOWN_ERROR:
		printf( "%s DB no proper selected.\n", pErrorMsg );
		break;

	default:
		printf("%s Unknown error message recived, exit.\n", pErrorMsg);
		break;
	}
	
	printf( "\x1B[0m" );								// force the reset of the shell color scheme
	exit(0);
}


// check input parameter function
RETURNCODE check_flag( int argc, const char* argv[] ){

	int i, j = 0, db = 0;

// if no parameter output error message
	if( argc < 2 )
		return NO_FLAG_SPECIFIED;

// Init table name by default	
	pTabname = pTBName;

// scan all input parameter
	for( i = 1; i < argc; i++ ){
		
		if( !strcmp( argv[i], "--version" ) || !strcmp( argv[i], "-v" ) ){

			printf( "%s%s", COPYRIGHT, VERSION );
			return VERSION_USAGE_OUTPUTTED;

		} else if( !strcmp( argv[i], "--help" ) || !strcmp( argv[i], "-h" ) ){

			printf( "%s%s", USAGE, COPYRIGHT );
			return VERSION_USAGE_OUTPUTTED;

		} else if( !strcmp( argv[i], "--recursive" ) || !strcmp( argv[i], "-r" ) ){

			bRecursive		= TRUE;

		} else if( !strcmp( argv[i], "--verbose" ) || !strcmp( argv[i], "-V" ) ){

			bVerbose		= TRUE;

		} else if( !strcmp( argv[i], "--fsize" ) || !strcmp( argv[i], "-f" ) ){

			bFsInfo			= TRUE;

		} else if( !strcmp( argv[i], "--usecolor" ) || !strcmp( argv[i], "-u" ) ){

			bUseColor		= TRUE;

		} else if( !strcmp( argv[i], "--relativepath" ) || !strcmp( argv[i], "-p" ) ){

			bRelPath		= TRUE;

		} else if( !strcmp( argv[i], "--interactive" ) || !strcmp( argv[i], "-i" ) ){

			bInteractive	= TRUE;

		} else if( !strcmp( argv[i], "--ID3V1" ) || !strcmp( argv[i], "-1" ) ){

			TagVersion 	   |= ID3v1;

		} else if( !strcmp( argv[i], "--ID3V2" ) || !strcmp( argv[i], "-2" ) ){

			TagVersion 	   |= ID3v2;

		} else if( !strcmp( argv[i], "--usefilename" ) || !strcmp( argv[i], "-n" ) ){
			
			if( (i+1) >= (argc-1) || argv[i+1][0] == '-' )
				return FNFORMAT_PARAM_ERROR;

			pFileNameFormat = argv[++i];
			bUseFileName	= TRUE;

		} else if( !strcmp( argv[i], "--spacechar" ) || !strcmp( argv[i], "-s" ) ){

			if( (i+1) >= (argc-1) || argv[i+1][0] == '-' )
				return SPACECHAR_PARAM_ERROR;

			pSpaceChar = argv[++i];
			bUseSpaceChar	= TRUE;

		} else if( !strcmp( argv[i], "--createtab" ) || !strcmp( argv[i], "-c" ) ){
	
			if( (i+1) < (argc-1) && argv[i+1][0] != '-' )
				pTabname = argv[++i];

			bCreateTab = TRUE;

			// usage --createtab [TABNAME]

		} else if( !strcmp( argv[i], "--sqlite" ) || !strcmp( argv[i], "-l" ) ){
	
			if( (i+1) >= (argc-1) || argv[i+1][0] == '-' )
				return SQLITE_PARAM_ERROR;

			pFilename = argv[++i];
			UseDB = USE_SQLITE;
			db++;
			
			// usage --sqlite FILENAME

		} else if( !strcmp( argv[i], "--mysql" ) || !strcmp( argv[i], "-m" ) ){
			
			if( (i+3) >= (argc-1) || argv[i+1][0] == '-' || argv[i+2][0] == '-' || argv[i+3][0] == '-' )
				return MYSQL_PARAM_ERROR;
			
			pHost = argv[++i];
			pUser = argv[++i];
		// if no '-' can be the password AND there is at least one param after the DATABASE this is the password
			if( argv[i+2][0] != '-' && (i+2) < (argc-1) )						// the password isn't mandatory
				pPass = argv[++i];
			else
				pPass = NULL;

			pDB   = argv[++i];
			UseDB = USE_MYSQL;
			db++;
			
			// usage --mysql HOST USER [PASSWORD] DATABASE

		} else if( argv[i][0] == '-' && argv[i][1] != '-' ){

			while( argv[i][++j] ){
				
				switch( argv[i][j] ){
				case 'V':
					bVerbose   = TRUE;
					break;
				case 'r':
					bRecursive = TRUE;
					break;
				case 'u':
					bUseColor  = TRUE;
					break;
				case 'p':
					bRelPath   = TRUE;
					break;
				case 'f':
					bFsInfo    = TRUE;
					break;
				case 'i':
					bInteractive = TRUE;
					break;
				case '1':
					TagVersion |= ID3v1;
					break;
				case '2':
					TagVersion |= ID3v2;
					break;
				default:
					pError = argv[i];
					return UNKNOW_PARAM;
				}
			}

		} else if( i != (argc - 1) ){
		
			pError = argv[i];
			return UNKNOW_PARAM;

		} else {

			pPath = argv[i];
		}
	}

// check db variable
	if( db <= 0 ) return NO_DB_SELECTED;
	if( db >= 2 ) return TOO_MANY_DB;
// by default get info from ID3v1 and ID3v2
	if( !TagVersion ) TagVersion = ID3v1 | ID3v2;
	
	return PARAM_OK;
}

