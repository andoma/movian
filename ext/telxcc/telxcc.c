/*!
(c) 2011-2014 Forers, s. r. o.: telxcc

telxcc conforms to ETSI 300 706 Presentation Level 1.5: Presentation Level 1 defines the basic Teletext page,
characterised by the use of spacing attributes only and a limited alphanumeric and mosaics repertoire.
Presentation Level 1.5 decoder responds as Level 1 but the character repertoire is extended via packets X/26.
Selection of national option sub-sets related features from Presentation Level 2.5 feature set have been implemented, too.
(X/28/0 Format 1, X/28/4, M/29/0 and M/29/4 packets)

Further documentation:
ETSI TS 101 154 V1.9.1 (2009-09), Technical Specification
  Digital Video Broadcasting (DVB); Specification for the use of Video and Audio Coding in Broadcasting Applications based on the MPEG-2 Transport Stream
ETSI EN 300 231 V1.3.1 (2003-04), European Standard (Telecommunications series)
  Television systems; Specification of the domestic video Programme Delivery Control system (PDC)
ETSI EN 300 472 V1.3.1 (2003-05), European Standard (Telecommunications series)
  Digital Video Broadcasting (DVB); Specification for conveying ITU-R System B Teletext in DVB bitstreams
ETSI EN 301 775 V1.2.1 (2003-05), European Standard (Telecommunications series)
  Digital Video Broadcasting (DVB); Specification for the carriage of Vertical Blanking Information (VBI) data in DVB bitstreams
ETS 300 706 (May 1997)
  Enhanced Teletext Specification
ETS 300 708 (March 1997)
  Television systems; Data transmission within Teletext
ISO/IEC STANDARD 13818-1 Second edition (2000-12-01)
  Information technology — Generic coding of moving pictures and associated audio information: Systems
ISO/IEC STANDARD 6937 Third edition (2001-12-15)
  Information technology — Coded graphic character set for text communication — Latin alphabet
Werner Brückner -- Teletext in digital television
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <inttypes.h>
#include "hamming.h"
#include "teletext.h"

#define TELXCC_VERSION "2.6.0"

#ifdef _WIN32
#define PLATFORM "Windows"
#elif __linux__
#define PLATFORM "Linux"
#elif __APPLE__
#define PLATFORM "Apple"
#elif __unix__
#define PLATFORM "Unix"
#elif __posix__
#define PLATFORM "POSIX"
#else
#define PLATFORM "unknown platform"
#endif

#ifdef __MINGW32__
// switch stdin and all normal files into binary mode -- needed for Windows
#include <fcntl.h>
int _CRT_fmode = _O_BINARY;

#define WIN32_LEAN_AND_MEAN
#define _WIN32_IE 0x0400
#define ICC_STANDARD_CLASSES 0x00004000
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <wchar.h>
#endif

typedef enum {
	NO = 0x00,
	YES = 0x01,
	UNDEF = 0xff
} bool_t;

// size of a (M2)TS packet in bytes (TS = 188, M2TS = 192)
#define TS_PACKET_SIZE 192

// size of a TS packet payload in bytes
#define TS_PACKET_PAYLOAD_SIZE 184

// size of a packet payload buffer
#define PAYLOAD_BUFFER_SIZE 4096

typedef struct {
	uint8_t sync;
	uint8_t transport_error;
	uint8_t payload_unit_start;
	uint8_t transport_priority;
	uint16_t pid;
	uint8_t scrambling_control;
	uint8_t adaptation_field_exists;
	uint8_t continuity_counter;
} ts_packet_t;

typedef struct {
	uint16_t program_num;
	uint16_t program_pid;
} pat_section_t;

typedef struct {
	uint8_t pointer_field;
	uint8_t table_id;
	uint16_t section_length;
	uint8_t current_next_indicator;
} pat_t;

typedef struct {
	uint8_t stream_type;
	uint16_t elementary_pid;
	uint16_t es_info_length;
} pmt_program_descriptor_t;

typedef struct {
	uint8_t pointer_field;
	uint8_t table_id;
	uint16_t section_length;
	uint16_t program_num;
	uint8_t current_next_indicator;
	uint16_t pcr_pid;
	uint16_t program_info_length;
} pmt_t;

typedef enum {
	DATA_UNIT_EBU_TELETEXT_NONSUBTITLE = 0x02,
	DATA_UNIT_EBU_TELETEXT_SUBTITLE = 0x03,
	DATA_UNIT_EBU_TELETEXT_INVERTED = 0x0c,
	DATA_UNIT_VPS = 0xc3,
	DATA_UNIT_CLOSED_CAPTIONS = 0xc5
} data_unit_t;

typedef enum {
	TRANSMISSION_MODE_PARALLEL = 0,
	TRANSMISSION_MODE_SERIAL = 1
} transmission_mode_t;

const char* TTXT_COLOURS[8] = {
	//black,     red,       green,     yellow,    blue,      magenta,   cyan,      white
	"#000000", "#ff0000", "#00ff00", "#ffff00", "#0000ff", "#ff00ff", "#00ffff", "#ffffff"
};

// 1-byte alignment; just to be sure, this struct is being used for explicit type conversion
// FIXME: remove explicit type conversion from buffer to structs
#pragma pack(push)
#pragma pack(1)
typedef struct {
	uint8_t _clock_in; // clock run in
	uint8_t _framing_code; // framing code, not needed, ETSI 300 706: const 0xe4
	uint8_t address[2];
	uint8_t data[40];
} teletext_packet_payload_t;
#pragma pack(pop)

typedef struct {
	uint64_t show_timestamp; // show at timestamp (in ms)
	uint64_t hide_timestamp; // hide at timestamp (in ms)
	uint16_t text[25][40]; // 25 lines x 40 cols (1 screen/page) of wide chars
	uint8_t tainted; // 1 = text variable contains any data
} teletext_page_t;

typedef struct {
	uint64_t show_timestamp; // show at timestamp (in ms)
	uint64_t hide_timestamp; // hide at timestamp (in ms)
	char *text;
} frame_t;

// application config global variable
struct {
#ifdef __MINGW32__
	wchar_t *input_name; // input file name (used on Windows, UNICODE)
	wchar_t *output_name; // output file name (used on Windows, UNICODE)
#else
	char *input_name; // input file name
	char *output_name; // output file name
#endif
	uint8_t verbose; // should telxcc be verbose?
	uint16_t page; // teletext page containing cc we want to filter
	uint16_t tid; // 13-bit packet ID for teletext stream
	double offset; // time offset in seconds
	uint8_t colours; // output <font...></font> tags
	uint8_t bom; // print UTF-8 BOM characters at the beginning of output
	uint8_t nonempty; // produce at least one (dummy) frame
	uint64_t utc_refvalue; // UTC referential value
	// FIXME: move SE_MODE to output module
	uint8_t se_mode;
	//char *template; // output format template
	uint8_t m2ts; // consider input stream is af s M2TS, instead of TS
} config = {
	.input_name = NULL,
	.output_name = NULL,
	.verbose = NO,
	.page = 0,
	.tid = 0,
	.offset = 0,
	.colours = NO,
	.bom = YES,
	.nonempty = NO,
	.utc_refvalue = 0,
	.se_mode = NO,
	//.template = NULL,
	.m2ts = NO
};

/*
formatting template:
	%f -- from timestamp (absolute, UTC)
	%t -- to timestamp (absolute, UTC)
	%F -- from time (SRT)
	%T -- to time (SRT)
	%g -- from timestamp (relative)
	%u -- to timestamp (relative)
	%c -- counter 0-based
	%C -- counter 1-based
	%s -- subtitles
	%l -- subtitles (lines)
	%p -- page number
	%i -- stream ID
*/

