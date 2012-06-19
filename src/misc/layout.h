#pragma once

/**
 * Based on JPEG/EXIF orientations
 *
 * http://sylvana.net/jpegcrop/exif_orientation.html
 */
#define LAYOUT_ORIENTATION_NONE       0
#define LAYOUT_ORIENTATION_NORMAL     1
#define LAYOUT_ORIENTATION_MIRROR_X   2
#define LAYOUT_ORIENTATION_ROT_180    3
#define LAYOUT_ORIENTATION_MIRROR_Y   4
#define LAYOUT_ORIENTATION_TRANSPOSE  5
#define LAYOUT_ORIENTATION_ROT_90     6
#define LAYOUT_ORIENTATION_TRANSVERSE 7
#define LAYOUT_ORIENTATION_ROT_270    8


// "numpad" style layout. Also used by SSA rendering, so don't change
#define LAYOUT_ALIGN_BOTTOM_LEFT   1
#define LAYOUT_ALIGN_BOTTOM        2
#define LAYOUT_ALIGN_BOTTOM_RIGHT  3
#define LAYOUT_ALIGN_LEFT          4
#define LAYOUT_ALIGN_CENTER        5
#define LAYOUT_ALIGN_RIGHT         6
#define LAYOUT_ALIGN_TOP_LEFT      7
#define LAYOUT_ALIGN_TOP           8
#define LAYOUT_ALIGN_TOP_RIGHT     9
#define LAYOUT_ALIGN_JUSTIFIED     10 // special

