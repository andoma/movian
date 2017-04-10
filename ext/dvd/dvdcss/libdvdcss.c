/* libdvdcss.c: DVD reading library.
 *
 * Authors: Stéphane Borel <stef@via.ecp.fr>
 *          Sam Hocevar <sam@zoy.org>
 *          Håkan Hjort <d95hjort@dtek.chalmers.se>
 *
 * Copyright (C) 1998-2008 VideoLAN
 *
 * libdvdcss is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * libdvdcss is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with libdvdcss; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * \mainpage libdvdcss developer documentation
 *
 * \section intro Introduction
 *
 * \e libdvdcss is a simple library designed for accessing DVDs like a block
 * device without having to bother about the decryption. The important features
 * are:
 * \li portability: Currently supported platforms are GNU/Linux, FreeBSD,
 *     NetBSD, OpenBSD, Haiku, Mac OS X, Solaris, QNX, OS/2, and Windows
 *     2000 or later.
 * \li adaptability: Unlike most similar projects, libdvdcss does not require
 *     the region of your drive to be set and will try its best to read from
 *     the disc even in the case of a region mismatch.
 * \li simplicity: A DVD player can be built around the \e libdvdcss API using
 *     no more than 4 or 5 library calls.
 *
 * \e libdvdcss is free software, released under the GNU General Public License.
 * This ensures that \e libdvdcss remains free and used only with free
 * software.
 *
 * \section api The libdvdcss API
 *
 * The complete \e libdvdcss programming interface is documented in the
 * dvdcss.h file.
 *
 * \section env Environment variables
 *
 * Some environment variables can be used to change the behavior of
 * \e libdvdcss without having to modify the program which uses it. These
 * variables are:
 *
 * \li \b DVDCSS_VERBOSE: Sets the verbosity level.
 *     - \c 0 outputs no messages at all.
 *     - \c 1 outputs error messages to stderr.
 *     - \c 2 outputs error messages and debug messages to stderr.
 *
 * \li \b DVDCSS_METHOD: Sets the authentication and decryption method
 *     that \e libdvdcss will use to read scrambled discs. Can be one
 *     of \c title, \c key or \c disc.
 *     - \c key is the default method. \e libdvdcss will use a set of
 *       calculated player keys to try and get the disc key. This can fail
 *       if the drive does not recognize any of the player keys.
 *     - \c disc is a fallback method when \c key has failed. Instead of
 *       using player keys, \e libdvdcss will crack the disc key using
 *       a brute force algorithm. This process is CPU intensive and requires
 *       64 MB of memory to store temporary data.
 *     - \c title is the fallback when all other methods have failed. It does
 *       not rely on a key exchange with the DVD drive, but rather uses a
 *       crypto attack to guess the title key. In rare cases this may fail
 *       because there is not enough encrypted data on the disc to perform
 *       a statistical attack, but on the other hand it is the only way to
 *       decrypt a DVD stored on a hard disc, or a DVD with the wrong region
 *       on an RPC2 drive.
 *
 * \li \b DVDCSS_RAW_DEVICE: Specify the raw device to use. Exact usage will
 *     depend on your operating system, the Linux utility to set up raw devices
 *     is \c raw(8) for instance. Please note that on most operating systems,
 *     using a raw device requires highly aligned buffers: Linux requires a
 *     2048 bytes alignment (which is the size of a DVD sector).
 *
 * \li \b DVDCSS_CACHE: Specify a directory in which to cache title key
 *     values. This will speed up descrambling of DVDs which are in the
 *     cache. The DVDCSS_CACHE directory is created if it does not exist,
 *     and a subdirectory is created named after the DVD's title or
 *     manufacturing date. If DVDCSS_CACHE is not set or is empty, \e libdvdcss
 *     will use the default value which is "${HOME}/.dvdcss/" under Unix and
 *     "C:\Documents and Settings\$USER\Application Data\dvdcss\" under Win32.
 *     The special value "off" disables caching.
 */

/*
 * Preamble
 */
#include "config.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_SYS_PARAM_H
#   include <sys/param.h>
#endif
#ifdef HAVE_PWD_H
#   include <pwd.h>
#endif
#include <fcntl.h>
#include <errno.h>

