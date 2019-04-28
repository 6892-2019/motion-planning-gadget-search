import subprocess
import msgpack


def make_msgpack_rpc_call(command, *args, sequence_number=0, compress=False):
    if compress:
        raise NotImplementedError('RPC call compression not implemented')
    if not isinstance(command, str):
        raise ValueError('command {} is not a str'.format(command))
    return msgpack.packb([0, sequence_number, command, args], use_bin_type=True)


def unpack_msgpack_rpc_response(resp_bytes, expected_sequence_number=0, compress=False):
    if compress:
        raise NotImplementedError('RPC response compression not implemented')
    resp = msgpack.unpackb(resp_bytes, raw=False)
    if len(resp) != 4 or resp[0] != 1:
        raise ValueError('malformed RPC response: {}'.format(resp))
    if resp[1] != expected_sequence_number:
        raise ValueError('unexpected sequence number: expected {}, got {}'.format(expected_sequence_number, resp[1]))
    return (resp[2], resp[3])


def local_toggles_runner(script_args, command, *args, error_as_exc=True):
    # We're not using the sequence number for anything here.
    cmd_bytes = make_msgpack_rpc_call(command, *args)
    with open('/tmp/errorcmd.bin', 'wb') as whatever:
        whatever.write(cmd_bytes)

    results = subprocess.run([script_args.runner_path], check=True, input=cmd_bytes, capture_output=True)
    error, retval = unpack_msgpack_rpc_response(results.stdout)
    if error:
        raise ValueError('RPC returned error: command {}, args {}, error {}'.format(command, args, error))
    return retval
