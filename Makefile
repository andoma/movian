#
#  Movian
#  Copyright (C) 2007-2011 Andreas Öman
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <http://www.gnu.org/licenses/>.

.SUFFIXES:
SUFFIXES=


C ?= ${CURDIR}

include ${C}/config.default
BUILDDIR ?= ${C}/build.${BUILD}

# All targets deps on Makefile, but we can comment that out during dev:ing
ALLDEPS=${BUILDDIR}/config.mak Makefile src/arch/${OS}/${OS}.mk

ALLDEPS += ${STAMPS}

OPTFLAGS ?= -O${OPTLEVEL}

PROG=${BUILDDIR}/movian
LIB=${BUILDDIR}/libmovian

include ${BUILDDIR}/config.mak

CFLAGS_std += -Wall -Werror -Wwrite-strings -Wno-deprecated-declarations \
		-Wmissing-prototypes -Wno-multichar  -Iext/dvd -std=gnu99

CFLAGS = ${CFLAGS_std} ${OPTFLAGS}

#PGFLAGS ?= -pg

OPTFLAGS += ${PGFLAGS}
LDFLAGS += ${PGFLAGS}


##############################################################
# Core
##############################################################
SRCS += src/main.c \
	src/trace.c \
	src/task.c \
	src/runcontrol.c \
	src/version.c \
	src/navigator.c \
	src/backend/backend.c \
	src/backend/backend_prop.c \
	src/backend/search.c \
	src/event.c \
	src/keyring.c \
	src/settings.c \
	src/service.c \
	src/notifications.c \
	src/plugins.c \
	src/upgrade.c \
	src/blobcache_file.c \
	src/i18n.c \
	src/prop/prop_core.c \
	src/prop/prop_test.c \
	src/prop/prop_nodefilter.c \
	src/prop/prop_tags.c \
	src/prop/prop_vector.c \
	src/prop/prop_grouper.c \
	src/prop/prop_concat.c \
	src/prop/prop_reorder.c \
	src/prop/prop_linkselected.c \
	src/prop/prop_window.c \
	src/prop/prop_proxy.c \
	src/metadata/playinfo.c \
	src/db/kvstore.c \
	src/backend/slideshow/slideshow.c \

SRCS +=	src/media/media.c \
	src/media/media_buf.c \
	src/media/media_track.c \
	src/media/media_queue.c \
	src/media/media_codec.c \
	src/media/media_event.c \

SRCS-${CONFIG_MEDIA_SETTINGS} += src/media/media_settings.c

SRCS-${CONFIG_LIBAV} += src/libav.c

SRCS-${CONFIG_EMU_THREAD_SPECIFICS} += src/arch/emu_thread_specifics.c



SRCS-$(CONFIG_WEBPOPUP) += src/ui/webpopup.c

SRCS-$(CONFIG_USAGEREPORT) += src/usage.c

SRCS-$(CONFIG_PLAYQUEUE) += src/playqueue.c

SRCS-$(CONFIG_HTTPSERVER) += src/prop/prop_http.c

##############################################################
# Images
##############################################################
SRCS +=	src/image/image.c \
	src/image/pixmap.c \
	src/image/nanosvg.c \
	src/image/svg.c \
	src/image/rasterizer_ft.c \
	src/image/jpeg.c \
	src/image/vector.c \
	src/image/image_decoder_libav.c \
	src/image/dominantcolor.c \

##############################################################
# Misc support
##############################################################
SRCS +=	src/misc/ptrvec.c \
	src/misc/average.c \
	src/misc/callout.c \
	src/misc/rstr.c \
	src/misc/gz.c \
	src/misc/str.c \
	src/misc/time.c \
	src/misc/codepages.c \
	src/misc/extents.c \
	src/misc/isolang.c \
	src/misc/dbl.c \
	src/misc/json.c \
	src/misc/unicode_composition.c \
	src/misc/pool.c \
	src/misc/buf.c \
	src/misc/charset_detector.c \
	src/misc/big5.c \
	src/misc/cancellable.c \
	src/misc/lockmgr.c \
	src/misc/prng.c \

SRCS += ext/trex/trex.c

SRCS-${CONFIG_BSPATCH} += ext/bspatch/bspatch.c

##############################################################
# Metadata system
##############################################################

SRCS-$(CONFIG_METADATA) += src/metadata/metadb.c \
			   src/metadata/mlp.c \
			   src/metadata/metadata_sources.c \
			   src/metadata/browsemdb.c \
			   src/metadata/decoration.c \
			   src/metadata/metadata.c \
			   src/metadata/metadata_str.c \

SRCS-$(CONFIG_METADATA) += src/fileaccess/fa_indexer.c \
			   src/fileaccess/fa_probe.c \
			   src/fileaccess/fa_scanner.c