FILE *fin = NULL;
FILE *fout = NULL;

// macro -- output only when increased verbosity was turned on
#define VERBOSE_ONLY if (config.verbose == YES)

// application states -- flags for notices that should be printed only once
struct {
	uint8_t programme_info_processed;
	uint8_t pts_initialized;
} states = {
	.programme_info_processed = NO,
	.pts_initialized = NO
};

// SRT frames produced
uint32_t frames_produced = 0;

// subtitle type pages bitmap, 2048 bits = 2048 possible pages in teletext (excl. subpages)
uint8_t cc_map[256] = { 0 };

// global TS PCR value
uint32_t global_timestamp = 0;

// last timestamp computed
uint64_t last_timestamp = 0;

// working teletext page buffer
teletext_page_t page_buffer = { 0 };

// teletext transmission mode
transmission_mode_t transmission_mode = TRANSMISSION_MODE_SERIAL;

// flag indicating if incoming data should be processed or ignored
uint8_t receiving_data = NO;

// current charset (charset can be -- and always is -- changed during transmission)
struct {
	uint8_t current;
	uint8_t g0_m29;
	uint8_t g0_x28;
} primary_charset = {
	.current = 0x00,
	.g0_m29 = UNDEF,
	.g0_x28 = UNDEF
};

// entities, used in colour mode, to replace unsafe HTML tag chars
struct {
	uint16_t character;
	char *entity;
} const ENTITIES[] = {
	{ .character = '<', .entity = "&lt;" },
	{ .character = '>', .entity = "&gt;" },
	{ .character = '&', .entity = "&amp;" }
};

// PMTs table
#define TS_PMT_MAP_SIZE 128
uint16_t pmt_map[TS_PMT_MAP_SIZE] = { 0 };
uint16_t pmt_map_count = 0;

// TTXT streams table
#define TS_PMT_TTXT_MAP_SIZE 128
uint16_t pmt_ttxt_map[TS_PMT_MAP_SIZE] = { 0 };
uint16_t pmt_ttxt_map_count = 0;

// helper, array length function
#define ARRAY_LENGTH(a) (sizeof(a)/sizeof(a[0]))

// helper, linear searcher for a value
static inline bool_t in_array(uint16_t *array, uint16_t length, uint16_t element) {
	bool_t r = NO;
	for (uint16_t i = 0; i < length; i++) {
		if (array[i] == element) {
			r = YES;
			break;
		}
	}
	return r;
}

// extracts magazine number from teletext page
#define MAGAZINE(p) ((p >> 8) & 0xf)

// extracts page number from teletext page
#define PAGE(p) (p & 0xff)

// ETS 300 706, chapter 8.2
uint8_t unham_8_4(uint8_t a) {
	uint8_t r = UNHAM_8_4[a];
	if (r == 0xff) {
		r = 0;
		VERBOSE_ONLY fprintf(stderr, "! Unrecoverable data error; UNHAM8/4(%02x)\n", a);
	}
	return (r & 0x0f);
}

// ETS 300 706, chapter 8.3
uint32_t unham_24_18(uint32_t a) {
	uint8_t test = 0;

	// Tests A-F correspond to bits 0-6 respectively in 'test'.
	for (uint8_t i = 0; i < 23; i++) test ^= ((a >> i) & 0x01) * (i + 33);
	// Only parity bit is tested for bit 24
	test ^= ((a >> 23) & 0x01) * 32;

	if ((test & 0x1f) != 0x1f) {
		// Not all tests A-E correct
		if ((test & 0x20) == 0x20) {
			// F correct: Double error
			return 0xffffffff;
		}
		// Test F incorrect: Single error
		a ^= 1 << (30 - test);
	}

	return (a & 0x000004) >> 2 | (a & 0x000070) >> 3 | (a & 0x007f00) >> 4 | (a & 0x7f0000) >> 5;
}

void remap_g0_charset(uint8_t c) {
	if (c != primary_charset.current) {
		uint8_t m = G0_LATIN_NATIONAL_SUBSETS_MAP[c];
		if (m == 0xff) {
			fprintf(stderr, "- G0 Latin National Subset ID 0x%1x.%1x is not implemented\n", (c >> 3), (c & 0x7));
		}
		else {
			for (uint8_t j = 0; j < 13; j++) G0[LATIN][G0_LATIN_NATIONAL_SUBSETS_POSITIONS[j]] = G0_LATIN_NATIONAL_SUBSETS[m].characters[j];
			VERBOSE_ONLY fprintf(stderr, "- Using G0 Latin National Subset ID 0x%1x.%1x (%s)\n", (c >> 3), (c & 0x7), G0_LATIN_NATIONAL_SUBSETS[m].language);
			primary_charset.current = c;
		}
	}
}

void timestamp_to_srttime(uint64_t timestamp, char *buffer) {
	uint64_t p = timestamp;
	uint8_t h = p / 3600000;
	uint8_t m = p / 60000 - 60 * h;
	uint8_t s = p / 1000 - 3600 * h - 60 * m;
	uint16_t u = p - 3600000 * h - 60000 * m - 1000 * s;
	sprintf(buffer, "%02"PRIu8":%02"PRIu8":%02"PRIu8",%03"PRIu16, h, m, s, u);
}

// UCS-2 (16 bits) to UTF-8 (Unicode Normalization Form C (NFC)) conversion
void ucs2_to_utf8(char *r, uint16_t ch) {
	if (ch < 0x80) {
		r[0] = ch & 0x7f;
		r[1] = 0;
		r[2] = 0;
		r[3] = 0;
	}
	else if (ch < 0x800) {
		r[0] = (ch >> 6) | 0xc0;
		r[1] = (ch & 0x3f) | 0x80;
		r[2] = 0;
		r[3] = 0;
	}
	else {
		r[0] = (ch >> 12) | 0xe0;
		r[1] = ((ch >> 6) & 0x3f) | 0x80;
		r[2] = (ch & 0x3f) | 0x80;
		r[3] = 0;
	}
}

// check parity and translate any reasonable teletext character into ucs2
uint16_t telx_to_ucs2(uint8_t c) {
	if (PARITY_8[c] == 0) {
		VERBOSE_ONLY fprintf(stderr, "! Unrecoverable data error; PARITY(%02x)\n", c);
		return 0x20;
	}

	uint16_t r = c & 0x7f;
	if (r >= 0x20) r = G0[LATIN][r - 0x20];
	return r;
}

