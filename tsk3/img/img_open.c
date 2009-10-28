/*
 * Brian Carrier [carrier <at> sleuthkit [dot] org]
 * Copyright (c) 2006-2008 Brian Carrier, Basis Technology.  All Rights reserved
 * Copyright (c) 2005 Brian Carrier.  All rights reserved
 *
 * tsk_img_open
 *
 * This software is distributed under the Common Public License 1.0
 */

/**
 * \file img_open.c
 * Contains the basic img_open function call, that interfaces with
 * the format specific _open calls
 */

#include "tsk_img_i.h"

#include "raw.h"
#include "split.h"

#if HAVE_LIBAFFLIB
typedef int bool;
#include "aff.h"
#endif

#if HAVE_LIBEWF
#include "ewf.h"
#endif



/**
 * \ingroup imglib
 * Opens a single (non-split) disk image file so that it can be read.  This is a 
 * wrapper around tsk_img_open().  See it for more details on detection etc. See
 * tsk_img_open_sing_utf8() for a version of this function that always takes
 * UTF-8 as input. 
 *
 * @param a_image The path to the image file 
 * @param type The disk image type (can be autodetection)
 * @param a_ssize Size of device sector in bytes (or 0 for default)
 *
 * @return Pointer to TSK_IMG_INFO or NULL on error
 */
TSK_IMG_INFO *
tsk_img_open_sing(const TSK_TCHAR * a_image, TSK_IMG_TYPE_ENUM type,
    unsigned int a_ssize)
{
    const TSK_TCHAR *const a = a_image;
    return tsk_img_open(1, &a, type, a_ssize);
}


/**
 * \ingroup imglib
 * Opens one or more disk image files so that they can be read.  If a file format
 * type is specified, this function will call the specific routine to open the file.
 * Otherwise, it will detect the type (it will default to raw if no specific type can
 * be detected).   This function must be called before a disk image can be read from. 
 * Note that the data type used to store the image paths is a TSK_TCHAR, which changes
 * depending on a Unix or Windows build.  If you will always have UTF8, then consider
 * using tsk_img_open_utf8(). 
 *
 * @param num_img The number of images to open (will be > 1 for split images).
 * @param images The path to the image files (the number of files must
 * be equal to num_img and they must be in a sorted order)
 * @param type The disk image type (can be autodetection)
 * @param a_ssize Size of device sector in bytes (or 0 for default)
 *
 * @return Pointer to TSK_IMG_INFO or NULL on error
 */