#ifdef HAVE_UNISTD_H
#   include <unistd.h>
#endif

#ifdef _WIN32
#   include <windows.h>
#   include <shlobj.h>
#endif

#include "dvdcss/dvdcss.h"

#include "common.h"
#include "css.h"
#include "libdvdcss.h"
#include "ioctl.h"
#include "device.h"

#ifdef HAVE_BROKEN_MKDIR
#include <direct.h>
#define mkdir(a, b) _mkdir(a)
#endif

#define CACHE_TAG_NAME "CACHEDIR.TAG"

#define STRING_KEY_SIZE (DVD_KEY_SIZE * 2)
#define INTERESTING_SECTOR 16
#define DISC_TITLE_OFFSET  40
#define DISC_TITLE_LENGTH  32
#define MANUFACTURING_DATE_OFFSET 813
#define MANUFACTURING_DATE_LENGTH  16


static dvdcss_t dvdcss_open_common ( const char *psz_target, void *p_stream,
                                     dvdcss_stream_cb *p_stream_cb );
static void set_verbosity( dvdcss_t dvdcss )
{
    const char *psz_verbose = getenv( "DVDCSS_VERBOSE" );

    dvdcss->b_debug  = 0;
    dvdcss->b_errors = 0;

    if( psz_verbose != NULL )
    {
        int i = atoi( psz_verbose );

        if( i >= 2 )
            dvdcss->b_debug  = 1;
        if( i >= 1 )
            dvdcss->b_errors = 1;
    }
}

static int set_access_method( dvdcss_t dvdcss )
{
    const char *psz_method = getenv( "DVDCSS_METHOD" );

    if( !psz_method )
        return 0;

    if( !strncmp( psz_method, "key", 4 ) )
    {
        dvdcss->i_method = DVDCSS_METHOD_KEY;
    }
    else if( !strncmp( psz_method, "disc", 5 ) )
    {
        dvdcss->i_method = DVDCSS_METHOD_DISC;
    }
    else if( !strncmp( psz_method, "title", 5 ) )
    {
        dvdcss->i_method = DVDCSS_METHOD_TITLE;
    }
    else
    {
        print_error( dvdcss, "unknown decryption method %s, please choose "
                     "from 'title', 'key' or 'disc'", psz_method );
        return -1;
    }
    return 0;
}

static int set_cache_directory( dvdcss_t dvdcss )
{
    char *psz_cache = getenv( "DVDCSS_CACHE" );

    if( psz_cache && !strcmp( psz_cache, "off" ) )
    {
        return -1;
    }

    if( psz_cache == NULL || psz_cache[0] == '\0' )
    {
#ifdef _WIN32
        char psz_home[PATH_MAX];

        /* Cache our keys in
         * C:\Documents and Settings\$USER\Application Data\dvdcss\ */
        if (SHGetFolderPathA (NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE,
                              NULL, SHGFP_TYPE_CURRENT, psz_home ) == S_OK)
        {
            snprintf( dvdcss->psz_cachefile, PATH_MAX, "%s\\dvdcss", psz_home );
            dvdcss->psz_cachefile[PATH_MAX - 1] = '\0';
            psz_cache = dvdcss->psz_cachefile;
        }
#else
#ifdef __ANDROID__
        /* $HOME is not writable on __ANDROID__ so we have to create a custom
         * directory in userland */
        char *psz_home = "/sdcard/Android/data/org.videolan.dvdcss";

        int i_ret = mkdir( psz_home, 0755 );
        if( i_ret < 0 && errno != EEXIST )
        {
            print_error( dvdcss, "failed creating home directory" );
            psz_home = NULL;
        }
#else
        char *psz_home = NULL;
#ifdef HAVE_PWD_H
        struct passwd *p_pwd;

        /* Try looking in password file for home dir. */
        p_pwd = getpwuid(getuid());
        if( p_pwd )
        {
            psz_home = p_pwd->pw_dir;
        }
#endif /* HAVE_PWD_H */

#endif /* __ANDROID__ */

        if( psz_home == NULL )
        {
            psz_home = getenv( "HOME" );
        }

        /* Cache our keys in ${HOME}/.dvdcss/ */
        if( psz_home )
        {
            int home_pos = 0;

#ifdef __OS2__
            if( *psz_home == '/' || *psz_home == '\\')
            {
                const char *psz_unixroot = getenv("UNIXROOT");

                if( psz_unixroot &&
                    psz_unixroot[0] &&
                    psz_unixroot[1] == ':'  &&
                    psz_unixroot[2] == '\0')
                {
                    strcpy( dvdcss->psz_cachefile, psz_unixroot );
                    home_pos = 2;
                }
            }
#endif /* __OS2__ */
            snprintf( dvdcss->psz_cachefile + home_pos, PATH_MAX - home_pos,
                      "%s/.dvdcss", psz_home );
            dvdcss->psz_cachefile[PATH_MAX - 1] = '\0';
            psz_cache = dvdcss->psz_cachefile;
        }
#endif /* ! defined( _WIN32 ) */
    }

    /* Check that there is enough space for the cache directory path and the
     * block filename. The +1s are path separators. */
    if( psz_cache && strlen( psz_cache ) + 1 + DISC_TITLE_LENGTH + 1 +
        MANUFACTURING_DATE_LENGTH + 1 + STRING_KEY_SIZE + 1 +
        sizeof(CACHE_TAG_NAME) > PATH_MAX )
    {
        print_error( dvdcss, "cache directory name is too long" );
        return -1;
    }
    return 0;
}