// FIXME: implement output modules (to support different formats, printf formatting etc)
void process_page(teletext_page_t *page) {
#ifdef DEBUG
	for (uint8_t row = 1; row < 25; row++) {
		fprintf(fout, "# DEBUG[%02u]: ", row);
		for (uint8_t col = 0; col < 40; col++) fprintf(fout, "%3x ", page->text[row][col]);
		fprintf(fout, "\n");
	}
	fprintf(fout, "\n");
#endif

	// optimization: slicing column by column -- higher probability we could find boxed area start mark sooner
	uint8_t page_is_empty = YES;
	for (uint8_t col = 0; col < 40; col++) {
		for (uint8_t row = 1; row < 25; row++) {
			if (page->text[row][col] == 0x0b) {
				page_is_empty = NO;
				goto page_is_empty;
			}
		}
	}
	page_is_empty:
	if (page_is_empty == YES) return;

	if (page->show_timestamp > page->hide_timestamp) page->hide_timestamp = page->show_timestamp;

	if (config.se_mode == YES) {
		++frames_produced;
		fprintf(fout, "%.3f|", (double)page->show_timestamp / 1000.0);
	}
	else {
		char timecode_show[24] = { 0 };
		timestamp_to_srttime(page->show_timestamp, timecode_show);
		timecode_show[12] = 0;

		char timecode_hide[24] = { 0 };
		timestamp_to_srttime(page->hide_timestamp, timecode_hide);
		timecode_hide[12] = 0;

		fprintf(fout, "%"PRIu32"\r\n%s --> %s\r\n", ++frames_produced, timecode_show, timecode_hide);
	}

	// process data
	for (uint8_t row = 1; row < 25; row++) {
		// anchors for string trimming purpose
		uint8_t col_start = 40;
		uint8_t col_stop = 40;

		for (int8_t col = 39; col >= 0; col--) {
			if (page->text[row][col] == 0xb) {
				col_start = col;
				break;
			}
		}
		// line is empty
		if (col_start > 39) continue;

		for (uint8_t col = col_start + 1; col <= 39; col++) {
			if (page->text[row][col] > 0x20) {
				if (col_stop > 39) col_start = col;
				col_stop = col;
			}
			if (page->text[row][col] == 0xa) break;
		}
		// line is empty
		if (col_stop > 39) continue;

		// ETS 300 706, chapter 12.2: Alpha White ("Set-After") - Start-of-row default condition.
		// used for colour changes _before_ start box mark
		// white is default as stated in ETS 300 706, chapter 12.2
		// black(0), red(1), green(2), yellow(3), blue(4), magenta(5), cyan(6), white(7)
		uint8_t foreground_color = 0x7;
		uint8_t font_tag_opened = NO;

		for (uint8_t col = 0; col <= col_stop; col++) {
			// v is just a shortcut
			uint16_t v = page->text[row][col];

			if (col < col_start) {
				if (v <= 0x7) foreground_color = v;
			}

			if (col == col_start) {
				if ((foreground_color != 0x7) && (config.colours == YES)) {
					fprintf(fout, "<font color=\"%s\">", TTXT_COLOURS[foreground_color]);
					font_tag_opened = YES;
				}
			}

			if (col >= col_start) {
				if (v <= 0x7) {
					// ETS 300 706, chapter 12.2: Unless operating in "Hold Mosaics" mode,
					// each character space occupied by a spacing attribute is displayed as a SPACE.
					if (config.colours == YES) {
						if (font_tag_opened == YES) {
							fprintf(fout, "</font> ");
							font_tag_opened = NO;
						}

						// black is considered as white for telxcc purpose
						// telxcc writes <font/> tags only when needed
						if ((v > 0x0) && (v < 0x7)) {
							fprintf(fout, "<font color=\"%s\">", TTXT_COLOURS[v]);
							font_tag_opened = YES;
						}
					}
					else v = 0x20;
				}

				if (v >= 0x20) {
					// translate some chars into entities, if in colour mode
					if (config.colours == YES) {
						for (uint8_t i = 0; i < ARRAY_LENGTH(ENTITIES); i++) {
							if (v == ENTITIES[i].character) {
								fprintf(fout, "%s", ENTITIES[i].entity);
								// v < 0x20 won't be printed in next block
								v = 0;
								break;
							}
						}
					}
				}

				if (v >= 0x20) {
					char u[4] = { 0, 0, 0, 0 };
					ucs2_to_utf8(u, v);
					fprintf(fout, "%s", u);
				}
			}
		}

		// no tag will left opened!
		if ((config.colours == YES) && (font_tag_opened == YES)) {
			fprintf(fout, "</font>");
			font_tag_opened = NO;
		}

		// line delimiter
		fprintf(fout, "%s", (config.se_mode == YES) ? " " : "\r\n");
	}

	fprintf(fout, "\r\n");
	fflush(fout);
}

