# 
# Uses the cg compiler from nVidia
# Get it here:
#   http://developer.nvidia.com/object/cg_download.html
#

.SUFFIXES: .cg


TARGETS= yuv2rgb.h yuv2rgb_2mix.h yuv2rgb_rect.h yuv2rgb_2mix_rect.h

all:	$(TARGETS) Makefile

clean:	
	rm -f $(TARGETS)

.cg.h:
	cgc -quiet -profile arbfp1 $< | awk '{print "\t\"" $$0 "\\n\""}' | cat >$@

