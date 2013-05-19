#!/usr/bin/env python2

import sys, zmq, cmd

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
        print 'Sending command:[%s]' % cmd
        self.requester.send(cmd)
        message = self.requester.recv()
        print 'Received reply:[%s]' % message

try:
    bind_address = sys.argv[1] if len(sys.argv) > 1 else "tcp://localhost:5555"
    LavfiCmd(bind_address).cmdloop('FFmpeg libavfilter interactive shell')
except KeyboardInterrupt:
    pass