static int init_cache_dir( dvdcss_t dvdcss )
{
    static const char psz_tag[] =
        "Signature: 8a477f597d28d172789f06886806bc55\r\n"
        "# This file is a cache directory tag created by libdvdcss.\r\n"
        "# For information about cache directory tags, see:\r\n"
        "#   http://www.brynosaurus.com/cachedir/\r\n";
    char psz_tagfile[PATH_MAX];
    int i_fd, i_ret;

    i_ret = mkdir( dvdcss->psz_cachefile, 0755 );
    if( i_ret < 0 && errno != EEXIST )
    {
        print_error( dvdcss, "failed creating cache directory" );
        dvdcss->psz_cachefile[0] = '\0';
        return -1;
    }

    sprintf( psz_tagfile, "%s/" CACHE_TAG_NAME, dvdcss->psz_cachefile );
    i_fd = open( psz_tagfile, O_RDWR|O_CREAT, 0644 );
    if( i_fd >= 0 )
    {
        ssize_t len = strlen(psz_tag);
        if( write( i_fd, psz_tag, len ) < len )
        {
            print_error( dvdcss,
                         "Error writing cache directory tag, continuing..\n" );
        }
        close( i_fd );
    }
    return 0;
}

static void create_cache_subdir( dvdcss_t dvdcss )
{
    uint8_t p_sector[DVDCSS_BLOCK_SIZE];
    char psz_key[STRING_KEY_SIZE + 1];
    char *psz_title;
    uint8_t *psz_serial;
    int i, i_ret;

    /* We read sector 0. If it starts with 0x000001ba (BE), we are
     * reading a VOB file, and we should not cache anything. */

    i_ret = dvdcss->pf_seek( dvdcss, 0 );
    if( i_ret != 0 )
    {
        goto error;
    }

    i_ret = dvdcss->pf_read( dvdcss, p_sector, 1 );
    if( i_ret != 1 )
    {
        goto error;
    }

    if( p_sector[0] == 0x00 && p_sector[1] == 0x00
        && p_sector[2] == 0x01 && p_sector[3] == 0xba )
    {
        goto error;
    }

    /* The data we are looking for is at sector 16 (32768 bytes):
     *  - offset 40: disc title (32 uppercase chars)
     *  - offset 813: manufacturing date + serial no (16 digits) */

    i_ret = dvdcss->pf_seek( dvdcss, INTERESTING_SECTOR );
    if( i_ret != INTERESTING_SECTOR )
    {
        goto error;
    }

    i_ret = dvdcss->pf_read( dvdcss, p_sector, 1 );
    if( i_ret != 1 )
    {
        goto error;
    }

    /* Get the disc title */
    psz_title = (char *)p_sector + DISC_TITLE_OFFSET;
    psz_title[DISC_TITLE_LENGTH] = '\0';

    for( i = 0; i < DISC_TITLE_LENGTH; i++ )
    {
        if( psz_title[i] <= ' ' )
        {
            psz_title[i] = '\0';
            break;
        }
        else if( psz_title[i] == '/' || psz_title[i] == '\\' )
        {
            psz_title[i] = '-';
        }
    }

    /* Get the date + serial */
    psz_serial = p_sector + MANUFACTURING_DATE_OFFSET;
    psz_serial[MANUFACTURING_DATE_LENGTH] = '\0';

    /* Check that all characters are digits, otherwise convert. */
    for( i = 0 ; i < MANUFACTURING_DATE_LENGTH ; i++ )
    {
        if( psz_serial[i] < '0' || psz_serial[i] > '9' )
        {
            char psz_tmp[MANUFACTURING_DATE_LENGTH + 1];
            sprintf( psz_tmp,
                     "%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x",
                     psz_serial[0], psz_serial[1], psz_serial[2],
                     psz_serial[3], psz_serial[4], psz_serial[5],
                     psz_serial[6], psz_serial[7] );
            memcpy( psz_serial, psz_tmp, MANUFACTURING_DATE_LENGTH );
            break;
        }
    }

    /* Get disk key, since some discs have the same title, manufacturing
     * date and serial number, but different keys. */
    if( dvdcss->b_scrambled )
    {
        for( i = 0; i < DVD_KEY_SIZE; i++ )
        {
            sprintf( &psz_key[i * 2], "%.2x", dvdcss->css.p_disc_key[i] );
        }
        psz_key[STRING_KEY_SIZE] = '\0';
    }
    else
    {
        psz_key[0] = 0;
    }

    /* We have a disc name or ID, we can create the cache subdirectory. */
    i = sprintf( dvdcss->psz_cachefile, "%s/%s-%s-%s",
                 dvdcss->psz_cachefile, psz_title, psz_serial, psz_key );
    i_ret = mkdir( dvdcss->psz_cachefile, 0755 );
    if( i_ret < 0 && errno != EEXIST )
    {
        print_error( dvdcss, "failed creating cache subdirectory" );
        goto error;
    }
    i += sprintf( dvdcss->psz_cachefile + i, "/");

    /* Pointer to the filename we will use. */
    dvdcss->psz_block = dvdcss->psz_cachefile + i;

    print_debug( dvdcss, "Content Scrambling System (CSS) key cache dir: %s",
                 dvdcss->psz_cachefile );
    return;

error:
    dvdcss->psz_cachefile[0] = '\0';
}

