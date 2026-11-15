#!/usr/bin/env python3
"""Drives a running `erebus_native --server` with fake players over UDP.

    net_test.py <port>

Checks the rules the server is there to enforce, without a window:
  - a place fills one instance up to its cap, then opens a second one;
  - a squad mate arriving somewhere is pulled into their squad's instance;
  - a squad's size is limited by what its leader is doing (3 for a mission);
  - friend requests reach the other player and both sides hear the result;
  - snapshots carry everyone else in the instance and nobody from outside it.
Prints PASS/FAIL lines and exits non-zero on any failure.
"""
import socket
import struct
import sys
import time

MAGIC, VERSION = 0x31425245, 1
HELLO, STATE, BYE, FIND, FREQ, FACC, WATCH, SINV, SACC, SLEAVE = range(1, 11)
WELCOME, SNAP, FOUND, FREQ_FROM, FADDED, PRESENCE, SINV_FROM, SQUAD, NOTICE = range(64, 73)


def s16(text):
    b = text.encode()
    return struct.pack('<H', len(b)) + b


class Reader:
    def __init__(self, b):
        self.b, self.at = b, 0

    def take(self, fmt):
        n = struct.calcsize(fmt)
        v = struct.unpack_from(fmt, self.b, self.at)
        self.at += n
        return v[0] if len(v) == 1 else v

    def str(self):
        n = self.take('<H')
        v = self.b[self.at:self.at + n].decode()
        self.at += n
        return v


class Fake:
    def __init__(self, port, pid, name):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(('127.0.0.1', 0))
        self.sock.setblocking(False)
        self.server = ('127.0.0.1', port)
        self.id, self.name = pid, name
        self.instance = None
        self.seen = set()
        self.squad = []
        self.limit = None
        self.notices = []
        self.invites = []
        self.friends = []

    def send(self, msg, body=b''):
        self.sock.sendto(struct.pack('<IBB', MAGIC, VERSION, msg) + struct.pack('<Q', self.id) + body,
                         self.server)

    def hello(self, place, activity):
        self.send(HELLO, s16(self.name) + s16(place) + s16(activity))

    def state(self, activity):
        self.send(STATE, struct.pack('<6fB', 1, 0, 2, 90, 0, 1, 0) + s16('rifle') + s16(activity))

    def pump(self):
        while True:
            try:
                data, _ = self.sock.recvfrom(4096)
            except BlockingIOError:
                return
            r = Reader(data)
            magic, ver, msg = r.take('<I'), r.take('<B'), r.take('<B')
            if magic != MAGIC or ver != VERSION:
                continue
            if msg == WELCOME:
                r.take('<I')
                self.instance = r.str()
            elif msg == SNAP:
                self.seen = set()
                for _ in range(r.take('<B')):
                    pid = r.take('<Q')
                    r.str()
                    r.take('<6f')
                    r.take('<B')
                    r.str()
                    r.take('<B')
                    self.seen.add(pid)
            elif msg == SQUAD:
                r.take('<Q')
                self.limit = r.take('<B')
                n = r.take('<B')
                self.squad = []
                for _ in range(n):
                    pid = r.take('<Q')
                    r.str()
                    r.str()
                    self.squad.append(pid)
            elif msg in (FREQ_FROM, SINV_FROM):
                self.invites.append((msg, r.take('<Q'), r.str()))
            elif msg == FADDED:
                self.friends.append(r.take('<Q'))
            elif msg == NOTICE:
                self.notices.append(r.str())


def settle(fakes, seconds=0.4, activity='freeroam'):
    end = time.time() + seconds
    while time.time() < end:
        for f in fakes:
            f.state(activity)
            f.pump()
        time.sleep(0.03)


