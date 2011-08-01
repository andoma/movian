#!/usr/bin/env python

import sys
import os
import re
import json

M = re.compile('_p{0,1}\("([^"]*)"\)')


def scanfile(path, p):
    f = open(path)
    s = f.read()
    l = re.findall(M, s)
    if len(l) > 0:
        for w in l:
            if w in p:
                if path not in p:
                    p[w].append(path)
            else:
                p[w] = [path]
    f.close()



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

    for x in sorted(phraselist, key=lambda x: '%s %s' % (x[1][0], x[0])):
        key = x[0]
        source = x[1][0]

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