void process_telx_packet(data_unit_t data_unit_id, teletext_packet_payload_t *packet, uint64_t timestamp) {
	// variable names conform to ETS 300 706, chapter 7.1.2
	uint8_t address = (unham_8_4(packet->address[1]) << 4) | unham_8_4(packet->address[0]);
	uint8_t m = address & 0x7;
	if (m == 0) m = 8;
	uint8_t y = (address >> 3) & 0x1f;
	uint8_t designation_code = (y > 25) ? unham_8_4(packet->data[0]) : 0x00;

	if (y == 0) {
		// CC map
		uint8_t i = (unham_8_4(packet->data[1]) << 4) | unham_8_4(packet->data[0]);
		uint8_t flag_subtitle = (unham_8_4(packet->data[5]) & 0x08) >> 3;
		cc_map[i] |= flag_subtitle << (m - 1);

		if ((config.page == 0) && (flag_subtitle == YES) && (i < 0xff)) {
			config.page = (m << 8) | (unham_8_4(packet->data[1]) << 4) | unham_8_4(packet->data[0]);
			fprintf(stderr, "- No teletext page specified, first received suitable page is %03x, not guaranteed\n", config.page);
		}

	 	// Page number and control bits
		uint16_t page_number = (m << 8) | (unham_8_4(packet->data[1]) << 4) | unham_8_4(packet->data[0]);
		uint8_t charset = ((unham_8_4(packet->data[7]) & 0x08) | (unham_8_4(packet->data[7]) & 0x04) | (unham_8_4(packet->data[7]) & 0x02)) >> 1;
		//uint8_t flag_suppress_header = unham_8_4(packet->data[6]) & 0x01;
		//uint8_t flag_inhibit_display = (unham_8_4(packet->data[6]) & 0x08) >> 3;

		// ETS 300 706, chapter 9.3.1.3:
		// When set to '1' the service is designated to be in Serial mode and the transmission of a page is terminated
		// by the next page header with a different page number.
		// When set to '0' the service is designated to be in Parallel mode and the transmission of a page is terminated
		// by the next page header with a different page number but the same magazine number.
		// The same setting shall be used for all page headers in the service.
		// ETS 300 706, chapter 7.2.1: Page is terminated by and excludes the next page header packet
		// having the same magazine address in parallel transmission mode, or any magazine address in serial transmission mode.
		transmission_mode = unham_8_4(packet->data[7]) & 0x01;

		// FIXME: Well, this is not ETS 300 706 kosher, however we are interested in DATA_UNIT_EBU_TELETEXT_SUBTITLE only
		if ((transmission_mode == TRANSMISSION_MODE_PARALLEL) && (data_unit_id != DATA_UNIT_EBU_TELETEXT_SUBTITLE)) return;

		if ((receiving_data == YES) && (
				((transmission_mode == TRANSMISSION_MODE_SERIAL) && (PAGE(page_number) != PAGE(config.page))) ||
				((transmission_mode == TRANSMISSION_MODE_PARALLEL) && (PAGE(page_number) != PAGE(config.page)) && (m == MAGAZINE(config.page)))
			)) {
			receiving_data = NO;
			return;
		}

		// Page transmission is terminated, however now we are waiting for our new page
		if (page_number != config.page) return;

		// Now we have the begining of page transmission; if there is page_buffer pending, process it
		if (page_buffer.tainted == YES) {
			// it would be nice, if subtitle hides on previous video frame, so we contract 40 ms (1 frame @25 fps)
			page_buffer.hide_timestamp = timestamp - 40;
			process_page(&page_buffer);
		}

		page_buffer.show_timestamp = timestamp;
		page_buffer.hide_timestamp = 0;
		memset(page_buffer.text, 0x00, sizeof(page_buffer.text));
		page_buffer.tainted = NO;
		receiving_data = YES;
		primary_charset.g0_x28 = UNDEF;

		uint8_t c = (primary_charset.g0_m29 != UNDEF) ? primary_charset.g0_m29 : charset;
		remap_g0_charset(c);

		/*
		// I know -- not needed; in subtitles we will never need disturbing teletext page status bar
		// displaying tv station name, current time etc.
		if (flag_suppress_header == NO) {
			for (uint8_t i = 14; i < 40; i++) page_buffer.text[y][i] = telx_to_ucs2(packet->data[i]);
			//page_buffer.tainted = YES;
		}
		*/
	}
	else if ((m == MAGAZINE(config.page)) && (y >= 1) && (y <= 23) && (receiving_data == YES)) {
		// ETS 300 706, chapter 9.4.1: Packets X/26 at presentation Levels 1.5, 2.5, 3.5 are used for addressing
		// a character location and overwriting the existing character defined on the Level 1 page
		// ETS 300 706, annex B.2.2: Packets with Y = 26 shall be transmitted before any packets with Y = 1 to Y = 25;
		// so page_buffer.text[y][i] may already contain any character received
		// in frame number 26, skip original G0 character
		for (uint8_t i = 0; i < 40; i++) if (page_buffer.text[y][i] == 0x00) page_buffer.text[y][i] = telx_to_ucs2(packet->data[i]);
		page_buffer.tainted = YES;
	}
	else if ((m == MAGAZINE(config.page)) && (y == 26) && (receiving_data == YES)) {
		// ETS 300 706, chapter 12.3.2: X/26 definition
		uint8_t x26_row = 0;
		uint8_t x26_col = 0;

		uint32_t triplets[13] = { 0 };
		for (uint8_t i = 1, j = 0; i < 40; i += 3, j++) triplets[j] = unham_24_18((packet->data[i + 2] << 16) | (packet->data[i + 1] << 8) | packet->data[i]);

		for (uint8_t j = 0; j < 13; j++) {
			if (triplets[j] == 0xffffffff) {
				// invalid data (HAM24/18 uncorrectable error detected), skip group
				VERBOSE_ONLY fprintf(stderr, "! Unrecoverable data error; UNHAM24/18()=%04x\n", triplets[j]);
				continue;
			}

			uint8_t data = (triplets[j] & 0x3f800) >> 11;
			uint8_t mode = (triplets[j] & 0x7c0) >> 6;
			uint8_t address = triplets[j] & 0x3f;
			uint8_t row_address_group = (address >= 40) && (address <= 63);

			// ETS 300 706, chapter 12.3.1, table 27: set active position
			if ((mode == 0x04) && (row_address_group == YES)) {
				x26_row = address - 40;
				if (x26_row == 0) x26_row = 24;
				x26_col = 0;
			}

			// ETS 300 706, chapter 12.3.1, table 27: termination marker
			if ((mode >= 0x11) && (mode <= 0x1f) && (row_address_group == YES)) break;

			// ETS 300 706, chapter 12.3.1, table 27: character from G2 set
			if ((mode == 0x0f) && (row_address_group == NO)) {
				x26_col = address;
				if (data > 31) page_buffer.text[x26_row][x26_col] = G2[0][data - 0x20];
			}

			// ETS 300 706, chapter 12.3.1, table 27: G0 character with diacritical mark
			if ((mode >= 0x11) && (mode <= 0x1f) && (row_address_group == NO)) {
				x26_col = address;

				// A - Z
				if ((data >= 65) && (data <= 90)) page_buffer.text[x26_row][x26_col] = G2_ACCENTS[mode - 0x11][data - 65];
				// a - z
				else if ((data >= 97) && (data <= 122)) page_buffer.text[x26_row][x26_col] = G2_ACCENTS[mode - 0x11][data - 71];
				// other
				else page_buffer.text[x26_row][x26_col] = telx_to_ucs2(data);
			}
		}
	}
	else if ((m == MAGAZINE(config.page)) && (y == 28) && (receiving_data == YES)) {
		// TODO:
		//   ETS 300 706, chapter 9.4.7: Packet X/28/4
		//   Where packets 28/0 and 28/4 are both transmitted as part of a page, packet 28/0 takes precedence over 28/4 for all but the colour map entry coding.
		if ((designation_code == 0) || (designation_code == 4)) {
			// ETS 300 706, chapter 9.4.2: Packet X/28/0 Format 1
			// ETS 300 706, chapter 9.4.7: Packet X/28/4
			uint32_t triplet0 = unham_24_18((packet->data[3] << 16) | (packet->data[2] << 8) | packet->data[1]);

			if (triplet0 == 0xffffffff) {
				// invalid data (HAM24/18 uncorrectable error detected), skip group
				VERBOSE_ONLY fprintf(stderr, "! Unrecoverable data error; UNHAM24/18()=%04x\n", triplet0);
			}
			else {
				// ETS 300 706, chapter 9.4.2: Packet X/28/0 Format 1 only
				if ((triplet0 & 0x0f) == 0x00) {
					primary_charset.g0_x28 = (triplet0 & 0x3f80) >> 7;
					remap_g0_charset(primary_charset.g0_x28);
				}
			}
		}
	}
	else if ((m == MAGAZINE(config.page)) && (y == 29)) {
		// TODO:
		//   ETS 300 706, chapter 9.5.1 Packet M/29/0
		//   Where M/29/0 and M/29/4 are transmitted for the same magazine, M/29/0 takes precedence over M/29/4.
		if ((designation_code == 0) || (designation_code == 4)) {
			// ETS 300 706, chapter 9.5.1: Packet M/29/0
			// ETS 300 706, chapter 9.5.3: Packet M/29/4
			uint32_t triplet0 = unham_24_18((packet->data[3] << 16) | (packet->data[2] << 8) | packet->data[1]);

			if (triplet0 == 0xffffffff) {
				// invalid data (HAM24/18 uncorrectable error detected), skip group
				VERBOSE_ONLY fprintf(stderr, "! Unrecoverable data error; UNHAM24/18()=%04x\n", triplet0);
			}
			else {
				// ETS 300 706, table 11: Coding of Packet M/29/0
				// ETS 300 706, table 13: Coding of Packet M/29/4
				if ((triplet0 & 0xff) == 0x00) {
					primary_charset.g0_m29 = (triplet0 & 0x3f80) >> 7;
					// X/28 takes precedence over M/29
					if (primary_charset.g0_x28 == UNDEF) {
						remap_g0_charset(primary_charset.g0_m29);
					}
				}
			}
		}
	}
	else if ((m == 8) && (y == 30)) {
		// ETS 300 706, chapter 9.8: Broadcast Service Data Packets
		if (states.programme_info_processed == NO) {
			// ETS 300 706, chapter 9.8.1: Packet 8/30 Format 1
			if (unham_8_4(packet->data[0]) < 2) {
				fprintf(stderr, "- Programme Identification Data = ");
				for (uint8_t i = 20; i < 40; i++) {
					uint8_t c = telx_to_ucs2(packet->data[i]);
					// strip any control codes from PID, eg. TVP station
					if (c < 0x20) continue;

					char u[4] = { 0, 0, 0, 0 };
					ucs2_to_utf8(u, c);
					fprintf(stderr, "%s", u);
				}
				fprintf(stderr, "\n");

				// OMG! ETS 300 706 stores timestamp in 7 bytes in Modified Julian Day in BCD format + HH:MM:SS in BCD format
				// + timezone as 5-bit count of half-hours from GMT with 1-bit sign
				// In addition all decimals are incremented by 1 before transmission.
				uint32_t t = 0;
				// 1st step: BCD to Modified Julian Day
				t += (packet->data[10] & 0x0f) * 10000;
				t += ((packet->data[11] & 0xf0) >> 4) * 1000;
				t += (packet->data[11] & 0x0f) * 100;
				t += ((packet->data[12] & 0xf0) >> 4) * 10;
				t += (packet->data[12] & 0x0f);
				t -= 11111;
				// 2nd step: conversion Modified Julian Day to unix timestamp
				t = (t - 40587) * 86400;
				// 3rd step: add time
				t += 3600 * ( ((packet->data[13] & 0xf0) >> 4) * 10 + (packet->data[13] & 0x0f) );
				t +=   60 * ( ((packet->data[14] & 0xf0) >> 4) * 10 + (packet->data[14] & 0x0f) );
				t +=        ( ((packet->data[15] & 0xf0) >> 4) * 10 + (packet->data[15] & 0x0f) );
				t -= 40271;
				// 4th step: conversion to time_t
				time_t t0 = (time_t)t;
				// ctime output itself is \n-ended
				fprintf(stderr, "- Programme Timestamp (UTC) = %s", ctime(&t0));

				VERBOSE_ONLY fprintf(stderr, "- Transmission mode = %s\n", (transmission_mode == TRANSMISSION_MODE_SERIAL ? "serial" : "parallel"));

				if (config.se_mode == YES) {
					fprintf(stderr, "- Broadcast Service Data Packet received, resetting UTC referential value to %s", ctime(&t0));
					config.utc_refvalue = t;
					states.pts_initialized = NO;
				}

				states.programme_info_processed = YES;
			}
		}
	}
}