TSK_IMG_INFO *
tsk_img_open(int num_img,
    const TSK_TCHAR * const images[], TSK_IMG_TYPE_ENUM type,
    unsigned int a_ssize)
{
    TSK_IMG_INFO *img_info = NULL;

    // Get rid of any old error messages laying around
    tsk_error_reset();

    if ((num_img == 0) || (images[0] == NULL)) {
        tsk_error_reset();
        tsk_errno = TSK_ERR_IMG_NOFILE;
        snprintf(tsk_errstr, TSK_ERRSTR_L, "tsk_img_open");
        return NULL;
    }
    
    if ((a_ssize > 0) & (a_ssize < 512)) {
        tsk_error_reset(); 
        tsk_errno = TSK_ERR_IMG_ARG;
        snprintf(tsk_errstr, TSK_ERRSTR_L, "sector size is less than 512 bytes (%d)", a_ssize);
        return NULL;
    }
    
    if ((a_ssize % 512) != 0) {
        tsk_error_reset(); 
        tsk_errno = TSK_ERR_IMG_ARG;
        snprintf(tsk_errstr, TSK_ERRSTR_L, "sector size is not a multiple of 512 (%d)", a_ssize);
        return NULL;
    }
    

    if (tsk_verbose)
        TFPRINTF(stderr,
            _TSK_T("tsk_img_open: Type: %d   NumImg: %d  Img1: %s\n"),
            type, num_img, images[0]);

    /* If no type is given, then we use the autodetection methods 
     * In case the image file matches the signatures of multiple formats,
     * we try all of the embedded formats 
     */
    if (type == TSK_IMG_TYPE_DETECT) {
        TSK_IMG_INFO *img_set = NULL;
#if HAVE_LIBAFFLIB || HAVE_LIBEWF
        char *set = NULL;
#endif
        struct STAT_STR stat_buf;

        // we rely on tsk_errno, so make sure it is 0
        tsk_error_reset();

        /* Try the non-raw formats first */
#if HAVE_LIBAFFLIB
        if ((img_info = aff_open(images, a_ssize)) != NULL) {
            /* we don't allow the "ANY" when autodetect is used because
             * we only want to detect the tested formats. */
            if (img_info->itype == TSK_IMG_TYPE_AFF_ANY) {
                img_info->close(img_info);
            }
            else {
                set = "AFF";
                img_set = img_info;
            }
        }
        else {
            tsk_error_reset();
        }
#endif

#if HAVE_LIBEWF
        if ((img_info = ewf_open(num_img, images, a_ssize)) != NULL) {
            if (set == NULL) {
                set = "EWF";
                img_set = img_info;
            }
            else {
                img_set->close(img_set);
                img_info->close(img_info);
                tsk_error_reset();
                tsk_errno = TSK_ERR_IMG_UNKTYPE;
                snprintf(tsk_errstr, TSK_ERRSTR_L, "EWF or %s", set);
                return NULL;
            }
        }
        else {
            tsk_error_reset();
        }
#endif
        // if any of the non-raw formats were detected, then use it. 
        if (img_set != NULL)
            return img_set;

        /* We'll use the raw format */
        if (num_img == 1) {
            if ((img_info = raw_open(images[0], a_ssize)) != NULL) {
                return img_info;
            }
            else if (tsk_errno) {
                return NULL;
            }
        }
        else {
            if ((img_info = split_open(num_img, images, a_ssize)) != NULL) {
                return img_info;
            }
            else if (tsk_errno) {
                return NULL;
            }
        }

        /* To improve the error message, verify the file can be read. */
        if (TSTAT(images[0], &stat_buf) < 0) {
            // special case to handle windows objects
#if defined(TSK_WIN32) || defined(__CYGWIN__)
            if (TSTRNCMP(_TSK_T("\\\\.\\"), images[0], 4) == 0) {
                if (tsk_verbose)
                    TFPRINTF(stderr,
                        _TSK_T
                        ("tsk_img_open: Ignoring stat error because of windows object: %s\n"),
                        images[0]);
            }
            else {
#endif
                tsk_error_reset();
                tsk_errno = TSK_ERR_IMG_STAT;
                snprintf(tsk_errstr, TSK_ERRSTR_L, "%" PRIttocTSK " : %s",
                    images[0], strerror(errno));
                return NULL;
#if defined(TSK_WIN32) || defined(__CYGWIN__)
            }
#endif
        }

        tsk_errno = TSK_ERR_IMG_UNKTYPE;
        tsk_errstr[0] = '\0';
        tsk_errstr2[0] = '\0';
        return NULL;
    }

    /*
     * Type values
     * Make a local copy that we can modify the string as we parse it
     */

    switch (type) {
    case TSK_IMG_TYPE_RAW_SING:

        /* If we have more than one image name, and raw was the only
         * type given, then use split */
        if (num_img > 1)
            img_info = split_open(num_img, images, a_ssize);
        else
            img_info = raw_open(images[0], a_ssize);
        break;

    case TSK_IMG_TYPE_RAW_SPLIT:

        /* If only one image file is given, and only one type was
         * given then use raw */
        if (num_img == 1)
            img_info = raw_open(images[0], a_ssize);
        else
            img_info = split_open(num_img, images, a_ssize);
        break;

#if HAVE_LIBAFFLIB
    case TSK_IMG_TYPE_AFF_AFF:
    case TSK_IMG_TYPE_AFF_AFD:
    case TSK_IMG_TYPE_AFF_AFM:
    case TSK_IMG_TYPE_AFF_ANY:
        img_info = aff_open(images, a_ssize);
        break;
#endif

#if HAVE_LIBEWF
    case TSK_IMG_TYPE_EWF_EWF:
        img_info = ewf_open(num_img, images, a_ssize);
        break;
#endif

    default:
        tsk_error_reset();
        tsk_errno = TSK_ERR_IMG_UNSUPTYPE;
        snprintf(tsk_errstr, TSK_ERRSTR_L, "%d", type);
        return NULL;
    }

    return img_info;
}


/**
* \ingroup imglib
 * Opens a single (non-split) disk image file so that it can be read.  This version
 * always takes a UTF-8 encoding of the disk image.  See tsk_img_open_sing() for a
 * version that takes a wchar_t or char depending on the platform. 
 * This is a wrapper around tsk_img_open().  See it for more details on detection etc. 
 *
 * @param a_image The UTF-8 path to the image file 
 * @param type The disk image type (can be autodetection)
 * @param a_ssize Size of device sector in bytes (or 0 for default)
 *
 * @return Pointer to TSK_IMG_INFO or NULL on error
 */
TSK_IMG_INFO *
tsk_img_open_utf8_sing(const char *a_image, TSK_IMG_TYPE_ENUM type,
    unsigned int a_ssize)
{
    const char *const a = a_image;
    return tsk_img_open_utf8(1, &a, type, a_ssize);
}


/**
 * \ingroup imglib
 * Opens one or more disk image files so that they can be read.  This is a wrapper
 * around tsk_img_open() and this version always takes a UTF-8 encoding of the 
 * image files.  See its description for more details. 
 *
 * @param num_img The number of images to open (will be > 1 for split images).
 * @param images The path to the UTF-8 encoded image files (the number of files must
 * be equal to num_img and they must be in a sorted order)
 * @param type The disk image type (can be autodetection)
 * @param a_ssize Size of device sector in bytes (or 0 for default)
 *
 * @return Pointer to TSK_IMG_INFO or NULL on error
 */