SRCS-$(CONFIG_METADATA) += src/api/lastfm.c \
			   src/api/tmdb.c \
			   src/api/tvdb.c \


BUNDLES-$(CONFIG_METADATA) += res/metadb


##############################################################
# Sqlite3
##############################################################
SRCS-${CONFIG_SQLITE_INTERNAL} += ext/sqlite/sqlite3.c

SRCS-$(CONFIG_SQLITE) += src/db/db_support.c



${BUILDDIR}/ext/sqlite/sqlite3.o : CFLAGS = -O2 ${SQLITE_CFLAGS_cfg} \
 -DSQLITE_THREADSAFE=2 \
 -DSQLITE_OMIT_UTF16 \
 -DSQLITE_OMIT_AUTOINIT \
 -DSQLITE_OMIT_COMPLETE \
 -DSQLITE_OMIT_DECLTYPE \
 -DSQLITE_OMIT_DEPRECATED \
 -DSQLITE_OMIT_GET_TABLE \
 -DSQLITE_OMIT_TCL_VARIABLE \
 -DSQLITE_OMIT_LOAD_EXTENSION \
 -DSQLITE_DEFAULT_FOREIGN_KEYS=1 \
 -DSQLITE_ENABLE_UNLOCK_NOTIFY \


SRCS-$(CONFIG_SQLITE_VFS) += src/db/vfs.c

BUNDLES-$(CONFIG_KVSTORE) += res/kvstore

##############################################################
# HTSMSG
##############################################################
SRCS +=	src/htsmsg/htsbuf.c \
	src/htsmsg/htsmsg.c \
	src/htsmsg/htsmsg_json.c \
	src/htsmsg/htsmsg_xml.c \
	src/htsmsg/htsmsg_binary.c \
	src/htsmsg/htsmsg_store.c \


##############################################################
# Virtual FS system
##############################################################
SRCS += src/fileaccess/fileaccess.c \
	src/fileaccess/fa_vfs.c \
	src/fileaccess/fa_http.c \
	src/fileaccess/fa_zip.c \
	src/fileaccess/fa_zlib.c \
	src/fileaccess/fa_bundle.c \
	src/fileaccess/fa_buffer.c \
	src/fileaccess/fa_slice.c \
	src/fileaccess/fa_bwlimit.c \
	src/fileaccess/fa_cmp.c \
	src/fileaccess/fa_aes.c \
	src/fileaccess/fa_data.c \
	src/fileaccess/fa_imageloader.c \
	src/fileaccess/fa_filepicker.c \

SRCS-$(CONFIG_FTPCLIENT) += src/fileaccess/fa_ftp.c \
			    src/fileaccess/ftpparse.c \

SRCS-$(CONFIG_LIBAV) += \
	src/fileaccess/fa_libav.c \
	src/fileaccess/fa_backend.c \
	src/fileaccess/fa_video.c \
	src/fileaccess/fa_audio.c \

SRCS-$(CONFIG_XMP)             += src/fileaccess/fa_xmp.c
SRCS-$(CONFIG_LIBGME)          += src/fileaccess/fa_gmefile.c
SRCS-$(CONFIG_LOCATEDB)        += src/fileaccess/fa_locatedb.c
SRCS-$(CONFIG_SPOTLIGHT)       += src/fileaccess/fa_spotlight.c
SRCS-$(CONFIG_LIBNTFS)         += src/fileaccess/fa_ntfs.c
SRCS-$(CONFIG_NATIVESMB)       += src/fileaccess/smb/fa_nativesmb.c \
				  src/fileaccess/smb/nmb.c
SRCS-$(CONFIG_RAR)             += src/fileaccess/fa_rar.c
SRCS-$(CONFIG_SID)             += src/fileaccess/fa_sidfile.c \
				  ext/audio/sid.c

BUNDLES += res/fileaccess

##############################################################
# Service Discovery
##############################################################

SRCS 			+= src/sd/sd.c
SRCS-$(CONFIG_AVAHI) 	+= src/sd/avahi.c
SRCS-$(CONFIG_BONJOUR) 	+= src/sd/bonjour.c

${BUILDDIR}/src/sd/avahi.o : CFLAGS = $(CFLAGS_AVAHI) -Wall -Werror  ${OPTFLAGS}



##############################################################
# APIs
##############################################################
SRCS += 		src/api/xmlrpc.c \
			src/api/soap.c \

SRCS-$(CONFIG_HTTPSERVER) += \
	src/api/httpcontrol.c \
	src/api/screenshot.c \
	src/api/stpp.c \

SRCS-$(CONFIG_AIRPLAY) += src/api/airplay.c

##############################################################
# Networking
##############################################################
SRCS += src/networking/net_common.c \
	src/networking/http.c \
	src/networking/asyncio_http.c \
	src/networking/websocket.c \

