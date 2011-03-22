
all: 	f_tex.fp \
	f_flat.fp \
	f_tex_blur.fp \
	v1.vp \
	yuv2rgb_1f_norm.fp \
	yuv2rgb_2f_norm.fp \
	yuv2rgb_v.vp


%.fp40: ../glsl/%.glsl
	cgc -oglsl -profile fp40 $< >$@

%.fp: %.fp40
	cgcomp -a -f $< $@


%.vp40: ../glsl/%.glsl
	cgc -oglsl -profile vp40 $< >$@

%.vp: %.vp40
	cgcomp -a -v $< $@


clean:
	rm -f *.fp *.vp
