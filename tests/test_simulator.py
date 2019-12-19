#!/usr/bin/env python
# -*- coding: utf-8; -*-

from __future__ import print_function

import sys
import os
import filecmp
import subprocess
import argparse
import os.path

class TestCase:
    def __init__(self, protocol, **parameters):
        self.protocol = protocol
        self.parameters = parameters

    def env(self):
        result = {"PROTOCOL": self.protocol}
        result.update({k.upper(): str(v) for k, v in self.parameters.items()})
        return result

    def __str__(self):
        return "%s - %s" % (self.protocol, str(self.parameters))

tests = {
    'default_noloss': [
        TestCase("gack"),
        TestCase("gsaw"),
        TestCase("noack"),
        TestCase("nocode"),
        TestCase("pace"),
        TestCase("pacemg"),
        TestCase("sliding_window"),
        TestCase("sliding_window", feedback=0),
        TestCase("tetrys"),
        TestCase("chain", stage0="sliding_window", stage0_sequence=1, stage1="noack"),
    ],
    'default': [
        TestCase("gack"),
        TestCase("noack", redundancy=20),
        # BROKEN(hangs): TestCase("pace", pace_redundancy=2, tail_redundancy=20),
        # BROKEN(hangs): TestCase("pacemg"),
        TestCase("sliding_window", sequence=1, feedback=1, redundancy=0),
        TestCase("sliding_window", sequence=1, feedback=0, redundancy=10),
        TestCase("chain", stage0="noack", stage0_redundancy=10, stage1="noack", stage1_redundancy=10),
        TestCase("chain", stage0="sliding_window", stage0_sequence=1, stage0_redundancy=0, stage1="noack", stage1_redundancy=5),
        # BROKEN(hangs): TestCase("tetrys"),
    ],
    "single_rec": [
        TestCase("gack"),
        # BROKEN: TestCase("noack", redundancy=17),
        # BROKEN(different output): "nocode",
        # BROKEN(hangs): "pace",
        # BROKEN(hangs): "pacemg",
        TestCase("sliding_window", fb_timeout="10ms", redundancy=0),
        TestCase("sliding_window", feedback=0, redundancy=16),
    ],
    "double_rec": [
        TestCase("gack"),
        TestCase("sliding_window", fb_timeout="10ms", redundancy=0),
        TestCase("sliding_window", feedback=0, redundancy=20),
    ],
}

def error_str(s):
    return '\033[91m' + s + '\033[0m'

def ok_str(s):
    return '\033[92m' + s + '\033[0m'

def randomhex(length):
    data = os.urandom(int(0.5 + length/float(2)))

    if hasattr(data, "hex"):
        # python 3 bytes
        result = data.hex()
    else:
        # python 2 str
        result = data.encode("hex")

    assert(len(result) >= length)
    return result[:length]

def prepare_testdata(packets, packet_size, ascii):
    # don't recreate file when it exists and has correct size
    if os.path.isfile("block") and os.path.getsize("block") == packets*packet_size:
        return

    if ascii:
        with open('block', 'w') as f:
            for i in range(packets):
                data = randomhex(packet_size-1)
                print(data, file=f)
    else:
        with open('block', 'wb') as f:
            f.write(os.urandom(packets*packet_size))

def get_simulator_name():
    # we are in the build directory under tests/
    if os.path.isfile('../bin/simulator'):
        return '../bin/simulator'
    else:
        return 'bin/simulator'

def run_test(topology, test):
    print(ok_str("Perform test: %s - %s" % (topology, str(test))))

    parameters = test.env()
    env = os.environ.copy()
    env.update(parameters)
    env["SYMBOL_SIZE"] = "1500"

    with open('out', 'wb') as fo, open('block', 'rb') as fi:
        cmd = [get_simulator_name(), topology]
        cmdline = "%s < block > out" % " ".join(["%s=%s"%(k,v) for k, v in parameters.items()] + cmd)
        print(ok_str(cmdline))
        p = subprocess.Popen([get_simulator_name(), topology], env=env, stdout=fo, stdin=fi)
        p.wait()

        if p.returncode != 0:
            print(error_str("error while executing test"), file=sys.stderr)
            return False

    if not filecmp.cmp('block', 'out'):
        print(error_str("input and output file differ"), file=sys.stderr)
        return False

    return True

def perform_tests(topology, filter_protocol):
    for test in tests[topology]:
        if filter_protocol != None and filter_protocol != test.protocol:
            continue

        if not run_test(topology, test):
            print(error_str("failed to perform test: %s - %s" % (topology, str(test))), file=sys.stderr)
            sys.exit(1)

def main():
    parser = argparse.ArgumentParser(description='Run simulator with different parameters and protocols.')
    parser.add_argument('--protocol', help='Protocols to test against (default: run all)')
    parser.add_argument('--topology', help='Tests to run (default: run all)')
    parser.add_argument('--ascii', help='Generate an ASCII test file', default=False, action="store_true")

    args = parser.parse_args()

    prepare_testdata(10000, 1498, args.ascii)

    if args.topology:
        perform_tests(args.topology, args.protocol)
    else:
        for topo in sorted(tests):
            perform_tests(topo, args.protocol)

if __name__ == '__main__':
    main()
