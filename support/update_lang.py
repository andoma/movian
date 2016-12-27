#!/usr/bin/env python

import sys
import os
import re
import json

M = re.compile('_p?\("([^"]*)"\)')
MPL = re.compile('_pl\("([^"]*)"[^,]*,[^"]*"([^"]*)"[^,]*,[^"]*[^"]*\)')

def scanfile(path, p):
    d = open(path).read()
    for w in re.findall(M, d):
        p.setdefault(w, []).append(path)

    for w in re.findall(MPL, d):
        p.setdefault(w, []).append(path)

def buildphrases():
    p = {}

    for r in ('./src', './glwskins/flat'):
        for a in os.walk(r):
            for f in a[2]:
                if f.endswith(('.c', '.view')):
                    scanfile(os.path.join(a[0], f), p)

    # invert dict to use first source as key and value is list of phrases
    phrases = {}
    for phrase, sources in p.iteritems():
      phrases.setdefault(sorted(sources)[0], []).append(phrase)
    # sort phrases for each source
    for source in phrases:
      phrases[source].sort()
    
    return phrases

if len(sys.argv) < 2:
    print "Usage: %s <langfile> [<langfile...>]" % sys.argv[0]
    sys.exit(1)

phrases = buildphrases()

for path in sys.argv[1:]:

    linetrailer = '\n'

    def emit(s):
        outfile.write(s + linetrailer)

    if path.endswith('~'):
        continue

    maintainer = 'Unknown'
    language = 'Unknown'
    native = 'Unknown'
    writebom = False
    in_phrases = {}
    in_plural_phrases = {}

    if os.path.isfile(path):
        f = open(path)

        bom = f.read(3)
        if bom == '\xef\xbb\xbf':
            writebom = True
        else:
            f.seek(0);

        R = re.compile('([a-z]*): *(.*)')
        RPL = re.compile('([a-z]*)\[([0-9]*)\]: *(.*)')
        
        entry = None
        
        for l in f:

            if l.endswith('\r\n'):
                linetrailer = '\r\n'

            l = l.rstrip('\n\r')
            o = re.match(R, l)
            if o:
                if o.group(1) == 'maintainer':
                    maintainer = o.group(2)
                elif o.group(1) == 'language':
                    language = o.group(2)
                elif o.group(1) == 'native':
                    native = o.group(2)
                elif o.group(1) == 'id':
                    mid = o.group(2)
                elif o.group(1) == 'msg' and len(o.group(2)) > 0:
                    in_phrases[mid] = o.group(2)
                continue

            o = re.match(RPL, l)
            if o:
                in_plural_phrases.setdefault(mid, {})[int(o.group(2))] = o.group(3)
        f.close()


    outlist = []
    last = None

    outfile = open(path, 'w')
    if writebom:
        outfile.write('\xef\xbb\xbf')

    emit('language: %s' % language)
    emit('native: %s' % native)
    emit('maintainer: %s' % maintainer)

    print "Processing %s (%s / %s) maintained by %s" % \
        (path, language, native, maintainer)  

    missed = 0

    for source in sorted(phrases):

        emit('#')
        emit('# %s' % source)
        emit('#')

        for phrase in phrases[source]:

            if isinstance(phrase, tuple):
                emit('id: %s' % phrase[0])
                if phrase[0] in in_plural_phrases:
                    d = in_plural_phrases[phrase[0]]
                    idx = d.keys()
                    idx.sort()
                    for i in idx:
                        emit('msg[%d]: %s' % (i, d[i]))
                else:
                    emit("# Missing plural translation for '%s' and '%s'" % (
                            phrase[0], phrase[1]))
                    emit('msg[0]: ')
                    emit('msg[1]: ')
                    missed += 1
            else:
                emit('id: %s' % phrase)
                if phrase in in_phrases:
                    emit('msg: %s' % in_phrases[phrase])
                else:
                    emit("# Missing translation")
                    emit('msg: ')
                    missed += 1

            emit('')

    print "  %d translations missing" % (missed, )
    outfile.close()