SRCS-$(CONFIG_FTPSERVER) += src/networking/ftp_server.c

SRCS-$(CONFIG_POLARSSL) += src/networking/net_polarssl.c
SRCS-$(CONFIG_OPENSSL)  += src/networking/net_openssl.c

SRCS-$(CONFIG_HTTPSERVER) += src/networking/http_server.c

SRCS-$(CONFIG_UPNP) +=  src/networking/ssdp.c \
			src/upnp/upnp.c \
			src/upnp/upnp_control.c \
			src/upnp/upnp_event.c \
			src/upnp/upnp_browse.c \
			src/upnp/upnp_avtransport.c \
			src/upnp/upnp_renderingcontrol.c \
			src/upnp/upnp_connectionmanager.c \

SRCS-$(CONFIG_CONNMAN) += src/networking/connman.c
SRCS-$(CONFIG_CONNMAN) += src/prop/prop_gvariant.c

##############################################################
# Video support
##############################################################
SRCS += src/video/video_playback.c \
	src/video/video_decoder.c \
	src/video/video_settings.c \
	src/video/h264_parser.c \
	src/misc/bitstream.c \
	src/video/h264_annexb.c \

SRCS-$(CONFIG_VDPAU)    += src/video/vdpau.c

SRCS-$(CONFIG_CEDAR) += \
	src/ui/glw/glw_video_sunxi.c \
	src/video/cedar.c \
	ext/tlsf/tlsf.c \
	src/arch/sunxi/sunxi.c \

##############################################################
# Subtitles
##############################################################
SRCS += src/subtitles/subtitles.c \
	src/subtitles/sub_ass.c \
	src/subtitles/ext_subtitles.c \
	src/subtitles/dvdspu.c \
	src/subtitles/vobsub.c \
	src/subtitles/video_overlay.c \

##############################################################
# Text rendering
##############################################################
SRCS-$(CONFIG_LIBFREETYPE) += src/text/freetype.c
SRCS-$(CONFIG_LIBFONTCONFIG) += src/text/fontconfig.c
SRCS += src/text/parser.c
SRCS += src/text/fontstash.c

##############################################################
# Audio subsys
##############################################################
SRCS-$(CONFIG_LIBAV) += src/audio2/audio.c

SRCS-$(CONFIG_AUDIOTEST) += src/audio2/audio_test.c

##############################################################
# DVD
##############################################################
SRCS-$(CONFIG_DVD)       += src/backend/dvd/dvd.c

##############################################################
# Bittorrent
##############################################################
SRCS-$(CONFIG_BITTORRENT) += \
	src/backend/bittorrent/bt_backend.c \
	src/backend/bittorrent/fa_torrent.c \
	src/backend/bittorrent/torrent.c \
	src/backend/bittorrent/peer.c \
	src/backend/bittorrent/diskio.c \
	src/backend/bittorrent/torrent_stats.c \
	src/backend/bittorrent/torrent_settings.c \
	src/backend/bittorrent/tracker.c \
	src/backend/bittorrent/tracker_udp.c \
	src/backend/bittorrent/tracker_http.c \
	src/backend/bittorrent/bencode.c \
	src/backend/bittorrent/magnet.c \


##############################################################
# TV
##############################################################
SRCS-$(CONFIG_HTSP)  += src/backend/htsp/htsp.c

BUNDLES-$(CONFIG_HTSP) += res/tvheadend

##############################################################
# TV
##############################################################
SRCS-$(CONFIG_HLS) += \
	src/backend/hls/hls.c \
	src/backend/hls/hls_ts.c \

##############################################################
# Icecast
##############################################################
SRCS-$(CONFIG_ICECAST)  += src/backend/icecast/icecast.c \