static void init_cache( dvdcss_t dvdcss )
{
    /* Set CSS key cache directory. */
    int i_ret = set_cache_directory( dvdcss );
    if ( i_ret < 0 )
    {
        return;
    }

    /* If the cache is enabled, initialize the cache directory. */
    i_ret = init_cache_dir( dvdcss );
    if ( i_ret < 0 )
    {
        return;
    }

    /* If the cache is enabled, create a DVD-specific subdirectory. */
    create_cache_subdir( dvdcss );
}

/**
 * \brief Open a DVD device or directory and return a dvdcss instance.
 *
 * \param psz_target a string containing the target name, for instance
 *        "/dev/hdc" or "E:"
 * \return a handle to a dvdcss instance or NULL on error.
 *
 * Initialize the \e libdvdcss library, open the requested DVD device or
 * directory, and return a handle to be used for all subsequent \e libdvdcss
 * calls. \e libdvdcss checks whether ioctls can be performed on the disc,
 * and when possible, the disc key is retrieved.
 */
LIBDVDCSS_EXPORT dvdcss_t dvdcss_open ( const char *psz_target )
{
    return dvdcss_open_common( psz_target, NULL, NULL );
}

/**
 * \brief Open a DVD device using dvdcss_stream_cb.
 *
 * \param p_stream a private handle used by p_stream_cb
 * \param p_stream_cb a struct containing seek and read functions
 * \return a handle to a dvdcss instance or NULL on error.
 *
 * \see dvdcss_open()
 */
