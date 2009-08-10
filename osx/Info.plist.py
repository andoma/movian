#!/usr/bin/env python

print "\t<key>CFBundleDocumentTypes</key>"
print "\t<array>"
for name, exts, mimes in [ \
  ("AVI container", ["avi"], []), 
  ("MPEG container", ["mpg", "mpeg"], []),
  ("ISO disc image", ["iso", "bin"], []),
  ("OGG audio file", ["ogg"], ["audio/ogg"])
  ]:
  print "\t\t<dict>"
  print "\t\t\t<key>CFBundleTypeName</key>"
  print "\t\t\t<string>"  + name + "</string>"
  if len(exts) > 0:
    print "\t\t\t<key>CFBundleTypeExtensions</key>"
    print "\t\t\t<array>"
    for ext in exts:
      print "\t\t\t\t<string>" + ext + "</string>"
    print "\t\t\t</array>"
  if len(mimes) > 0:
    print "\t\t\t<key>CFBundleTypeMIMETypes</key>"
    print "\t\t\t<array>"
    for mime in mimes:
      print "\t\t\t\t<string>" + mime + "</string>"
    print "\t\t\t</array>"
  print "\t\t\t<key>CFBundleTypeIconFile</key>"
  print "\t\t\t<string>hts</string>"
  print "\t\t\t<key>CFBundleTypeRole</key>"
  print "\t\t\t<string>Viewer</string>"
  print "\t\t</dict>"
print "\t</array>"

print "\t<key>CFBundleURLTypes</key>"
print "\t<array>"
for scheme in ["htsp"]:
  print "\t\t<dict>"
  print "\t\t\t<key>CFBundleURLName</key>"
  print "\t\t\t<string>" + scheme + " url</string>"
  print "\t\t\t<key>CFBundleURLSchemes</key>"
  print "\t\t\t<array>"
  print "\t\t\t\t<string>" + scheme + "</string>"
  print "\t\t\t</array>"
  print "\t\t\t<key>CFBundleURLIconFile</key>"
  print "\t\t\t<string>generic</string>"
  print "\t\t</dict>"
print "\t</array>"

