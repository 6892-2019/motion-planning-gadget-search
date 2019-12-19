#!/usr/bin/env python3

from collections import OrderedDict, defaultdict, namedtuple
import os
import itertools
import ast
from typing import Optional
from types import SimpleNamespace
from operator import attrgetter

# the vendor/ subdirectories we want
# TODO: currently we make all headers available; this only controls objects
import_vendored = ['vta', 'farmhash', 'simdjson']

default_targets = ['debug', 'release']

# flags that don't depend on the configuration
global_flags = OrderedDict()
global_flags['builddir'] = 'build' # significant to Ninja
global_flags['modeflags'] = '-std=c++2a'
global_flags['warnflags'] = '-pedantic -Wall -Wextra -Wuninitialized -Winit-self -Wconversion -Wuseless-cast -Wlogical-op -Waggressive-loop-optimizations -Winvalid-pch -Wno-unused-parameter -Wduplicated-cond -Wnull-dereference -Wno-dangling-else -Wsuggest-override -fdiagnostics-color=always'
global_flags['ldflags'] = '-fuse-ld=gold -Wl,--gc-sections -Wl,--gdb-index -u malloc -ljemalloc_pic -lc -lpthread -ldl -lfmt'

# configuration-specific flag *templates*
config_flags = OrderedDict()
config_flags['{config}_builddir'] = '$builddir/{config}'
config_flags['{config}_modeflags'] = '$modeflags'
config_flags['{config}_warnflags'] = '$warnflags'
config_flags['{config}_optflags'] = '{optflags} -ffunction-sections -fdata-sections'
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
  'optflags': '-g -O2 -march=native -flto=$hardware_concurrency -fvisibility=hidden -DNDEBUG',
  'pools': {
    'ld': 'multithreaded',
  }
}
configs = [debug_cfg, sanitize_cfg, fastdebug_cfg, release_cfg]





# stuff below here shouldn't need editing/customization

rules = OrderedDict()
rules['cxx'] = [
  'command = g++ -MMD -MT $out -MF $out.d ${config}_modeflags ${config}_optflags ${config}_warnflags ${config}_includeflags -c $in -o $out',
  'depfile = $out.d',
  'deps = gcc',
]
rules['ld'] = [
  'command = g++ ${config}_modeflags ${config}_optflags -o $out $in ${config}_ldflags $addl_ldflags',
]

import multiprocessing
global_flags['hardware_concurrency'] = str(multiprocessing.cpu_count())
pools = {
  # For tasks that use all the hardware threads.  Unfortunately other tasks may
  # still run in parallel if they don't depend on any task in this pool.
  'multithreaded': '1',
}


object_things = []
object_groups = defaultdict(list)
executable_things = []

def extract_genbuild_data(path) -> Optional[SimpleNamespace]:
  for line in open(path).readlines():
    idx = line.find('genbuild ')
    if idx != -1:
      idx += len('genbuild ')
      data = line[idx:].strip()
      try:
        return ast.literal_eval(data)
      except ValueError as e:
        raise ValueError(path) from e
  return None

def discover_targets(src_root):
  group_stack = []
  for subdir, dirs, files in os.walk(src_root):
    # os.walk flattens the recusion, but we actually want to track it.
    while group_stack and not subdir.startswith(group_stack[-1]):
      group_stack.pop()
    group_stack.append(subdir)

    for f in files:
      # If we want non-C objects (i.e., not ending in .o), make this a map from
      # input suffix to output suffix.
      for suffix in ('.cpp', '.cc'):
        if f.endswith(suffix):
          object_thing = SimpleNamespace()
          object_thing.source = os.path.join(subdir, f)
          object_thing.object = "${config}_builddir/" + object_thing.source[:-len(suffix)] + ".o"
          data = extract_genbuild_data(object_thing.source)
          object_thing.data = data
          object_things.append(object_thing)
          if data and data.get('entrypoint', False):
            exe_name = os.path.basename(object_thing.object)[:-2] + ".exe"
            exe_thing = SimpleNamespace()
            exe_thing.target = '${config}_builddir/bin/' + exe_name
            exe_thing.object = object_thing.object
            exe_thing.link_groups = list(group_stack)
            if 'link_groups' in data:
              exe_thing.link_groups.extend(data['link_groups'])
            exe_thing.data = data
            executable_things.append(exe_thing)
          else:
            object_groups[subdir].append(object_thing.object)

discover_targets('src')
for dependency in import_vendored:
  discover_targets('vendor/'+dependency)

vendor_object_groups = [g for g in object_groups.keys() if g.startswith('vendor/')]
for et in executable_things:
  et.link_groups.extend(vendor_object_groups)
  et.objects = [et.object] + sorted(itertools.chain.from_iterable([object_groups[g] for g in et.link_groups]))

object_things.sort(key=attrgetter('source'))
executable_things.sort(key=attrgetter('target'))



with open('build.ninja', 'w') as buildfile:
  for k, v in global_flags.items():
    buildfile.write('{} = {}\n'.format(k, v))
  buildfile.write('\n')
  
  for name, depth in pools.items():
    buildfile.write('pool {}\n  depth = {}\n'.format(name, depth))
  buildfile.write('\n')
  
  for config in configs:
    buildfile.write('\n\n\n')
    for k, v in config_flags.items():
      buildfile.write('{} = {}\n'.format(k.format(**config), v.format(**config)))
    buildfile.write('\n')
    
    for k, v in rules.items():
      buildfile.write('rule {}_{}\n'.format(config['config'], k))
      for q in v:
        buildfile.write('  {}\n'.format(q.format(**config)))
      if 'pools' in config and k in config['pools']:
        buildfile.write('  pool = {}\n'.format(config['pools'][k]))
    buildfile.write('\n')
    
    buildfile.write('build ${config}_pchtarget: {config}_cxx src/precompiled.hpp\n'.format(**config))
    for t in object_things:
      buildfile.write(('build {}: {} {}'.format(t.object, '{config}_cxx', t.source) + ' | ${config}_pchtarget\n').format(**config))
    buildfile.write('\n')
    
    # Unfortunately, we can't optimize by introducing variables for repeated
    # input object lists, because ninja splits the list before expanding
    # variables.
    for t in executable_things:
      buildfile.write('build {}: {} {}\n'.format(t.target, '{config}_ld', ' '.join(t.objects)).format(**config))
      if 'ldflags' in t.data:
        buildfile.write('  addl_ldflags = {}\n'.format(t.data['ldflags']))
    
    buildfile.write('build {}: phony {}\n'.format('{config}', ' '.join([t.target for t in executable_things])).format(**config))
  
  buildfile.write('\ndefault {}\n'.format(' '.join(default_targets)))
