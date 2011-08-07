#!/usr/bin/env python

import sys
import os
import re
import json

M = re.compile('_p?\("([^"]*)"\)')

def scanfile(path, p):
    for w in re.findall(M, open(path).read()):
        p.setdefault(w, []).append(path)

def buildphrases(rootpath):
    p = {}
    for a in os.walk(rootpath):
        for f in a[2]:
            if f.endswith(('.c', '.view', '.skin')):
                scanfile(os.path.join(a[0], f), p)

    # invert dict to use first source as key and value is list of phrases
    phrases = {}
    for phrase, sources in p.iteritems():
      phrases.setdefault(sorted(sources)[0], []).append(phrase)
    # sort phrases for each source
    for source in phrases:
      phrases[source].sort()
    
    return phrases

if len(sys.argv) < 3:
    print "Usage: %s rootpath <langfile> [<langfile...>]" % sys.argv[0]
    sys.exit(1)

phrases = buildphrases(sys.argv[1])

for path in sys.argv[2:]:

    if path.endswith('~'):
        continue

    maintainer = 'Unknown'
    language = 'Unknown'
    native = 'Unknown'

    in_phrases = {}
    if os.path.isfile(path):
        f = open(path)

        R = re.compile('([^:]*): *(.*)')
        
        entry = None
        
        for l in f:
            l = l.rstrip('\n')
            o = re.match(R, l)
            if not o:
                continue
            
            if o.group(1) == 'maintainer':
                maintainer = o.group(2)
            if o.group(1) == 'language':
                language = o.group(2)
            if o.group(1) == 'native':
                native = o.group(2)

            if o.group(1) == 'id':
                mid = o.group(2)

            if o.group(1) == 'msg' and len(o.group(2)) > 0:
                in_phrases[mid] = o.group(2)

        f.close()

    outlist = []
    last = None

    outfile = open(path, 'w')

    print >>outfile, 'language: %s' % language
    print >>outfile, 'native: %s' % native
    print >>outfile, 'maintainer: %s' % maintainer

    print "Processing %s (%s / %s) maintained by %s" % \
        (path, language, native, maintainer)  

    for source in sorted(phrases):
        print >>outfile, '#'
        print >>outfile, '# %s' % source
        print >>outfile, '#'

        for phrase in phrases[source]:
            print >>outfile, 'id: %s' % phrase
            if phrase in in_phrases:
                print >>outfile, 'msg: %s' % in_phrases[phrase]
            else:
                print >>outfile, "# Missing translation"
                print >>outfile, 'msg: '
                print " ! Missing translation for %s" % phrase

            print >>outfile

    outfile.close()