void process_pes_packet(uint8_t *buffer, uint16_t size) {
	if (size < 6) return;

	// Packetized Elementary Stream (PES) 32-bit start code
	uint64_t pes_prefix = (buffer[0] << 16) | (buffer[1] << 8) | buffer[2];
	uint8_t pes_stream_id = buffer[3];

	// check for PES header
	if (pes_prefix != 0x000001) return;

	// stream_id is not "Private Stream 1" (0xbd)
	if (pes_stream_id != 0xbd) return;

	// PES packet length
	// ETSI EN 301 775 V1.2.1 (2003-05) chapter 4.3: (N x 184) - 6 + 6 B header
	uint16_t pes_packet_length = 6 + ((buffer[4] << 8) | buffer[5]);
	// Can be zero. If the "PES packet length" is set to zero, the PES packet can be of any length.
	// A value of zero for the PES packet length can be used only when the PES packet payload is a video elementary stream.
	if (pes_packet_length == 6) return;

	// truncate incomplete PES packets
	if (pes_packet_length > size) pes_packet_length = size;

	uint8_t optional_pes_header_included = NO;
	uint16_t optional_pes_header_length = 0;
	// optional PES header marker bits (10.. ....)
	if ((buffer[6] & 0xc0) == 0x80) {
		optional_pes_header_included = YES;
		optional_pes_header_length = buffer[8];
	}

	// should we use PTS or PCR?
	static uint8_t using_pts = UNDEF;
	if (using_pts == UNDEF) {
		if ((optional_pes_header_included == YES) && ((buffer[7] & 0x80) > 0)) {
			using_pts = YES;
			VERBOSE_ONLY fprintf(stderr, "- PID 0xbd PTS available\n");
		} else {
			using_pts = NO;
			VERBOSE_ONLY fprintf(stderr, "- PID 0xbd PTS unavailable, using TS PCR\n");
		}
	}

	uint32_t t = 0;
	// If there is no PTS available, use global PCR
	if (using_pts == NO) {
		t = global_timestamp;
	}
	else {
		// PTS is 33 bits wide, however, timestamp in ms fits into 32 bits nicely (PTS/90)
		// presentation and decoder timestamps use the 90 KHz clock, hence PTS/90 = [ms]
		uint64_t pts = 0;
		// __MUST__ assign value to uint64_t and __THEN__ rotate left by 29 bits
		// << is defined for signed int (as in "C" spec.) and overflow occures
		pts = (buffer[9] & 0x0e);
		pts <<= 29;
		pts |= (buffer[10] << 22);
		pts |= ((buffer[11] & 0xfe) << 14);
		pts |= (buffer[12] << 7);
		pts |= ((buffer[13] & 0xfe) >> 1);
		t = pts / 90;
	}

	static int64_t delta = 0;
	static uint32_t t0 = 0;
	if (states.pts_initialized == NO) {
		delta = 1000 * config.offset + 1000 * config.utc_refvalue - t;
		states.pts_initialized = YES;

		if ((using_pts == NO) && (global_timestamp == 0)) {
			// We are using global PCR, nevertheless we still have not received valid PCR timestamp yet
			states.pts_initialized = NO;
		}
	}
	if (t < t0) delta = last_timestamp;
	last_timestamp = t + delta;
	t0 = t;

	// skip optional PES header and process each 46 bytes long teletext packet
	uint16_t i = 7;
	if (optional_pes_header_included == YES) i += 3 + optional_pes_header_length;
	while (i <= pes_packet_length - 6) {
		uint8_t data_unit_id = buffer[i++];
		uint8_t data_unit_len = buffer[i++];

		if ((data_unit_id == DATA_UNIT_EBU_TELETEXT_NONSUBTITLE) || (data_unit_id == DATA_UNIT_EBU_TELETEXT_SUBTITLE)) {
			// teletext payload has always size 44 bytes
			if (data_unit_len == 44) {
				// reverse endianess (via lookup table), ETS 300 706, chapter 7.1
				for (uint8_t j = 0; j < data_unit_len; j++) buffer[i + j] = REVERSE_8[buffer[i + j]];

				// FIXME: This explicit type conversion could be a problem some day -- do not need to be platform independant
				process_telx_packet(data_unit_id, (teletext_packet_payload_t *)&buffer[i], last_timestamp);
			}
		}

		i += data_unit_len;
	}
}

