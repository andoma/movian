
all: 	v1.vp \
	f_tex.fp \
	f_flat.fp \
	f_tex_blur.fp \
	yuv2rgb_1f_norm.fp \
	yuv2rgb_2f_norm.fp \
	yuv2rgb_v.vp \
	f_flat_stencil.fp \
	f_tex_stencil.fp \
	f_tex_stencil.fp \
	f_tex_stencil_blur.fp \

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