##############################################################
# GLW user interface
##############################################################
SRCS-$(CONFIG_GLW)   += src/ui/glw/glw.c \
			src/ui/glw/glw_style.c \
			src/ui/glw/glw_renderer.c \
			src/ui/glw/glw_event.c \
			src/ui/glw/glw_view.c \
		     	src/ui/glw/glw_view_lexer.c \
		     	src/ui/glw/glw_view_parser.c \
			src/ui/glw/glw_view_eval.c \
			src/ui/glw/glw_view_preproc.c \
			src/ui/glw/glw_view_support.c \
			src/ui/glw/glw_view_attrib.c \
			src/ui/glw/glw_view_loader.c \
			src/ui/glw/glw_dummy.c \
			src/ui/glw/glw_container.c \
			src/ui/glw/glw_cursor.c \
			src/ui/glw/glw_scroll.c \
			src/ui/glw/glw_list.c \
			src/ui/glw/glw_clist.c \
			src/ui/glw/glw_array.c \
			src/ui/glw/glw_deck.c \
			src/ui/glw/glw_playfield.c \
			src/ui/glw/glw_layer.c \
			src/ui/glw/glw_expander.c \
			src/ui/glw/glw_resizer.c \
			src/ui/glw/glw_slider.c \
			src/ui/glw/glw_rotator.c  \
			src/ui/glw/glw_detachable.c  \
			src/ui/glw/glw_throbber.c  \
			src/ui/glw/glw_slideshow.c \
			src/ui/glw/glw_freefloat.c \
			src/ui/glw/glw_transitions.c \
			src/ui/glw/glw_navigation.c \
			src/ui/glw/glw_texture_loader.c \
			src/ui/glw/glw_image.c \
			src/ui/glw/glw_text_bitmap.c \
			src/ui/glw/glw_bloom.c \
			src/ui/glw/glw_cube.c \
			src/ui/glw/glw_displacement.c \
			src/ui/glw/glw_coverflow.c \
			src/ui/glw/glw_mirror.c \
			src/ui/glw/glw_video_common.c \
			src/ui/glw/glw_video_overlay.c \
			src/ui/glw/glw_bar.c \
			src/ui/glw/glw_flicker.c \
			src/ui/glw/glw_keyintercept.c \
			src/ui/glw/glw_clip.c \
			src/ui/glw/glw_primitives.c \
			src/ui/glw/glw_math.c \
			src/ui/glw/glw_underscan.c \
			src/ui/glw/glw_popup.c \

SRCS-$(CONFIG_GLW_SETTINGS) += 	  src/ui/glw/glw_settings.c

SRCS-$(CONFIG_GLW_FRONTEND_X11)	  += src/ui/glw/glw_x11.c \
				     src/ui/linux/x11_common.c

SRCS-$(CONFIG_GLW_BACKEND_OPENGL) += src/ui/glw/glw_opengl_shaders.c \
                                     src/ui/glw/glw_opengl_ogl.c \
                                     src/ui/glw/glw_texture_opengl.c \
                                     src/ui/glw/glw_video_opengl.c \
                                     src/ui/glw/glw_video_vdpau.c \

SRCS-$(CONFIG_GLW_BACKEND_OPENGL_ES) += src/ui/glw/glw_opengl_shaders.c \
                                        src/ui/glw/glw_opengl_es.c \
                                        src/ui/glw/glw_texture_opengl.c \

SRCS-$(CONFIG_GLW_REC)            += src/ui/glw/glw_rec.c

SRCS-$(CONFIG_GLW_FRONTEND_PS3)   += src/ui/glw/glw_ps3.c
SRCS-$(CONFIG_GLW_BACKEND_RSX)    += src/ui/glw/glw_rsx.c
SRCS-$(CONFIG_GLW_BACKEND_RSX)    += src/ui/glw/glw_texture_rsx.c
SRCS-$(CONFIG_GLW_BACKEND_RSX)    += src/ui/glw/glw_video_rsx.c

SRCS-$(CONFIG_GLW_FRONTEND_WII)	  += src/ui/glw/glw_wii.c
SRCS-$(CONFIG_GLW_BACKEND_GX)     += src/ui/glw/glw_texture_gx.c
SRCS-$(CONFIG_GLW_BACKEND_GX)     += src/ui/glw/glw_gx.c
SRCS-$(CONFIG_GLW_BACKEND_GX)     += src/ui/glw/glw_video_gx.c
SRCS-$(CONFIG_GLW_BACKEND_GX)     += src/ui/glw/glw_gxasm.S

SRCS-$(CONFIG_NVCTRL)             += src/ui/linux/nvidia.c

BUNDLES-$(CONFIG_GLW_BACKEND_OPENGL)    += res/shaders/glsl
BUNDLES-$(CONFIG_GLW_BACKEND_OPENGL_ES) += res/shaders/glsl

${BUILDDIR}/src/ui/glw/%.o : CFLAGS = ${OPTFLAGS} ${CFLAGS_std} -ffast-math

##############################################################
# GTK based interface
##############################################################
SRCS-$(CONFIG_GU) +=    src/ui/gu/gu.c \
			src/ui/gu/gu_helpers.c \
			src/ui/gu/gu_pixbuf.c \
			src/ui/gu/gu_popup.c \
			src/ui/gu/gu_menu.c \
			src/ui/gu/gu_menubar.c \
			src/ui/gu/gu_toolbar.c \
			src/ui/gu/gu_statusbar.c \
			src/ui/gu/gu_playdeck.c \
			src/ui/gu/gu_pages.c \
			src/ui/gu/gu_home.c \
			src/ui/gu/gu_settings.c \
			src/ui/gu/gu_directory.c \
			src/ui/gu/gu_directory_store.c \
			src/ui/gu/gu_directory_list.c \
			src/ui/gu/gu_directory_album.c \
			src/ui/gu/gu_directory_albumcollection.c \
			src/ui/gu/gu_cell_bar.c \
			src/ui/gu/gu_video.c \
			src/ui/linux/x11_common.c \