void analyze_pat(uint8_t *buffer, uint8_t size) {
	if (size < 7) return;

	pat_t pat = { 0 };
	pat.pointer_field = buffer[0];

// FIXME
if (pat.pointer_field > 0) {
	fprintf(stderr, "! pat.pointer_field > 0 (0x%02x)\n\n", pat.pointer_field);
	return;
}

	pat.table_id = buffer[1];
	if (pat.table_id == 0x00) {
		pat.section_length = ((buffer[2] & 0x03) << 8) | buffer[3];
		pat.current_next_indicator = buffer[6] & 0x01;
		// already valid PAT
		if (pat.current_next_indicator == 1) {
			uint16_t i = 9;
			while ((i < 9 + (pat.section_length - 5 - 4)) && (i < size)) {
				pat_section_t section = { 0 };
				section.program_num = (buffer[i] << 8) | buffer[i + 1];
				section.program_pid = ((buffer[i + 2] & 0x1f) << 8) | buffer[i + 3];

				if (in_array(pmt_map, pmt_map_count, section.program_pid) == NO) {
					if (pmt_map_count < TS_PMT_MAP_SIZE) {
						pmt_map[pmt_map_count++] = section.program_pid;
#ifdef DEBUG
						fprintf(stderr, "# Found PMT for SID %"PRIu16" (0x%x)\n", section.program_num, section.program_num);
#endif
					}
				}

				i += 4;
			}
		}
	}
}

void analyze_pmt(uint8_t *buffer, uint8_t size) {
	if (size < 7) return;

	pmt_t pmt = { 0 };
	pmt.pointer_field = buffer[0];

// FIXME
if (pmt.pointer_field > 0) {
	fprintf(stderr, "! pmt.pointer_field > 0 (0x%02x)\n\n", pmt.pointer_field);
	return;
}

	pmt.table_id = buffer[1];
	if (pmt.table_id == 0x02) {
		pmt.section_length = ((buffer[2] & 0x03) << 8) | buffer[3];
		pmt.program_num = (buffer[4] << 8) | buffer[5];
		pmt.current_next_indicator = buffer[6] & 0x01;
		pmt.pcr_pid = ((buffer[9] & 0x1f) << 8) | buffer[10];
		pmt.program_info_length = ((buffer[11] & 0x03) << 8) | buffer[12];
		// already valid PMT
		if (pmt.current_next_indicator == 1) {
			uint16_t i = 13 + pmt.program_info_length;
			while ((i < 13 + (pmt.program_info_length + pmt.section_length - 4 - 9)) && (i < size)) {
				pmt_program_descriptor_t desc = { 0 };
				desc.stream_type = buffer[i];
				desc.elementary_pid = ((buffer[i + 1] & 0x1f) << 8) | buffer[i + 2];
				desc.es_info_length = ((buffer[i + 3] & 0x03) << 8) | buffer[i + 4];

				uint8_t descriptor_tag = buffer[i + 5];
 				// descriptor_tag: 0x45 = VBI_data_descriptor, 0x46 = VBI_teletext_descriptor, 0x56 = teletext_descriptor
				if ((desc.stream_type == 0x06) && ((descriptor_tag == 0x45) || (descriptor_tag == 0x46) || (descriptor_tag == 0x56))) {
					if (in_array(pmt_ttxt_map, pmt_ttxt_map_count, desc.elementary_pid) == NO) {
						if (pmt_ttxt_map_count < TS_PMT_TTXT_MAP_SIZE) {
							pmt_ttxt_map[pmt_ttxt_map_count++] = desc.elementary_pid;
							if (config.tid == 0) config.tid = desc.elementary_pid;
							fprintf(stderr, "- Found VBI/teletext stream ID %"PRIu16" (0x%x) for SID %"PRIu16" (0x%x)\n", desc.elementary_pid, desc.elementary_pid, pmt.program_num, pmt.program_num);
						}
					}
				}

				i += 5 + desc.es_info_length;
			}
		}
	}
}

// graceful exit support
uint8_t exit_request = NO;

void signal_handler(int sig) {
	if ((sig == SIGINT) || (sig == SIGTERM)) {
		fprintf(stderr, "- SIGINT/SIGTERM received, preparing graceful exit\n");
		exit_request = YES;
	}
}

char* basename(const char *s) {
	char *r = (char *)s;
	while (*s) if (*s++ == '/') r = (char *)s;
	return r;
}

// main
int main(const int argc, char *argv[]) {
	int ret = EXIT_FAILURE;
	
	if ((argc > 1) && (strcmp(argv[1], "-V") == 0)) {
		fprintf(stderr, "%s\n", TELXCC_VERSION);
		ret = EXIT_SUCCESS;
		goto fail;
	}

#ifdef __MINGW32__
	int argwc = 0;
	wchar_t **argw = CommandLineToArgvW(GetCommandLineW(), &argwc);
	if ((argw == NULL) || (argwc != argc)) {
		fprintf(stderr, "! Could not process Windows UNICODE command line parameters.\n\n");
		goto fail;
	}
#endif

	fprintf(stderr, "telxcc - TELeteXt Closed Captions decoder\n");
	fprintf(stderr, "(c) Forers, s. r. o., <info@forers.com>, 2011-2014; Licensed under the GPL.\n");
	fprintf(stderr, "Version %s (%s), Built on %s\n", TELXCC_VERSION, PLATFORM, __DATE__);
	fprintf(stderr, "\n");

	// command line params parsing
	for (uint8_t i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0) {
			fprintf(stderr, "Usage: %s -h\n", basename(argv[0]));
			fprintf(stderr, "  or   %s -V\n", basename(argv[0]));
			fprintf(stderr, "  or   %s [-v] [-m] [-i INPUT] [-o OUTPUT] [-p PAGE] [-t TID] [-f OFFSET] [-n] [-1] [-c] [-s [REF]]\n", basename(argv[0]));
			fprintf(stderr, "\n");
			fprintf(stderr, "  -h          this help text\n");
			fprintf(stderr, "  -V          print out version and quit\n");
			fprintf(stderr, "  -v          be verbose\n");
			fprintf(stderr, "  -m          input file format is BDAV MPEG-2 Transport Stream (BluRay and some IP-TV recorders)\n");
			fprintf(stderr, "  -i INPUT    transport stream (- = STDIN, default STDIN)\n");
			fprintf(stderr, "  -o OUTPUT   subtitles in SubRip SRT file format (UTF-8 encoded, NFC) (- = STDOUT, default STDOUT)\n");
			fprintf(stderr, "  -p PAGE     teletext page number carrying closed captions\n");
			fprintf(stderr, "  -t TID      transport stream PID of teletext data sub-stream\n");
			fprintf(stderr, "              if the value of 8192 is specified, the first suitable stream will be used\n");
			fprintf(stderr, "  -f OFFSET   subtitles offset in seconds\n");
			fprintf(stderr, "  -n          do not print UTF-8 BOM characters to the file\n");
			fprintf(stderr, "  -1          produce at least one (dummy) frame\n");
			fprintf(stderr, "  -c          output colour information in font HTML tags\n");
			//fprintf(stderr, "  -F FORMAT   //FIXME\n");
			fprintf(stderr, "  -s [REF]    search engine mode; produce absolute timestamps in UTC and output data in one line\n");
			fprintf(stderr, "              if REF (unix timestamp) is omitted, use current system time,\n");
			fprintf(stderr, "              telxcc will automatically switch to transport stream UTC timestamps when available\n");
			fprintf(stderr, "\n");
			ret = EXIT_SUCCESS;
			goto fail;
		}
		else if ((strcmp(argv[i], "-i") == 0) && (argc > i + 1)) {
#ifdef __MINGW32__
			config.input_name = argw[++i];
#else
			config.input_name = argv[++i];
#endif
		}
		else if ((strcmp(argv[i], "-o") == 0) && (argc > i + 1)) {
#ifdef __MINGW32__
			config.output_name = argw[++i];
#else
			config.output_name = argv[++i];
#endif
		}
		else if ((strcmp(argv[i], "-p") == 0) && (argc > i + 1)) {
			config.page = atoi(argv[++i]);
		}
		else if ((strcmp(argv[i], "-t") == 0) && (argc > i + 1)) {
			config.tid = atoi(argv[++i]);
		}
		else if ((strcmp(argv[i], "-f") == 0) && (argc > i + 1)) {
			config.offset = atof(argv[++i]);
		}
		else if (strcmp(argv[i], "-n") == 0) {
			config.bom = NO;
		}
		else if (strcmp(argv[i], "-1") == 0) {
			config.nonempty = YES;
		}
		else if (strcmp(argv[i], "-c") == 0) {
			config.colours = YES;
		}
		//else if ((strcmp(argv[i], "-F") == 0) && (argc > i + 1)) {
		//	//FIXME
		//}
		else if (strcmp(argv[i], "-v") == 0) {
			config.verbose = YES;
		}
		else if (strcmp(argv[i], "-s") == 0) {
			config.se_mode = YES;
			uint64_t t = 0;
			if (argc > i + 1) {
				t = atoi(argv[i + 1]);
				if (t > 0) i++;
			}
			if (t <= 0) {
				time_t now = time(NULL);
				t = time(&now);
			}
			config.utc_refvalue = t;
		}
		else if (strcmp(argv[i], "-m") == 0) {
			config.m2ts = YES;
		}
		else {
			fprintf(stderr, "! Unknown option %s\n", argv[i]);
			fprintf(stderr, "- For usage options run \"%s -h\"\n\n", argv[0]);
			goto fail;
		}
	}

