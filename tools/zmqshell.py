#!/usr/bin/env python3

import argparse
import cmd
import logging
import sys
import zmq

HELP = '''
Provide a shell used to send interactive commands to a zmq filter.

The command assumes there is a running zmq or azmq filter acting as a
ZMQ server.

You can send a command to it, follwing the syntax:
TARGET COMMAND [COMMAND_ARGS]

* TARGET is the target filter identifier to send the command to
* COMMAND is the name of the command sent to the filter
* COMMAND_ARGS is the optional specification of command arguments

See the zmq/azmq filters documentation for more details, and the
zeromq documentation at:
https://zeromq.org/
'''

logging.basicConfig(format='zmqshell|%(levelname)s> %(message)s', level=logging.INFO)
log = logging.getLogger()


class LavfiCmd(cmd.Cmd):
    prompt = 'lavfi> '

    def __init__(self, bind_address):
        context = zmq.Context()
        self.requester = context.socket(zmq.REQ)
        self.requester.connect(bind_address)
        cmd.Cmd.__init__(self)

    def onecmd(self, cmd):
        if cmd == 'EOF':
            sys.exit(0)
        log.info(f"Sending command: {cmd}")
        self.requester.send_string(cmd)
        response = self.requester.recv_string()
        log.info(f"Received response: {response}")


class Formatter(
    argparse.ArgumentDefaultsHelpFormatter, argparse.RawDescriptionHelpFormatter
):
    pass


def main():
    parser = argparse.ArgumentParser(description=HELP, formatter_class=Formatter)
    parser.add_argument('--bind-address', '-b', default='tcp://localhost:5555', help='specify bind address used to communicate with ZMQ')

    args = parser.parse_args()
    try:
        LavfiCmd(args.bind_address).cmdloop('FFmpeg libavfilter interactive shell')
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