${BUILDDIR}/src/ui/gu/%.o : CFLAGS = $(CFLAGS_GTK) ${OPTFLAGS} ${CFLAGS_std}


##############################################################
# IPC
##############################################################
SRCS-$(CONFIG_LIRC) +=  src/ipc/lirc.c
SRCS-$(CONFIG_LIBCEC) +=  src/ipc/libcec.c
SRCS-$(CONFIG_STDIN)+=  src/ipc/stdin.c

##############################################################
# RTMP
##############################################################
SRCS-$(CONFIG_LIBRTMP) +=	ext/rtmpdump/librtmp/amf.c \
				ext/rtmpdump/librtmp/hashswf.c \
				ext/rtmpdump/librtmp/log.c \
				ext/rtmpdump/librtmp/rtmp.c \
				ext/rtmpdump/librtmp/parseurl.c

${BUILDDIR}/ext/rtmpdump/librtmp/%.o : CFLAGS = ${OPTFLAGS}

SRCS-$(CONFIG_LIBRTMP)  +=      src/backend/rtmp/rtmp.c

${BUILDDIR}/src/backend/rtmp/rtmp.o : CFLAGS = ${OPTFLAGS} -Wall -Werror -Iext/rtmpdump


##############################################################
# DVD
##############################################################
SRCS-$(CONFIG_DVD) += 	ext/dvd/dvdcss/css.c \
			ext/dvd/dvdcss/device.c \
			ext/dvd/dvdcss/error.c \
			ext/dvd/dvdcss/ioctl.c \
			ext/dvd/dvdcss/libdvdcss.c \
			ext/dvd/libdvdread/dvd_input.c \
			ext/dvd/libdvdread/dvd_reader.c \
			ext/dvd/libdvdread/dvd_udf.c \
			ext/dvd/libdvdread/ifo_read.c \
			ext/dvd/libdvdread/md5.c \
			ext/dvd/libdvdread/nav_read.c \
			ext/dvd/libdvdread/bitreader.c \
			ext/dvd/dvdnav/dvdnav.c \
			ext/dvd/dvdnav/highlight.c \
			ext/dvd/dvdnav/navigation.c \
			ext/dvd/dvdnav/read_cache.c \
			ext/dvd/dvdnav/remap.c \
			ext/dvd/dvdnav/settings.c \
			ext/dvd/dvdnav/vm/vm.c \
			ext/dvd/dvdnav/vm/decoder.c \
			ext/dvd/dvdnav/vm/vmcmd.c \
			ext/dvd/dvdnav/searching.c

${BUILDDIR}/ext/dvd/dvdcss/%.o : CFLAGS = ${OPTFLAGS} \
 -DHAVE_LIMITS_H -DHAVE_UNISTD_H -DHAVE_ERRNO_H -DVERSION="0" $(DVDCSS_CFLAGS)

${BUILDDIR}/ext/dvd/libdvdread/%.o : CFLAGS = ${OPTFLAGS} \
 -DHAVE_DVDCSS_DVDCSS_H -DDVDNAV_COMPILE -Wno-strict-aliasing  -Iext/dvd 

${BUILDDIR}/ext/dvd/dvdnav/%.o : CFLAGS = ${OPTFLAGS} \
 -DVERSION=\"movian\" -DDVDNAV_COMPILE -Wno-strict-aliasing -Iext/dvd \
 -Iext/dvd/dvdnav