// for better UX in Windows we want to detect that the app is not run by "double-clicking" in Windows Explorer GUI
// raise a dialog box if the application is invoked by "double-click"
// if argc > 1 do not display warning, because telxcc could be run within scheduler or via shortcut link etc.
#ifdef __MINGW32__
	if (argc == 1) {
		HWND consoleWnd = GetConsoleWindow();
		DWORD dwProcessId = 0;
		GetWindowThreadProcessId(consoleWnd, &dwProcessId);

		if (GetCurrentProcessId() == dwProcessId) {
			INITCOMMONCONTROLSEX iccx = {
				.dwSize = sizeof(iccx),
				.dwICC = ICC_STANDARD_CLASSES
			};
			InitCommonControlsEx(&iccx);
			MessageBox(NULL, "telxcc is a console application. Please run it from command line (cmd.exe), scheduler or another application.", "telxcc", MB_OK | MB_ICONWARNING);
			goto fail;
		}
	}
#endif

	if (config.m2ts == YES) {
		fprintf(stderr, "- Processing input stream as a BDAV MPEG-2 Transport Stream\n");
	}

	if (config.se_mode == YES) {
		time_t t0 = (time_t)config.utc_refvalue;
		fprintf(stderr, "- Search engine mode active, UTC referential value = %s", ctime(&t0));
	}

	// teletext page number out of range
	if ((config.page != 0) && ((config.page < 100) || (config.page > 899))) {
		fprintf(stderr, "! Teletext page number could not be lower than 100 or higher than 899\n\n");
		goto fail;
	}

	// default teletext page
	if (config.page > 0) {
		// dec to BCD, magazine pages numbers are in BCD (ETSI 300 706)
		config.page = ((config.page / 100) << 8) | (((config.page / 10) % 10) << 4) | (config.page % 10);
	}

	// PID out of range
	if (config.tid > 0x2000) {
		fprintf(stderr, "! Transport stream PID could not be higher than 8192\n\n");
		goto fail;
	}

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

#ifdef __MINGW32__
	if ((config.input_name == NULL) || (wcscmp(config.input_name, L"-") == 0)) {
		fin = stdin;
	} else {
		if ((fin = _wfopen(config.input_name, L"rb")) == NULL) {
			fprintf(stderr, "! Could not open input file.\n\n");
			goto fail;
		}
	}
#else
	if ((config.input_name == NULL) || (strcmp(config.input_name, "-") == 0)) {
		fin = stdin;
	} else {
		if ((fin = fopen(config.input_name, "rb")) == NULL) {
			fprintf(stderr, "! Could not open input file \"%s\".\n\n", config.input_name);
			goto fail;
		}
	}
#endif

	if (isatty(fileno(fin))) {
		fprintf(stderr, "! STDIN is a terminal. STDIN must be redirected.\n\n");
		goto fail;
	}

#ifdef __MINGW32__
	if ((config.output_name == NULL) || (wcscmp(config.output_name, L"-") == 0)) {
		fout = stdout;
	} else {
		if ((fout = _wfopen(config.output_name, L"wb")) == NULL) {
			fprintf(stderr, "! Could not open output file.\n\n");
			goto fail;
		}
	}
#else
	if ((config.output_name == NULL) || (strcmp(config.output_name, "-") == 0)) {
		fout = stdout;
	} else {
		if ((fout = fopen(config.output_name, "wb")) == NULL) {
			fprintf(stderr, "! Could not open output file \"%s\".\n\n", config.output_name);
			goto fail;
		}
	}
#endif

#ifdef __MINGW32__
	if (isatty(fileno(fout))) {
		fprintf(stderr, "! On Windows platform produced closed captions do not have the same encoding as the command line terminal (UTF-8 vs UCS-2). Hence CC could not be printed directly to the terminal.\n\n");
		goto fail;
	}