TSK_IMG_INFO *
tsk_img_open_utf8(int num_img, const char *const images[],
    TSK_IMG_TYPE_ENUM type, unsigned int a_ssize)
{
#ifdef TSK_WIN32
    {
        /* Note that there is an assumption in this code that wchar_t is 2-bytes.
         * this is a correct assumption for Windows, but not for all systems... */

        TSK_IMG_INFO *retval = NULL;
        wchar_t **images16;
        int i;

        // allocate a buffer to store the UTF-16 version of the images. 
        if ((images16 =
                (wchar_t **) tsk_malloc(sizeof(wchar_t *) * num_img)) ==
            NULL) {
            return NULL;
        }

        for (i = 0; i < num_img; i++) {
            size_t ilen;
            UTF16 *utf16;
            UTF8 *utf8;
            TSKConversionResult retval2;

            // we allocate the buffer with the same number of chars as the UTF-8 length
            ilen = strlen(images[i]);
            if ((images16[i] =
                    (wchar_t *) tsk_malloc((ilen +
                            1) * sizeof(wchar_t))) == NULL) {
                goto tsk_utf8_cleanup;
            }

            utf8 = (UTF8 *) images[i];
            utf16 = (UTF16 *) images16[i];

            retval2 =
                tsk_UTF8toUTF16((const UTF8 **) &utf8, &utf8[ilen],
                &utf16, &utf16[ilen], TSKlenientConversion);
            if (retval2 != TSKconversionOK) {
                tsk_errno = TSK_ERR_IMG_CONVERT;
                snprintf(tsk_errstr, TSK_ERRSTR_L,
                    "tsk_img_open_utf8: Error converting image %s %d",
                    images[i], retval2);
                goto tsk_utf8_cleanup;
            }
            *utf16 = '\0';
        }

        retval = tsk_img_open(num_img, images16, type, a_ssize);

        // free up the memory
      tsk_utf8_cleanup:
        for (i = 0; i < num_img; i++) {
            if (images16[i])
                free(images16[i]);
        }
        free(images16);

        return retval;
    }
#else
    return tsk_img_open(num_img, images, type, a_ssize);
#endif
}


#if 0
/* This interface needs some more thought because the size of wchar is not standard.
 * If the goal i to provide a constant wchar interface, then we need to incorporate
 * UTF-32 to UTF-8 support as well.  If the goal is to provide a standard UTF-16 
 * interface, we should use another type besiddes wchar_t.
 */
TSK_IMG_INFO *
tsk_img_open_utf16(int num_img,
    wchar_t * const images[], TSK_IMG_TYPE_ENUM type)
{
#if TSK_WIN32
    return tsk_img_open(num_img, images, type);
#else
    {
        TSK_IMG_INFO *retval;
        int i;
        char **images8;
        TSK_ENDIAN_ENUM endian;
        uint16_t tmp1;

        /* The unicode conversio routines are primarily to convert Unicode
         * in file and volume system images, which means they could be in
         * an endian ordering different from the local one.  We need to figure
         * out our local ordering so we can give it the right flag */
        tmp1 = 1;
        if (tsk_guess_end_u16(&endian, (uint8_t *) & tmp1, 1)) {
            // @@@@
            return NULL;
        }


        // convert UTF16 to UTF8
        if ((images8 =
                (char **) tsk_malloc(sizeof(char *) * num_img)) == NULL) {
            return NULL;
        }

        for (i = 0; i < num_img; i++) {
            size_t ilen;
            UTF16 *utf16;
            UTF8 *utf8;
            TSKConversionResult retval2;


            // we allocate the buffer to be four times the utf-16 length. 
            ilen = wcslen(images[i]);
            ilen <<= 2;

            if ((images8[i] = (char *) tsk_malloc(ilen)) == NULL) {
                return NULL;
            }

            utf16 = (UTF16 *) images[i];
            utf8 = (UTF8 *) images8[i];

            retval2 =
                tsk_UTF16toUTF8_lclorder((const UTF16 **) &utf16,
                &utf16[wcslen(images[i]) + 1], &utf8,
                &utf8[ilen + 1], TSKlenientConversion);
            if (retval2 != TSKconversionOK) {
                tsk_errno = TSK_ERR_IMG_CONVERT;
                snprintf(tsk_errstr, TSK_ERRSTR_L,
                    "tsk_img_open_utf16: Error converting image %d %d", i,
                    retval2);
                return NULL;
            }
            *utf8 = '\0';
        }

        retval = tsk_img_open(num_img, (const TSK_TCHAR **) images8, type);

        for (i = 0; i < num_img; i++) {
            free(images8[i]);
        }
        free(images8);

        return retval;
    }
#endif
}
#endif




/**
 * \ingroup imglib
 * Closes an open disk image.
 * @param a_img_info Pointer to the open disk image structure.
 */
void
tsk_img_close(TSK_IMG_INFO * a_img_info)
{
    if (a_img_info == NULL) {
        return;
    }
    a_img_info->close(a_img_info);
}