##############################################################
# polarssl
##############################################################
SRCS-$(CONFIG_POLARSSL) += \
	ext/polarssl-1.3/library/aes.c \
	ext/polarssl-1.3/library/aesni.c \
	ext/polarssl-1.3/library/arc4.c \
	ext/polarssl-1.3/library/asn1parse.c \
	ext/polarssl-1.3/library/asn1write.c \
	ext/polarssl-1.3/library/base64.c \
	ext/polarssl-1.3/library/bignum.c \
	ext/polarssl-1.3/library/blowfish.c \
	ext/polarssl-1.3/library/camellia.c \
	ext/polarssl-1.3/library/ccm.c \
	ext/polarssl-1.3/library/certs.c \
	ext/polarssl-1.3/library/cipher.c \
	ext/polarssl-1.3/library/cipher_wrap.c \
	ext/polarssl-1.3/library/ctr_drbg.c \
	ext/polarssl-1.3/library/debug.c \
	ext/polarssl-1.3/library/des.c \
	ext/polarssl-1.3/library/dhm.c \
	ext/polarssl-1.3/library/ecdh.c \
	ext/polarssl-1.3/library/ecdsa.c \
	ext/polarssl-1.3/library/ecp.c \
	ext/polarssl-1.3/library/ecp_curves.c \
	ext/polarssl-1.3/library/entropy.c \
	ext/polarssl-1.3/library/entropy_poll.c \
	ext/polarssl-1.3/library/error.c \
	ext/polarssl-1.3/library/gcm.c \
	ext/polarssl-1.3/library/havege.c \
	ext/polarssl-1.3/library/hmac_drbg.c \
	ext/polarssl-1.3/library/md2.c \
	ext/polarssl-1.3/library/md4.c \
	ext/polarssl-1.3/library/md5.c \
	ext/polarssl-1.3/library/md.c \
	ext/polarssl-1.3/library/md_wrap.c \
	ext/polarssl-1.3/library/memory_buffer_alloc.c \
	ext/polarssl-1.3/library/net.c \
	ext/polarssl-1.3/library/oid.c \
	ext/polarssl-1.3/library/padlock.c \
	ext/polarssl-1.3/library/pbkdf2.c \
	ext/polarssl-1.3/library/pem.c \
	ext/polarssl-1.3/library/pk.c \
	ext/polarssl-1.3/library/pkcs11.c \
	ext/polarssl-1.3/library/pkcs12.c \
	ext/polarssl-1.3/library/pkcs5.c \
	ext/polarssl-1.3/library/pkparse.c \
	ext/polarssl-1.3/library/pk_wrap.c \
	ext/polarssl-1.3/library/pkwrite.c \
	ext/polarssl-1.3/library/platform.c \
	ext/polarssl-1.3/library/ripemd160.c \
	ext/polarssl-1.3/library/rsa.c \
	ext/polarssl-1.3/library/sha1.c \
	ext/polarssl-1.3/library/sha256.c \
	ext/polarssl-1.3/library/sha512.c \
	ext/polarssl-1.3/library/ssl_cache.c \
	ext/polarssl-1.3/library/ssl_ciphersuites.c \
	ext/polarssl-1.3/library/ssl_cli.c \
	ext/polarssl-1.3/library/ssl_srv.c \
	ext/polarssl-1.3/library/ssl_tls.c \
	ext/polarssl-1.3/library/threading.c \
	ext/polarssl-1.3/library/timing.c \
	ext/polarssl-1.3/library/version.c \
	ext/polarssl-1.3/library/version_features.c \
	ext/polarssl-1.3/library/x509.c \
	ext/polarssl-1.3/library/x509_create.c \
	ext/polarssl-1.3/library/x509_crl.c \
	ext/polarssl-1.3/library/x509_crt.c \
	ext/polarssl-1.3/library/x509_csr.c \
	ext/polarssl-1.3/library/x509write_crt.c \
	ext/polarssl-1.3/library/x509write_csr.c \
	ext/polarssl-1.3/library/xtea.c \


${BUILDDIR}/ext/polarssl-1.3/library/%.o : CFLAGS = -Wall ${OPTFLAGS}


ifeq ($(CONFIG_POLARSSL), yes)
CFLAGS_com += -Iext/polarssl-1.3/include
endif

##############################################################
# 
##############################################################

SRCS-$(CONFIG_LIBNTFS) += \
	ext/libntfs_ext/source/acls.c \
	ext/libntfs_ext/source/attrib.c \
	ext/libntfs_ext/source/attrlist.c \
	ext/libntfs_ext/source/bitmap.c \
	ext/libntfs_ext/source/bootsect.c \
	ext/libntfs_ext/source/cache.c \
	ext/libntfs_ext/source/cache2.c \
	ext/libntfs_ext/source/collate.c \
	ext/libntfs_ext/source/compress.c \
	ext/libntfs_ext/source/debug.c \
	ext/libntfs_ext/source/device.c \
	ext/libntfs_ext/source/device_io.c \
	ext/libntfs_ext/source/dir.c \
	ext/libntfs_ext/source/efs.c \
	ext/libntfs_ext/source/gekko_io.c \
	ext/libntfs_ext/source/index.c \
	ext/libntfs_ext/source/inode.c \
	ext/libntfs_ext/source/lcnalloc.c \
	ext/libntfs_ext/source/logfile.c \
	ext/libntfs_ext/source/logging.c \
	ext/libntfs_ext/source/mft.c \
	ext/libntfs_ext/source/misc.c \
	ext/libntfs_ext/source/mst.c \
	ext/libntfs_ext/source/ntfs.c \
	ext/libntfs_ext/source/ntfsdir.c \
	ext/libntfs_ext/source/ntfsfile.c \
	ext/libntfs_ext/source/ntfsinternal.c \
	ext/libntfs_ext/source/object_id.c \
	ext/libntfs_ext/source/realpath.c \
	ext/libntfs_ext/source/reparse.c \
	ext/libntfs_ext/source/runlist.c \
	ext/libntfs_ext/source/security.c \
	ext/libntfs_ext/source/unistr.c \
	ext/libntfs_ext/source/volume.c \
	ext/libntfs_ext/source/xattrs.c \