#endif

	if (isatty(fileno(fout))) {
		fprintf(stderr, "- STDOUT is a terminal, omitting UTF-8 BOM sequence on the output.\n");
		config.bom = NO;
	}

	// full buffering -- disables flushing after CR/FL, we will flush manually whole SRT frames
	setvbuf(fout, (char*)NULL, _IOFBF, 0);
	
	// print UTF-8 BOM chars
	if (config.bom == YES) {
		fprintf(fout, "\xef\xbb\xbf");
		fflush(fout);
	}

	// PROCESING

	// FYI, packet counter
	uint32_t packet_counter = 0;

	// TS packet buffer
	uint8_t ts_packet_buffer[TS_PACKET_SIZE] = { 0 };
	uint8_t ts_packet_size = TS_PACKET_SIZE - 4;

	// pointer to TS packet buffer start
	uint8_t *ts_packet = &ts_packet_buffer[0];

	// if telxcc is configured to be in M2TS mode, it reads larger packets and ignores first 4 bytes
	if (config.m2ts == YES) {
		ts_packet_size = TS_PACKET_SIZE;
		ts_packet = &ts_packet_buffer[4];
	}

	// 0xff means not set yet
	uint8_t continuity_counter = 255;

	// PES packet buffer
	uint8_t payload_buffer[PAYLOAD_BUFFER_SIZE] = { 0 };
	uint16_t payload_counter = 0;

	// reading input
	while ((exit_request == NO) && (fread(&ts_packet_buffer, 1, ts_packet_size, fin) == ts_packet_size)) {
		// not TS packet -- misaligned?
		if (ts_packet[0] != 0x47) {
			fprintf(stderr, "! Invalid TS packet header; TS seems to be misaligned\n");

			uint16_t shift = 0;
			for (shift = 1; shift < TS_PACKET_SIZE; shift++) if (ts_packet[shift] == 0x47) break;

			if (shift < TS_PACKET_SIZE) {
				VERBOSE_ONLY fprintf(stderr, "! TS-packet-header-like byte found shifted by %"PRIu16" bytes, aligning TS stream (at least one TS packet lost)\n", shift);
				for (uint16_t i = shift; i < TS_PACKET_SIZE; i++) ts_packet[i - shift] = ts_packet[i];
				fread(&ts_packet[TS_PACKET_SIZE - shift], 1, shift, fin);
			}
		}

		// Transport Stream Header
		// We do not use buffer to struct loading (e.g. ts_packet_t *header = (ts_packet_t *)ts_packet;)
		// -- struct packing is platform dependant and not performing well.
		ts_packet_t header = { 0 };
		header.sync = ts_packet[0];
		header.transport_error = (ts_packet[1] & 0x80) >> 7;
		header.payload_unit_start = (ts_packet[1] & 0x40) >> 6;
		header.transport_priority = (ts_packet[1] & 0x20) >> 5;
		header.pid = ((ts_packet[1] & 0x1f) << 8) | ts_packet[2];
		header.scrambling_control = (ts_packet[3] & 0xc0) >> 6;
		header.adaptation_field_exists = (ts_packet[3] & 0x20) >> 5;
		header.continuity_counter = ts_packet[3] & 0x0f;
		//uint8_t ts_payload_exists = (ts_packet[3] & 0x10) >> 4;

		uint8_t af_discontinuity = 0;
		if (header.adaptation_field_exists > 0) {
			af_discontinuity = (ts_packet[5] & 0x80) >> 7;
		}

		// uncorrectable error?
		if (header.transport_error > 0) {
			VERBOSE_ONLY fprintf(stderr, "! Uncorrectable TS packet error (received CC %1x)\n", header.continuity_counter);
			continue;
		}

		// if available, calculate current PCR
		if (header.adaptation_field_exists > 0) {
			// PCR in adaptation field
			uint8_t af_pcr_exists = (ts_packet[5] & 0x10) >> 4;
			if (af_pcr_exists > 0) {
				uint64_t pts = ts_packet[6];
				pts <<= 25;
				pts |= (ts_packet[7] << 17);
				pts |= (ts_packet[8] << 9);
				pts |= (ts_packet[9] << 1);
				pts |= (ts_packet[10] >> 7);
				global_timestamp = pts / 90;
				pts = ((ts_packet[10] & 0x01) << 8);
				pts |= ts_packet[11];
				global_timestamp += pts / 27000;
			}
		}

		// null packet
		if (header.pid == 0x1fff) continue;

		// TID not specified, autodetect via PAT/PMT
		if (config.tid == 0) {
			// process PAT
			if (header.pid == 0x0000) {
				analyze_pat(&ts_packet[4], TS_PACKET_PAYLOAD_SIZE);
				continue;
			}

			// process PMT
			if (in_array(pmt_map, pmt_map_count, header.pid) == YES) {
				analyze_pmt(&ts_packet[4], TS_PACKET_PAYLOAD_SIZE);
				continue;
			}
		}

		// TID 0x2000 specified => dummy autodetection
		if (config.tid == 0x2000) {
			if (header.payload_unit_start > 0) {
				// searching for PES header and "Private Stream 1" stream_id
				uint64_t pes_prefix = (ts_packet[4] << 16) | (ts_packet[5] << 8) | ts_packet[6];
				uint8_t pes_stream_id = ts_packet[7];

				if ((pes_prefix == 0x000001) && (pes_stream_id == 0xbd)) {
					config.tid = header.pid;
					fprintf(stderr, "- No teletext PID specified, first received suitable stream PID is %"PRIu16" (0x%x), not guaranteed\n", config.tid, config.tid);
					continue;
				}
			}
		}

		if (config.tid == header.pid) {
			// TS continuity check
			if (continuity_counter == 255) continuity_counter = header.continuity_counter;
			else {
				if (af_discontinuity == 0) {
					continuity_counter = (continuity_counter + 1) % 16;
					if (header.continuity_counter != continuity_counter) {
						VERBOSE_ONLY fprintf(stderr, "- Missing TS packet, flushing pes_buffer (expected CC %1x, received CC %1x, TS discontinuity %s, TS priority %s)\n",
							continuity_counter, header.continuity_counter, (af_discontinuity ? "YES" : "NO"), (header.transport_priority ? "YES" : "NO"));
						payload_counter = 0;
						continuity_counter = 255;
					}
				}
			}

			// waiting for first payload_unit_start indicator
			if ((header.payload_unit_start == 0) && (payload_counter == 0)) continue;

			// proceed with payload buffer
			if ((header.payload_unit_start > 0) && (payload_counter > 0)) process_pes_packet(payload_buffer, payload_counter);

			// new payload frame start
			if (header.payload_unit_start > 0) payload_counter = 0;

			// add payload data to buffer
			if (payload_counter < (PAYLOAD_BUFFER_SIZE - TS_PACKET_PAYLOAD_SIZE)) {
				memcpy(&payload_buffer[payload_counter], &ts_packet[4], TS_PACKET_PAYLOAD_SIZE);
				payload_counter += TS_PACKET_PAYLOAD_SIZE;
				packet_counter++;
			}
			else VERBOSE_ONLY fprintf(stderr, "! Packet payload size exceeds payload_buffer size, probably not teletext stream\n");
		}
	}

	// output any pending close caption
	if (page_buffer.tainted == YES) {
		// this time we do not subtract any frames, there will be no more frames
		page_buffer.hide_timestamp = last_timestamp;
		process_page(&page_buffer);
	}

	VERBOSE_ONLY {
		if (config.tid == 0) fprintf(stderr, "- No teletext PID specified, no suitable PID found in PAT/PMT tables. Please specify teletext PID via -t parameter.\n  You can also specify -t 8192 for another type of autodetection (choosing the first suitable stream)\n");
		if (frames_produced == 0) fprintf(stderr, "- No frames produced. CC teletext page number was probably wrong.\n");
		fprintf(stderr, "- There were some CC data carried via pages = ");
		// We ignore i = 0xff, because 0xffs are teletext ending frames
		for (uint16_t i = 0; i < 255; i++) {
			for (uint8_t j = 0; j < 8; j++) {
				uint8_t v = cc_map[i] & (1 << j);
				if (v > 0) fprintf(stderr, "%03x ", ((j + 1) << 8) | i);
			}
		}
		fprintf(stderr, "\n");
	}

	if ((config.se_mode == NO) && (frames_produced == 0) && (config.nonempty == YES)) {
		fprintf(fout, "1\r\n00:00:00,000 --> 00:00:10,000\r\n.\r\n\r\n");
		fflush(fout);
		frames_produced++;
	}

	fprintf(stderr, "- Done (%"PRIu32" teletext packets processed, %"PRIu32" frames produced)\n", packet_counter, frames_produced);
	fprintf(stderr, "\n");

	ret = EXIT_SUCCESS;

fail:

	if ((fin != NULL) && (fin != stdin)) {
		fclose(fin);
		fin = NULL;
	}

	if ((fout != NULL) && (fout != stdout)) {
		fclose(fout);
		fout = NULL;
	}

#ifdef __MINGW32__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
	if (argw != NULL) {
		LocalFree(argw);
		argw = NULL;
		argwc = 0;
	}
#pragma GCC diagnostic pop
#endif

	return ret;
}
