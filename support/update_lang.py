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

    return p




if len(sys.argv) < 3:
    print "Usage: %s rootpath <langfile> [<langfile...>]" % sys.argv[0]
    sys.exit(1)

phraselist = [(k, v) for k,v in buildphrases(sys.argv[1]).iteritems()]


for path in sys.argv[2:]:

    if path.endswith('~'):
        continue

    maintainer = 'Unknown'
    language = 'Unknown'
    native = 'Unknown'

    in_strings = {}
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
                in_strings[mid] = o.group(2)

        f.close()

    outlist = []
    last = None

    outfile = open(path, 'w')

    print >>outfile, 'language: %s' % language
    print >>outfile, 'native: %s' % native
    print >>outfile, 'maintainer: %s' % maintainer

    print "Processing %s (%s / %s) maintained by %s" % \
        (path, language, native, maintainer)

    for key, sources in sorted(phraselist, key=lambda (w, sources): '%s %s' % (sources[0], w)):
        source = sources[0]

        if source != last: 
            last = source
            print >>outfile, '#'
            print >>outfile, '# %s' % last
            print >>outfile, '#'

        print >>outfile, 'id: %s' % key

        if key in in_strings:
            print >>outfile, 'msg: %s' % in_strings[key]
        else:
            print >>outfile, 'msg: '
            print " ! Missing translation for %s" % key

        print >>outfile

    outfile.close()