${BUILDDIR}/ext/libntfs_ext/source/%.o : CFLAGS = -Wall ${OPTFLAGS} \
	-DHAVE_CONFIG_H -Iext/libntfs_ext/source -Iext/libntfs_ext/include \
	-DPS3_GEKKO

##############################################################
# TLSF (Two Level Segregated Fit) memory allocator
##############################################################

SRCS-${CONFIG_TLSF} += ext/tlsf/tlsf.c

##############################################################
# Duktape
##############################################################

SRCS += ext/duktape/duktape.c \
	src/ecmascript/ecmascript.c \
	src/ecmascript/es_service.c \
	src/ecmascript/es_stats.c \
	src/ecmascript/es_route.c \
	src/ecmascript/es_searcher.c \
	src/ecmascript/es_prop.c \
	src/ecmascript/es_io.c \
	src/ecmascript/es_fs.c \
	src/ecmascript/es_misc.c \
	src/ecmascript/es_crypto.c \
	src/ecmascript/es_kvstore.c \
	src/ecmascript/es_console.c \
	src/ecmascript/es_string.c \
	src/ecmascript/es_htsmsg.c \
	src/ecmascript/es_native_obj.c \
	src/ecmascript/es_root.c \
	src/ecmascript/es_hook.c \
	src/ecmascript/es_timer.c \
	src/ecmascript/es_subtitles.c \

SRCS-$(CONFIG_METADATA) += src/ecmascript/es_metadata.c

SRCS-$(CONFIG_SQLITE) += src/ecmascript/es_sqlite.c

${BUILDDIR}/ext/duktape/%.o : CFLAGS = -Wall ${OPTFLAGS} \
 -fstrict-aliasing -std=c99 -DDUK_OPT_FASTINT #-DDUK_OPT_ASSERTIONS #-DDUK_OPT_DEBUG -DDUK_OPT_DPRINT -DDUK_OPT_DDPRINT -DDUK_OPT_DDDPRINT


##############################################################
# Gumbo
##############################################################

SRCS-$(CONFIG_GUMBO) += \
	ext/gumbo-parser/src/attribute.c \
	ext/gumbo-parser/src/char_ref.c \
	ext/gumbo-parser/src/error.c \
	ext/gumbo-parser/src/parser.c \
	ext/gumbo-parser/src/string_buffer.c \
	ext/gumbo-parser/src/string_piece.c \
	ext/gumbo-parser/src/tag.c \
	ext/gumbo-parser/src/tokenizer.c \
	ext/gumbo-parser/src/utf8.c \
	ext/gumbo-parser/src/util.c \
	ext/gumbo-parser/src/vector.c \
	src/ecmascript/es_gumbo.c \

${BUILDDIR}/ext/gumbo-parser/%.o : CFLAGS = -Wall ${OPTFLAGS} -fstrict-aliasing -std=c99 -Wno-unused-variable

##############################################################
# Dataroot
##############################################################

${BUILDDIR}/support/dataroot/%.o : CFLAGS = -O2

##############################################################
##############################################################
##############################################################

include src/arch/${OS}/${OS}.mk


# Various transformations
SRCS  += $(SRCS-yes)
DLIBS += $(DLIBS-yes)
SLIBS += $(SLIBS-yes)
SSRCS  = $(sort $(SRCS))
OBJS4=   $(SSRCS:%.cpp=$(BUILDDIR)/%.o)
OBJS3=   $(OBJS4:%.S=$(BUILDDIR)/%.o)
OBJS2=   $(OBJS3:%.c=$(BUILDDIR)/%.o)
OBJS=    $(OBJS2:%.m=$(BUILDDIR)/%.o)
DEPS=    ${OBJS:%.o=%.d}

# File bundles
BUNDLES += $(sort $(BUNDLES-yes))
BUNDLE_SRCS=$(BUNDLES:%=$(BUILDDIR)/bundles/%.c)
BUNDLE_DEPS=$(BUNDLE_SRCS:%.c=%.d)
BUNDLE_OBJS=$(BUNDLE_SRCS:%.c=%.o)
.PRECIOUS: ${BUNDLE_SRCS}

# Common CFLAGS for all files
CFLAGS_com += -g -funsigned-char ${OPTFLAGS} ${CFLAGS_dbg}
CFLAGS_com += -D_FILE_OFFSET_BITS=64
CFLAGS_com += -iquote${BUILDDIR} -iquote${C}/src -iquote${C}

# Tools

MKBUNDLE = $(C)/support/mkbundle