def main():
    port = int(sys.argv[1])
    fails = 0

    def check(name, ok, detail=''):
        nonlocal fails
        print(('  PASS ' if ok else '  FAIL ') + name + ('' if ok else '  (' + str(detail) + ')'))
        fails += 0 if ok else 1

    # Thirteen players on one patrol: twelve fit in an instance.
    crowd = [Fake(port, 1000 + i, 'P%d' % i) for i in range(13)]
    for f in crowd:
        f.hello('DUST SHELF', 'freeroam')
        settle(crowd, 0.05)
    settle(crowd, 0.6)
    names = sorted(set(f.instance for f in crowd))
    check('a full patrol opens a second instance', names == ['DUST SHELF 1', 'DUST SHELF 2'], names)
    first = [f for f in crowd if f.instance == 'DUST SHELF 1']
    check('an instance holds twelve', len(first) == 12, len(first))
    check('snapshots carry everyone else in the instance',
          all(len(f.seen) == 11 for f in first), [len(f.seen) for f in first])
    lone = [f for f in crowd if f.instance == 'DUST SHELF 2'][0]
    check('and nobody from another instance', len(lone.seen) == 0, lone.seen)
    for f in crowd:
        f.send(BYE)
    settle(crowd, 0.3)

    # Squad pull: two on the same patrol in different instances; once in a
    # squad, the one who accepts is moved into the leader's instance.
    a, b = Fake(port, 2001, 'ALPHA'), Fake(port, 2002, 'BRAVO')
    filler = [Fake(port, 2100 + i, 'F%d' % i) for i in range(11)]
    a.hello('GLASS FLATS', 'freeroam')
    settle([a], 0.2)
    for f in filler:
        f.hello('GLASS FLATS', 'freeroam')
    settle([a] + filler, 0.4)
    b.hello('GLASS FLATS', 'freeroam')   # instance 1 is full: B gets instance 2
    settle([a, b] + filler, 0.3)
    check('a full instance sends the next arrival elsewhere', a.instance != b.instance, (a.instance, b.instance))
    # Make room, then link up.
    filler[0].send(BYE)
    settle([a, b] + filler[1:], 0.3)
    a.send(SINV, struct.pack('<Q', b.id))
    settle([a, b], 0.3)
    check('a squad invite reaches the other player', any(i[0] == SINV_FROM and i[1] == a.id for i in b.invites),
          b.invites)
    b.send(SACC, struct.pack('<Q', a.id))
    settle([a, b] + filler[1:], 0.4)
    check('accepting forms the squad on both sides', sorted(a.squad) == sorted([a.id, b.id]) == sorted(b.squad),
          (a.squad, b.squad))
    check('accepting pulls you into the leader\'s instance', a.instance == b.instance, (a.instance, b.instance))
    check('an open place takes a squad of six', a.limit == 6, a.limit)
    for f in filler[1:]:
        f.send(BYE)

    # Squad limit: a mission is a fireteam of three.
    settle([a, b], 0.2, activity='mission')
    c, d = Fake(port, 2003, 'CHARLIE'), Fake(port, 2004, 'DELTA')
    for f in (c, d):
        f.hello('GLASS FLATS', 'freeroam')
    settle([a, b, c, d], 0.3, activity='mission')
    check('a mission squad\'s limit is three', a.limit == 3, a.limit)
    a.send(SINV, struct.pack('<Q', c.id))
    settle([a, b, c, d], 0.2, activity='mission')
    c.send(SACC, struct.pack('<Q', a.id))
    settle([a, b, c, d], 0.3, activity='mission')
    check('a third joins the fireteam', len(a.squad) == 3, a.squad)
    a.notices.clear()
    a.send(SINV, struct.pack('<Q', d.id))
    settle([a, b, c, d], 0.3, activity='mission')
    check('a fourth is refused for a mission', any('FULL' in n for n in a.notices) and
          not any(i[1] == a.id for i in d.invites), (a.notices, d.invites))

    # Friends.
    d.send(FREQ, struct.pack('<Q', a.id))
    settle([a, d], 0.3)
    req = [i for i in a.invites if i[0] == FREQ_FROM and i[1] == d.id]
    check('a friend request arrives', len(req) == 1, a.invites)
    a.send(FACC, struct.pack('<Q', d.id))
    settle([a, d], 0.3)
    check('both sides hear the friendship', d.id in a.friends and a.id in d.friends, (a.friends, d.friends))
    d.send(FIND, s16('alpha'))
    settle([d], 0.2)

    # Leaving: the leader goes, the squad carries on under the next member.
    a.send(BYE)
    settle([b, c, d], 0.4, activity='mission')
    check('the squad outlives its leader', sorted(b.squad) == sorted([b.id, c.id]), b.squad)
    for f in (b, c, d):
        f.send(BYE)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
