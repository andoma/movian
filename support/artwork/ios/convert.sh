SRC=origin.png

rm -rf out
mkdir -p out
convert origin.png -resize 29x29 out/Icon-Small.png
convert origin.png -resize 58x58 out/Icon-Small@2x.png
convert origin.png -resize 87x87 out/Icon-Small@3x.png
convert origin.png -resize 40x40 out/Icon-40.png
convert origin.png -resize 80x80 out/Icon-40@2x.png
convert origin.png -resize 120x120 out/Icon-40@3x.png
convert origin.png -resize 120x120 out/Icon-60@2x.png
convert origin.png -resize 160x160 out/Icon-60@3x.png
convert origin.png -resize 76x76 out/Icon-76.png
convert origin.png -resize 152x152 out/Icon-76@2x.png
convert origin.png -resize 167x167 out/Icon-83_5@2x.png