ifndef V
ECHO   = printf "$(1)\t%s\n" $(2)
BRIEF  = CC LINKER MKBUNDLE CXX STRIP
MSG    = $@
$(foreach VAR,$(BRIEF), \
    $(eval $(VAR) = @$$(call ECHO,$(VAR),$$(MSG)); $($(VAR))))
endif

.PHONY:	clean distclean makever build-%

${PROG}: $(OBJS) $(ALLDEPS)  ${BUILDDIR}/support/dataroot/wd.o
	$(LINKER) -o $@ $(OBJS) ${BUILDDIR}/support/dataroot/wd.o $(LDFLAGS) ${LDFLAGS_cfg}

${PROG}.bundle: $(OBJS) $(BUNDLE_OBJS) $(ALLDEPS) ${BUILDDIR}/support/dataroot/bundle.o
	$(LINKER) -o $@ $(OBJS) ${BUILDDIR}/support/dataroot/bundle.o $(BUNDLE_OBJS) $(LDFLAGS) ${LDFLAGS_cfg}

${PROG}.datadir: $(OBJS) $(ALLDEPS) ${BUILDDIR}/support/dataroot/datadir.o
	$(LINKER) -o $@ $(OBJS) ${BUILDDIR}/support/dataroot/datadir.o $(LDFLAGS) ${LDFLAGS_cfg}

${LIB}.so: $(OBJS) $(BUNDLE_OBJS) $(ALLDEPS)  support/dataroot/bundle.c
	$(LINKER) -shared -o $@ $(OBJS) support/dataroot/bundle.c $(BUNDLE_OBJS) ${LDFLAGS_cfg}

.PHONY: ${BUILDDIR}/zipbundles/bundle.zip

${BUILDDIR}/zipbundles/bundle.zip:
	rm -rf  ${BUILDDIR}/zipbundles
	mkdir -p ${BUILDDIR}/zipbundles
	zip -0r ${BUILDDIR}/zipbundles/bundle.zip ${BUNDLES}

$(BUILDDIR)/support/dataroot/ziptail.o: src/main.h

${PROG}.ziptail: $(OBJS) $(ALLDEPS) $(BUILDDIR)/support/dataroot/ziptail.o
	$(CC) -o $@ $(OBJS) $(BUILDDIR)/support/dataroot/ziptail.o $(LDFLAGS) ${LDFLAGS_cfg}


${BUILDDIR}/%.o: %.c $(ALLDEPS)
	@mkdir -p $(dir $@)
	$(CC) -MD -MP $(CFLAGS_com) $(CFLAGS) $(CFLAGS_cfg) -c -o $@ $(C)/$<

${BUILDDIR}/%.o: %.S $(ALLDEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_com) $(CFLAGS) $(CFLAGS_cfg) -c -o $@ $(C)/$<

${BUILDDIR}/%.o: %.m $(ALLDEPS)
	@mkdir -p $(dir $@)
	$(CC) -MD -MP $(CFLAGS_com) $(CFLAGS) $(CFLAGS_cfg) -c -o $@ $(C)/$<

${BUILDDIR}/%.o: %.cpp $(ALLDEPS)
	@mkdir -p $(dir $@)
	$(CXX) -MD -MP $(CFLAGS_com) $(CFLAGS_cfg) -c -o $@ $(C)/$<

clean:
	rm -rf ${BUILDDIR}/src ${BUILDDIR}/ext ${BUILDDIR}/bundles
	find . -name "*~" | xargs rm -f

distclean:
	rm -rf build.*
	find . -name "*~" | xargs rm -f

reconfigure:
	$(C)/configure.${CONFIGURE_POSTFIX} $(CONFIGURE_ARGS)

showconfig:
	@echo $(CONFIGURE_ARGS)

# Create buildversion.h
src/version.c: $(BUILDDIR)/buildversion.h
$(BUILDDIR)/buildversion.h: FORCE
	@$(C)/support/version.sh $(C) $@
FORCE:

# Include dependency files if they exist.
-include $(DEPS) $(BUNDLE_DEPS)


# Bundle files
$(BUILDDIR)/bundles/%.o: $(BUILDDIR)/bundles/%.c $(ALLDEPS)
	$(CC) $(CFLAGS_cfg) -I${C}/src/fileaccess -c -o $@ $<

$(BUILDDIR)/bundles/%.c: % $(C)/support/mkbundle $(ALLDEPS)
	@mkdir -p $(dir $@)
	$(MKBUNDLE) -o $@ -s $< -d ${BUILDDIR}/bundles/$<.d -p $<


export C
export BUILDDIR
export OPTFLAGS

# External builds
$(BUILDDIR)/stamps/%.stamp: build-%
	@mkdir -p $(dir $@)
	touch $@

build-%:
	${MAKE} -f ${C}/ext/$*.mk build
