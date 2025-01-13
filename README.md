This repository contains the motion planning simulation search system described in chapter 2 of Jeffrey Bosboom's Ph.D. thesis, [Exhaustive Search and Hardness Proofs for Games](https://dspace.mit.edu/handle/1721.1/129245), as well as the automata-based nonogram solver described in chapter 6.

### Dependencies

This project requires a C++ compiler (tested with GCC 14.2.1) and the following libraries:
- [Boost](https://www.boost.org/) (tested with 1.86.0)
- [doctest](https://github.com/doctest/doctest) (tested with 2.4.11)
- fmt (tested with 11.1.1)
- jemalloc (tested with 5.3.0)
- LMDB (tested with 0.9.33)
- [msgpack-cxx](https://github.com/msgpack/msgpack-c) (tested with 7.0.0)
- simdjson (tested with 3.11.3)
- [Google sparsehash](https://github.com/sparsehash/sparsehash) (tested with 2.0.4)
- [tsl/hopscotch_map](https://github.com/Tessil/hopscotch-map) (tested with 2.3.1)
- [tsl/ordered_map](https://github.com/Tessil/ordered-map) (tested with 1.1.0)
- xxhash (tested with 0.8.2)
- yaml-cpp (tested with 0.8.0)

Building requires Python 3 and ninja.  Generating gadget definitions requires PyYAML.

[socat](http://www.dest-unreach.org/socat/) needs to be installed as `/usr/bin/socat`.

### Example gadget search: P2T simulates A2T

The following script demonstrates how to use the gadget search system to find a simulation of an antiparallel 2-toggle by parallel 2-toggles and unconstrained vertices (Figure 2-1 of the thesis).

```bash
# build required executables
python3 ./genbuild.py
ninja build/release/bin/toggles-driver.exe \
  build/release/bin/toggles-runner.exe \
  build/release/bin/toggles-report.exe
# generate gadget definitions
mkdir build/gadgetdefs
python3 src/toggles/generate_doors.py > build/gadgetdefs/doors.yaml
python3 src/toggles/generate-io-gadgets.py build/gadgetdefs/
python3 src/toggles/tunnels.py src/toggles/tunnels.yaml src/toggles/tunnels-all.yaml \
  > build/gadgetdefs/tunnels.yaml 2> /dev/null
# initialize database (will take several minutes)
mkdir database.mdb
build/release/bin/toggles-runner.exe sync \
  --db-path ./database.mdb \
  --log ./database.mdb/synclog.txt \
  src/toggles/gadgets.yaml build/gadgetdefs/*
# do the search
mkdir /var/tmp/toggles
nthreads=$(lscpu -p | grep -c '^[0-9]')
PATH="$(pwd)/build/release/bin/:$PATH" build/release/bin/toggles-driver.exe \
  --precision 10 --max-states 8 --max-components 1 --combine-max-locations 6 \
  --skip-close --skip-mirror --stop-after 9 \
  --threads $nthreads --db-path ./database.mdb \
  toggle-toggle-parallel 3-split | tee database.mdb/log.txt
# generate the report
PATH="$(pwd)/build/release/bin/:$PATH" build/release/bin/toggles-report.exe \
  --threads $nthreads --db-path ./database.mdb \
  toggle-toggle-parallel 3-split > ./database.mdb/report.txt
```
