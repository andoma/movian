run:
	${STRIP} -o obj/showtime.stripped obj/showtime
	wiiload obj/showtime.stripped #webdav://www.olebyn.nu/media
