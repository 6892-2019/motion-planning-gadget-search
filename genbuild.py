#!/usr/bin/env python2

from collections import OrderedDict, defaultdict
import os

globals = OrderedDict()
globals.update({
  'modeflags': '-std=c++1z',
  'warnflags': '-pedantic -Wall -Wextra -Wuninitialized -Winit-self -Wconversion -Wuseless-cast -Wlogical-op -Waggressive-loop-optimizations -Winvalid-pch -Wno-unused-parameter -Wduplicated-cond -Wnull-dereference -Wno-dangling-else -fdiagnostics-color=always',
  'builddir': 'build/$config/',
  'src_dir': 'src/',
  'test_dir': 'test/',
  'ldflags': '../nauty/nauty.a -lc -lpthread',
})
# must be inserted after its references
globals['includeflags'] = '-I$builddir/include -I$src_dir/ -isystem ../nauty/ -isystem ../vta/include/'
globals['testldflags'] = '$ldflags -lgtest -lgtest_main'

debug_cfg = {'config': 'debug', 'optflags': '-g -O0 -march=native -gsplit-dwarf -fdebug-types-section -grecord-gcc-switches'}
sanitize_cfg = {'config': 'sanitize', 'optflags': debug_cfg['optflags'] + ' -fsanitize=address -fsanitize=undefined'}
fastdebug_cfg = {'config': 'fastdebug', 'optflags': '-g -O2 -march=native -gsplit-dwarf -fdebug-types-section -grecord-gcc-switches'}
release_cfg = {'config': 'release', 'optflags': '-O2 -march=native -flto -fvisibility=hidden -DNDEBUG'}
configs = [debug_cfg, sanitize_cfg, fastdebug_cfg, release_cfg]

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

    buildfile.write('pch_target = $builddir/include/precompiled.hpp.gch\n')
    buildfile.write('build $pch_target : cxx $src_dir/precompiled.hpp\n')
    buildfile.write('\n')

    src_objects = defaultdict(list)
    for subdir, dirs, files in os.walk('src/'):
      for f in files:
        if f.endswith('.cpp'):
          source = os.path.join(subdir, f)
          rel = os.path.relpath(subdir, 'src/')
          object = "$builddir/$src_dir/" + rel + '/' + f[:-4] + '.o'
          if 'precompiled-instantiations' in f:
            pch_inst_target = object
            buildfile.write('build {} : cxx {}\n'.format(object, source))
          else:
            buildfile.write('build {} : cxx {} | $pch_target\n'.format(object, source))
            src_objects[rel].append(object)
    buildfile.write('\n')

    src_objects['.'].append(pch_inst_target)
    src_objects_str = ' '.join(src_objects['.'])
    del src_objects['.']
    for k, v in src_objects.iteritems():
      buildfile.write('build $builddir/bin/{}.exe : ld {} {}\n'.format(k, ' '.join(v), src_objects_str))
      buildfile.write('\n')

    test_objects = []
    for subdir, dirs, files in os.walk('test/'):
      for f in files:
        if f.endswith('.cpp'):
          source = os.path.join(subdir, f)
          rel = os.path.relpath(source, 'test/')
          object = "$builddir/$test_dir/" + rel[:-4] + '.o'
          buildfile.write('build {} : cxx {} | $pch_target\n'.format(object, source))
          test_objects.append(object)
    buildfile.write('\n')

    buildfile.write('build $builddir/bin/test-automaton.exe : ld {} {}\n'.format(' '.join(test_objects), src_objects_str))
    buildfile.write('  ldflags = $testldflags\n')
    buildfile.write('\n')
