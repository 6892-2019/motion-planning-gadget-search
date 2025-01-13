#!/usr/bin/env python2
# SPDX-License-Identifier: MIT
# Copyright 2019 Massachusetts Institute of Technology

import itertools

MIN_ALPHABET_SIZE = 2
MAX_ALPHABET_SIZE = 16

def mirror_amount(locs):
  return (0,) if locs <= 2 else (0, 1)

def perm(n, locs, mirrored, rot):
  p = list(xrange(0, n))
  if mirrored:
    p[0:locs] = reversed(p[0:locs])
  p[0:locs] = p[rot:locs] + p[0:rot]
  return 'constexpr unsigned int a{}l{}m{}r{}[] = {{{}}};'.format(n, locs, int(mirrored), rot, ', '.join(map(str, p)));

def perms(n, locs, allow_mirror):
  p = list(xrange(0, n))
  for i in xrange(0, locs):
    print '{', ', '.join(map(str, p)), '}, ',
    # rotate
    p[0:locs] = p[1:locs] + p[0:1]
  if allow_mirror:
    print
    p = list(xrange(0, n))
    p[0:locs] = reversed(p[0:locs])
    for i in xrange(0, locs):
      print '{', ', '.join(map(str, p)), '}, ',
      p[0:locs] = p[1:locs] + p[0:1]

for alphabet_size in xrange(MIN_ALPHABET_SIZE, MAX_ALPHABET_SIZE+1):
  for locations in xrange(2, alphabet_size+1):
    for mirrored in mirror_amount(locations):
      for rotation in xrange(0, locations):
        print perm(alphabet_size, locations, mirrored, rotation)

for alphabet_size in xrange(MIN_ALPHABET_SIZE, MAX_ALPHABET_SIZE+1):
  for locations in xrange(2, alphabet_size+1):
    print 'constexpr const unsigned int* a{}l{}[] = {{'.format(alphabet_size, locations)
    for mirrored in mirror_amount(locations):
      print ', '.join(map(lambda r: 'a{}l{}m{}r{}'.format(alphabet_size, locations, mirrored, r), xrange(0, locations))) + ','
    print '};'

print 'constexpr std::pair<const unsigned int**, const unsigned int**> nullptrpair = {nullptr, nullptr};'

print 'constexpr std::pair<const unsigned int* const*, const unsigned int* const*> m0perms[{0}][{0}] = {{'.format(MAX_ALPHABET_SIZE+1)
for alphabet_size in xrange(0, MAX_ALPHABET_SIZE+1):
  elems = []
  for locations in xrange(0, MAX_ALPHABET_SIZE+1):
    if locations < MIN_ALPHABET_SIZE or locations > alphabet_size:
      elems.append('nullptrpair')
    else:
      source = 'a{}l{}'.format(alphabet_size, locations)
      elems.append('{{{0}, {0}+{1}}}'.format(source, locations))
  print '{' + ', '.join(elems) + '},'
print '};'

print 'constexpr std::pair<const unsigned int* const*, const unsigned int* const*> m1perms[{0}][{0}] = {{'.format(MAX_ALPHABET_SIZE+1)
for alphabet_size in xrange(0, MAX_ALPHABET_SIZE+1):
  elems = []
  for locations in xrange(0, MAX_ALPHABET_SIZE+1):
    if locations < MIN_ALPHABET_SIZE or locations > alphabet_size or len(mirror_amount(locations)) == 1:
      elems.append('nullptrpair')
    else:
      source = 'a{}l{}'.format(alphabet_size, locations)
      elems.append('{{{0}+{1}, {0}+{2}}}'.format(source, locations, 2*locations))
  print '{' + ', '.join(elems) + '},'
print '};'

print 'constexpr std::pair<const unsigned int* const*, const unsigned int* const*> allperms[{0}][{0}] = {{'.format(MAX_ALPHABET_SIZE+1)
for alphabet_size in xrange(0, MAX_ALPHABET_SIZE+1):
  elems = []
  for locations in xrange(0, MAX_ALPHABET_SIZE+1):
    if locations < MIN_ALPHABET_SIZE or locations > alphabet_size:
      elems.append('nullptrpair')
    else:
      source = 'a{}l{}'.format(alphabet_size, locations)
      elems.append('{{{0}, {0}+{1}}}'.format(source, len(mirror_amount(locations)) * locations))
  print '{' + ', '.join(elems) + '},'
print '};'