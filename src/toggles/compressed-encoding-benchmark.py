#!/usr/bin/python3
# SPDX-License-Identifier: MIT
# Copyright 2019 Massachusetts Institute of Technology

import lmdb, snappy, zstandard
import argparse, itertools

def header_len(gadget):
    q = gadget[1]
    # First two bytes, 1/2 for states, 0/1/2/3 for each edge type, 0/1 for components
    return 3 + ((q & 0b100000) >> 5) + ((q & 0b11000) >> 3) + ((q & 0b110) >> 1) + (q & 0b1)

def rle(data):
    ret = 0
    for k, g in itertools.groupby(data):
        l = sum(1 for _ in g)
        if l > 255:
            # We could varint-encode, I guess... or just emit two runs.
            print("warning:", l)
        # escape, symbol, length, or just the symbols if that's cheaper
        ret += min(l, 3)
    return ret

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--dump-bin', type=str)
    parser.add_argument('database', type=str)
    args = parser.parse_args()

    dumpfile = open(args.dump_bin if args.dump_bin else '/dev/null', 'wb')

    zstd_level = 1
    zstd_dict = zstandard.ZstdCompressionDict(open('/tmp/dict.zstd', 'rb').read())
    zstd_dict.precompute_compress(level=1)
    zstd_dict8 = zstandard.ZstdCompressionDict(open('/tmp/dict.zstd', 'rb').read())
    zstd_dict8.precompute_compress(level=8)

    env = lmdb.Environment(args.database, map_size=1*1024*1024*1024, readonly=True,
            create=False, max_dbs=16)
    raw_length = 0
    rle_length = 0
    snappy_length = 0
    zstd_length1 = 0
    zstd_length1d = 0
    zstd_length8 = 0
    zstd_length8d = 0
    samples = []
    with env.begin() as txn:
        gadget_hashtable = env.open_db('gadget_hashtable'.encode(), txn, create=False)
        with txn.cursor(gadget_hashtable) as cur:
            for key, value in cur:
                # Get just the edge data.
                edges = value[header_len(value):-8]
                dumpfile.write(edges)
                raw_length += len(edges)
                rle_length += rle(edges)
                snappy_length += len(snappy.compress(edges)) - (1 if len(edges) < 127 else 2)

                zcmp1 = zstandard.ZstdCompressor(level=1,
                    write_checksum=False, write_content_size=False, write_dict_id=False)
                zcmp1d = zstandard.ZstdCompressor(level=1, dict_data=zstd_dict,
                    write_checksum=False, write_content_size=False, write_dict_id=False)
                zcmp8 = zstandard.ZstdCompressor(level=8,
                    write_checksum=False, write_content_size=False, write_dict_id=False)
                zcmp8d = zstandard.ZstdCompressor(level=8, dict_data=zstd_dict8,
                    write_checksum=False, write_content_size=False, write_dict_id=False)
                zstd_length1 += len(zcmp1.compress(edges)) - 8
                zstd_length1d += len(zcmp1d.compress(edges)) - 8
                zstd_length8 += len(zcmp8.compress(edges)) - 8
                zstd_length8d += len(zcmp8d.compress(edges)) - 8
                # samples.append(edges)

    # dict_data = zstandard.train_dictionary(1024*1024, samples)
    # with open('/tmp/dict.zstd', 'wb') as dictfile:
    #     dictfile.write(dict_data.as_bytes())

    print('raw:  ', raw_length)
    print('rle:  ', rle_length)
    print('snpy: ', snappy_length)
    print('zstd1:', zstd_length1)
    print('zstd1d:', zstd_length1d)
    print('zstd8:', zstd_length8)
    print('zstd8d:', zstd_length8d)
    dumpfile.close()