LIBDVDCSS_EXPORT dvdcss_t dvdcss_open_stream ( void *p_stream,
                                               dvdcss_stream_cb *p_stream_cb )
{
    return dvdcss_open_common( NULL, p_stream, p_stream_cb );
}

static dvdcss_t dvdcss_open_common ( const char *psz_target, void *p_stream,
                                     dvdcss_stream_cb *p_stream_cb )
{
    int i_ret;

    /* Allocate the library structure. */
    dvdcss_t dvdcss = malloc( sizeof( *dvdcss ) );
    if( dvdcss == NULL )
    {
        return NULL;
    }

    if( psz_target == NULL &&
      ( p_stream == NULL || p_stream_cb == NULL ) )
    {
        goto error;
    }

    /* Initialize structure with default values. */
    dvdcss->i_fd = -1;
    dvdcss->i_pos = 0;
    dvdcss->p_titles = NULL;
    dvdcss->psz_device = psz_target ? strdup( psz_target ) : NULL;
    dvdcss->psz_error = "no error";
    dvdcss->i_method = DVDCSS_METHOD_KEY;
    dvdcss->psz_cachefile[0] = '\0';

    dvdcss->p_stream = p_stream;
    dvdcss->p_stream_cb = p_stream_cb;

    /* Set library verbosity from DVDCSS_VERBOSE environment variable. */
    set_verbosity( dvdcss );

    /* Set DVD access method from DVDCSS_METHOD environment variable. */
    if( set_access_method( dvdcss ) < 0 )
    {
        goto error;
    }

    /* Open device. */
    dvdcss_check_device( dvdcss );
    i_ret = dvdcss_open_device( dvdcss );
    if( i_ret < 0 )
    {
        goto error;
    }

    dvdcss->b_scrambled = 1; /* Assume the worst */
    dvdcss->b_ioctls = dvdcss_use_ioctls( dvdcss );

    if( dvdcss->b_ioctls )
    {
        i_ret = dvdcss_test( dvdcss );

        if( i_ret == -3 )
        {
            print_debug( dvdcss, "scrambled disc on a region-free RPC-II "
                                 "drive: possible failure, but continuing "
                                 "anyway" );
        }
        else if( i_ret < 0 )
        {
            /* Disable the CSS ioctls and hope that it works? */
            print_debug( dvdcss,
                         "could not check whether the disc was scrambled" );
            dvdcss->b_ioctls = 0;
        }
        else
        {
            print_debug( dvdcss, i_ret ? "disc is scrambled"
                                       : "disc is unscrambled" );
            dvdcss->b_scrambled = i_ret;
        }
    }

    memset( dvdcss->css.p_disc_key, 0, DVD_KEY_SIZE );
    /* If disc is CSS protected and the ioctls work, authenticate the drive */
    if( dvdcss->b_scrambled && dvdcss->b_ioctls )
    {
        i_ret = dvdcss_disckey( dvdcss );

        if( i_ret < 0 )
        {
            print_debug( dvdcss, "could not get disc key" );
        }
    }

    init_cache( dvdcss );

    /* Seek to the beginning, just for safety. */
    dvdcss->pf_seek( dvdcss, 0 );

    return dvdcss;

error:
    free( dvdcss->psz_device );
    free( dvdcss );
    return NULL;
}

/**
 * \brief Return a string containing the last error that occurred in the
 *        given \e libdvdcss instance.
 *
 * \param dvdcss a \e libdvdcss instance
 * \return a NULL-terminated string containing the last error message.
 *
 * Return a string with the last error message produced by \e libdvdcss.
 * Useful to conveniently format error messages in external applications.
 */
LIBDVDCSS_EXPORT const char * dvdcss_error ( const dvdcss_t dvdcss )
{
    return dvdcss->psz_error;
}

