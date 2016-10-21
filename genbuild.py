#!/usr/bin/env python2

from collections import OrderedDict
import os

globals = OrderedDict()
globals.update({
  'modeflags': '-std=c++14',
  'warnflags': '-pedantic -Wall -Wextra -Wuninitialized -Winit-self -Wconversion -Wuseless-cast -Wlogical-op -Waggressive-loop-optimizations -Winvalid-pch -Wno-unused-parameter -Wduplicated-cond -Wnull-dereference',
  'builddir': 'build/$config/',
  'src_dir': 'src/',
  'test_dir': 'test/',
  'ldflags': '-lc',
})
# must be inserted after its references
globals['includeflags'] = '-I$builddir/include -I$src_dir/'
globals['testldflags'] = '$ldflags -lgtest -lgtest_main'

debug_cfg = {'config': 'debug', 'optflags': '-g -O0 -march=native'}
fastdebug_cfg = {'config': 'fastdebug', 'optflags': '-g -O2 -march=native'}
release_cfg = {'config': 'release', 'optflags': '-O2 -march=native -flto -fvisibility=hidden -DNDEBUG'}
configs = [debug_cfg, fastdebug_cfg, release_cfg]

for config in configs:
  with open('{config}.ninja'.format(**config), 'wb') as buildfile:
    buildfile.write('rule cxx\n')
    buildfile.write('  command = g++ -MMD -MT $out -MF $out.d $modeflags $optflags $warnflags $includeflags -c $in -o $out\n')
    buildfile.write('  depfile = $out.d\n')
    buildfile.write('  deps = gcc\n')
    buildfile.write('\n')
    buildfile.write('rule ld\n')
    buildfile.write('  command = g++ $modeflags $optflags -o $out $in $ldflags\n')
    buildfile.write('\n')

    for k, v in config.iteritems():
      buildfile.write('{} = {}\n'.format(k, v))
    for k, v in globals.iteritems():
      buildfile.write('{} = {}\n'.format(k, v))
    buildfile.write('\n')

    buildfile.write('pch_target = $builddir/includes/precompiled.hpp.gch\n')
    buildfile.write('build $pch_target : cxx $src_dir/precompiled.hpp\n')
    buildfile.write('\n')

    src_objects = []
    for subdir, dirs, files in os.walk('src/'):
      for f in files:
        if f.endswith('.cpp'):
          source = os.path.join(subdir, f)
          object = "$builddir/$src_dir/" + f[:-4] + '.o'
          buildfile.write('build {} : cxx {} | $pch_target\n'.format(object, source))
          src_objects.append(object)
    buildfile.write('\n')

    buildfile.write('build $builddir/bin/automaton.exe : ld {}\n'.format(' '.join(src_objects)))
    buildfile.write('\n')

    test_objects = []
    for subdir, dirs, files in os.walk('test/'):
      for f in files:
        if f.endswith('.cpp'):
          source = os.path.join(subdir, f)
          object = "$builddir/$test_dir/" + f[:-4] + '.o'
          buildfile.write('build {} : cxx {} | $pch_target\n'.format(object, source))
          test_objects.append(object)
    buildfile.write('\n')

    buildfile.write('build $builddir/bin/test-automaton.exe : ld {}\n'.format(' '.join(test_objects)))
    buildfile.write('  ldflags = $testldflags\n')
    buildfile.write('\n')
