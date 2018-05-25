#!/usr/bin/env python2

from collections import OrderedDict, defaultdict, namedtuple
import os

# the vendor/ subdirectories we want
# TODO: currently we make all headers available; this only controls objects
import_vendored = ['vta', 'farmhash', 'fmt']

# flags that don't depend on the configuration
global_flags = OrderedDict()
global_flags['builddir'] = 'build' # significant to Ninja
global_flags['modeflags'] = '-std=c++1z'
global_flags['warnflags'] = '-pedantic -Wall -Wextra -Wuninitialized -Winit-self -Wconversion -Wuseless-cast -Wlogical-op -Waggressive-loop-optimizations -Winvalid-pch -Wno-unused-parameter -Wduplicated-cond -Wnull-dereference -Wno-dangling-else -Wsuggest-override -fdiagnostics-color=always'
global_flags['ldflags'] = '-u malloc -ljemalloc_pic -lc -lpthread -ldl'

# configuration-specific flag *templates*
config_flags = OrderedDict()
config_flags['{config}_builddir'] = '$builddir/{config}'
config_flags['{config}_modeflags'] = '$modeflags'
config_flags['{config}_warnflags'] = '$warnflags'
config_flags['{config}_optflags'] = '{optflags}'
config_flags['{config}_ldflags'] = '$ldflags' # could change in the future
config_flags['{config}_includeflags'] = '-I${config}_builddir/include -Isrc/ -isystem vendor/'
config_flags['{config}_pchtarget'] = '${config}_builddir/include/precompiled.hpp.gch'

debug_cfg = {
  'config': 'debug',
  'optflags': '-g -O0 -march=native -gsplit-dwarf -fdebug-types-section -grecord-gcc-switches',
}
sanitize_cfg = {
  'config': 'sanitize',
  'optflags': debug_cfg['optflags'] + ' -fsanitize=address -fsanitize=undefined',
}
fastdebug_cfg = {
  'config': 'fastdebug',
  'optflags': '-g -O1 -march=native -gsplit-dwarf -fdebug-types-section -grecord-gcc-switches',
}
release_cfg = {
  'config': 'release',
  'optflags': '-O2 -march=native -flto -fvisibility=hidden -DNDEBUG',
}
configs = [debug_cfg, sanitize_cfg, fastdebug_cfg, release_cfg]





# stuff below here shouldn't need editing/customization

rules = OrderedDict()
rules['{config}_cxx'] = [
  'command = g++ -MMD -MT $out -MF $out.d ${config}_modeflags ${config}_optflags ${config}_warnflags ${config}_includeflags -c $in -o $out',
  'depfile = $out.d',
  'deps = gcc',
]
rules['{config}_ld'] = [
  'command = g++ ${config}_modeflags ${config}_optflags -o $out $in ${config}_ldflags',
]



Thing = namedtuple('Thing', ['source', 'object', 'is_entrypoint'])
thing_groups = defaultdict(list)

for subdir, dirs, files in os.walk('src/'):
  group_name = os.path.relpath(subdir, 'src/')
  for f in files:
    if f.endswith('.cpp'):
      source_path = os.path.join(subdir, f)
      object_path = "${config}_builddir/" + source_path[:-4] + ".o"
      is_entrypoint = 'genbuild entrypoint' in open(source_path).read()
      thing_groups[group_name].append(Thing(source_path, object_path, is_entrypoint))

for dependency in import_vendored:
  for subdir, dirs, files in os.walk('vendor/'+dependency+'/'):
    group_name = os.path.relpath('src/', 'src/')
    for f in files:
      if f.endswith(('.cpp', '.cc')):
        source_path = os.path.join(subdir, f)
        object_path = "${config}_builddir/" + source_path[:-4] + ".o"
        #is_entrypoint = 'genbuild entrypoint' in open(source_path).read()
        thing_groups[group_name].append(Thing(source_path, object_path, False))

# map each thing_group key to non-entrypoint objects (plus parents)
object_groups = OrderedDict()
for k, v in thing_groups.iteritems():
  object_groups[k] = [t.object for t in v if not t.is_entrypoint]
for k, v in object_groups.iteritems():
  if k != '.': # TODO: we're assuming the top-level dir is only parent group
    v.extend(object_groups['.'])

link_groups = OrderedDict()
for k, v in thing_groups.iteritems():
  for t in v:
    if t.is_entrypoint:
      objects = [t.object]
      objects.extend(object_groups[k])
      executable_name = os.path.basename(t.object)[:-2] + ".exe"
      link_groups['${config}_builddir/bin/' + executable_name] = objects



with open('build.ninja', 'wb') as buildfile:
  for k, v in global_flags.iteritems():
    buildfile.write('{} = {}\n'.format(k, v))
  
  for config in configs:
    buildfile.write('\n\n\n')
    for k, v in config_flags.iteritems():
      buildfile.write('{} = {}\n'.format(k.format(**config), v.format(**config)))
    buildfile.write('\n')
    
    for k, v in rules.iteritems():
      buildfile.write('rule {}\n'.format(k.format(**config)))
      for q in v:
        buildfile.write('  {}\n'.format(q.format(**config)))
    buildfile.write('\n')
    
    buildfile.write('build ${config}_pchtarget: {config}_cxx src/precompiled.hpp\n'.format(**config))
    for ts in thing_groups.itervalues():
      for t in ts:
        buildfile.write(('build {}: {} {}'.format(t.object, '{config}_cxx', t.source) + ' | ${config}_pchtarget\n').format(**config))
    buildfile.write('\n')
    
    #TODO: we could avoid repetition by introducing variables for any object
    #group (i.e., excluding the entrypoint object) used more than once
    for exe, objs in link_groups.iteritems():
      buildfile.write('build {}: {} {}\n'.format(exe, '{config}_ld', ' '.join(objs)).format(**config))