/**
 * \brief Seek in the disc and change the current key if requested.
 *
 * \param dvdcss a \e libdvdcss instance
 * \param i_blocks an absolute block offset to seek to
 * \param i_flags #DVDCSS_NOFLAGS, optionally ORed with one of #DVDCSS_SEEK_KEY
 *        or #DVDCSS_SEEK_MPEG
 * \return the new position in blocks or a negative value in case an error
 *         happened.
 *
 * This function seeks to the requested position, in logical blocks.
 *
 * You typically set \p i_flags to #DVDCSS_NOFLAGS when seeking in a .IFO.
 *
 * If #DVDCSS_SEEK_MPEG is specified in \p i_flags and if \e libdvdcss finds it
 * reasonable to do so (i.e., if the dvdcss method is not "title"), the current
 * title key will be checked and a new one will be calculated if necessary.
 * This flag is typically used when reading data from a .VOB file.
 *
 * If #DVDCSS_SEEK_KEY is specified, the title key will always be checked,
 * even with the "title" method. This flag is typically used when seeking
 * in a new title.
 */
LIBDVDCSS_EXPORT int dvdcss_seek ( dvdcss_t dvdcss, int i_blocks, int i_flags )
{
    /* title cracking method is too slow to be used at each seek */
    if( ( ( i_flags & DVDCSS_SEEK_MPEG )
             && ( dvdcss->i_method != DVDCSS_METHOD_TITLE ) )
       || ( i_flags & DVDCSS_SEEK_KEY ) )
    {
        /* check the title key */
        if( dvdcss_title( dvdcss, i_blocks ) )
        {
            return -1;
        }
    }

    return dvdcss->pf_seek( dvdcss, i_blocks );
}

/**
 * \brief Read from the disc and decrypt data if requested.
 *
 * \param dvdcss a \e libdvdcss instance
 * \param p_buffer a buffer that will contain the data read from the disc
 * \param i_blocks the amount of blocks to read
 * \param i_flags #DVDCSS_NOFLAGS, optionally ORed with #DVDCSS_READ_DECRYPT
 * \return the amount of blocks read or a negative value in case an
 *         error happened.
 *
 * Read \p i_blocks logical blocks from the DVD.
 *
 * You typically set \p i_flags to #DVDCSS_NOFLAGS when reading data from a
 * .IFO file on the DVD.
 *
 * If #DVDCSS_READ_DECRYPT is specified in \p i_flags, dvdcss_read() will
 * automatically decrypt scrambled sectors. This flag is typically used when
 * reading data from a .VOB file on the DVD. It has no effect on unscrambled
 * discs or unscrambled sectors and can be safely used on those.
 *
 * \warning dvdcss_read() expects to be able to write \p i_blocks *
 *          #DVDCSS_BLOCK_SIZE bytes into \p p_buffer.
 */
LIBDVDCSS_EXPORT int dvdcss_read ( dvdcss_t dvdcss, void *p_buffer,
                                          int i_blocks,
                                          int i_flags )
{
    uint8_t *_p_buffer = p_buffer;
    int i_ret, i_index;

    i_ret = dvdcss->pf_read( dvdcss, _p_buffer, i_blocks );

    if( i_ret <= 0
         || !dvdcss->b_scrambled
         || !(i_flags & DVDCSS_READ_DECRYPT) )
    {
        return i_ret;
    }

    if( ! memcmp( dvdcss->css.p_title_key, "\0\0\0\0\0", 5 ) )
    {
        /* For what we believe is an unencrypted title,
         * check that there are no encrypted blocks */
        for( i_index = i_ret; i_index; i_index-- )
        {
            if( _p_buffer[0x14] & 0x30 )
            {
                print_error( dvdcss, "no key but found encrypted block" );
                /* Only return the initial range of unscrambled blocks? */
                /* or fail completely? return 0; */
                break;
            }
            _p_buffer = _p_buffer + DVDCSS_BLOCK_SIZE;
        }
    }
    else
    {
        /* Decrypt the blocks we managed to read */
        for( i_index = i_ret; i_index; i_index-- )
        {
            dvdcss_unscramble( dvdcss->css.p_title_key, _p_buffer );
            _p_buffer[0x14] &= 0x8f;
            _p_buffer = _p_buffer + DVDCSS_BLOCK_SIZE;
        }
    }

    return i_ret;
}

