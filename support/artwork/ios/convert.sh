SRC=../movian_1024_1024.png

rm -rf out
mkdir -p out
convert ${SRC} -resize 29x29 out/Icon-Small.png
convert ${SRC} -resize 58x58 out/Icon-Small@2x.png
convert ${SRC} -resize 87x87 out/Icon-Small@3x.png
convert ${SRC} -resize 40x40 out/Icon-40.png
convert ${SRC} -resize 80x80 out/Icon-40@2x.png
convert ${SRC} -resize 120x120 out/Icon-40@3x.png
convert ${SRC} -resize 120x120 out/Icon-60@2x.png
convert ${SRC} -resize 160x160 out/Icon-60@3x.png
convert ${SRC} -resize 76x76 out/Icon-76.png
convert ${SRC} -resize 152x152 out/Icon-76@2x.png
convert ${SRC} -resize 167x167 out/Icon-83_5@2x.png
