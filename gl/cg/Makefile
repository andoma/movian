.SUFFIXES: .arbfp1
.SUFFIXES: .fp40


TARGETS= yuv2rgb.h yuv2rgb_2mix.h yadif.h

all:	$(TARGETS) Makefile

clean:	
	rm -f $(TARGETS)

.arbfp1.h:
	cgc -quiet -profile arbfp1 $< | awk '{print "\t\"" $$0 "\\n\""}' | cat >$@

.fp40.h:
	cgc -quiet -profile fp40 $< | awk '{print "\t\"" $$0 "\\n\""}' | cat >$@