/**
 * \brief Read data from the disc into multiple buffers and decrypt data if
 *        requested.
 *
 * \param dvdcss a \e libdvdcss instance
 * \param p_iovec a pointer to an array of iovec structures that will contain
 *        the data read from the disc
 * \param i_blocks the amount of blocks to read
 * \param i_flags #DVDCSS_NOFLAGS, optionally ORed with #DVDCSS_READ_DECRYPT
 * \return the amount of blocks read or a negative value in case an
 *         error happened.
 *
 * Read \p i_blocks logical blocks from the DVD and write them
 * to an array of iovec structures.
 *
 * You typically set \p i_flags to #DVDCSS_NOFLAGS when reading data from a
 * .IFO file on the DVD.
 *
 * If #DVDCSS_READ_DECRYPT is specified in \p i_flags, dvdcss_readv() will
 * automatically decrypt scrambled sectors. This flag is typically used when
 * reading data from a .VOB file on the DVD. It has no effect on unscrambled
 * discs or unscrambled sectors and can be safely used on those.
 *
 * \warning dvdcss_readv() expects to be able to write \p i_blocks *
 *          #DVDCSS_BLOCK_SIZE bytes into the buffers pointed by \p p_iovec.
 *          Moreover, all iov_len members of the iovec structures should be
 *          multiples of #DVDCSS_BLOCK_SIZE.
 */
LIBDVDCSS_EXPORT int dvdcss_readv ( dvdcss_t dvdcss, void *p_iovec,
                                           int i_blocks,
                                           int i_flags )
{
    struct iovec *_p_iovec = p_iovec;
    int i_ret, i_index;
    void *iov_base;
    size_t iov_len;

    i_ret = dvdcss->pf_readv( dvdcss, _p_iovec, i_blocks );

    if( i_ret <= 0
         || !dvdcss->b_scrambled
         || !(i_flags & DVDCSS_READ_DECRYPT) )
    {
        return i_ret;
    }

    /* Initialize loop for decryption */
    iov_base = _p_iovec->iov_base;
    iov_len = _p_iovec->iov_len;

    /* Decrypt the blocks we managed to read */
    for( i_index = i_ret; i_index; i_index-- )
    {
        /* Check that iov_len is a multiple of 2048 */
        if( iov_len & 0x7ff )
        {
            return -1;
        }

        while( iov_len == 0 )
        {
            _p_iovec++;
            iov_base = _p_iovec->iov_base;
            iov_len = _p_iovec->iov_len;
        }

        dvdcss_unscramble( dvdcss->css.p_title_key, iov_base );
        ((uint8_t*)iov_base)[0x14] &= 0x8f;

        iov_base = (uint8_t*)iov_base + DVDCSS_BLOCK_SIZE;
        iov_len -= DVDCSS_BLOCK_SIZE;
    }

    return i_ret;
}

/**
 * \brief Clean up library state and structures.
 *
 * \param dvdcss a \e libdvdcss instance
 * \return zero in case of success, a negative value otherwise.
 *
 * Close the DVD device and free all the memory allocated by \e libdvdcss.
 * On return, the #dvdcss_t is invalidated and may not be used again.
 */
LIBDVDCSS_EXPORT int dvdcss_close ( dvdcss_t dvdcss )
{
    struct dvd_title *p_title;
    int i_ret;

    /* Free our list of keys */
    p_title = dvdcss->p_titles;
    while( p_title )
    {
        struct dvd_title *p_tmptitle = p_title->p_next;
        free( p_title );
        p_title = p_tmptitle;
    }

    i_ret = dvdcss_close_device( dvdcss );

    free( dvdcss->psz_device );
    free( dvdcss );

    return i_ret;
}

/**
 * \brief Detect whether or not a DVD is scrambled
 *
 * \param dvdcss a \e libdvdcss instance.
 * \return 1 if the DVD is scrambled, 0 otherwise.
 */
LIBDVDCSS_EXPORT int dvdcss_is_scrambled ( dvdcss_t dvdcss )
{
    return dvdcss->b_scrambled;
}
