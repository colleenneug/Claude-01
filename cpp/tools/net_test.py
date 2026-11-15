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
(HELLO, STATE, BYE, FIND, FREQ, FACC, WATCH, SINV, SACC, SLEAVE, HIT, KILL,
 HOSTILES, HOSTILE_HIT, ALLY_DAMAGE) = range(1, 16)
(WELCOME, SNAP, FOUND, FREQ_FROM, FADDED, PRESENCE, SINV_FROM, SQUAD, NOTICE,
 HIT_FROM, SCORE, MATCH_OVER, HOST) = range(64, 77)


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
        self.hits = []
        self.score = {}
        self.leader = 0
        self.winner = None
        self.host = 0
        self.hostiles = []     # (sender, count)
        self.hostile_hits = []
        self.ally_damage = []

    def send(self, msg, body=b''):
        self.sock.sendto(struct.pack('<IBB', MAGIC, VERSION, msg) + struct.pack('<Q', self.id) + body,
                         self.server)

    def hello(self, place, activity, follow=0):
        self.send(HELLO, s16(self.name) + s16(place) + s16(activity) + struct.pack('<Q', follow))

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
            elif msg == HIT_FROM:
                self.hits.append((r.take('<Q'), r.take('<f'), r.take('<B')))
            elif msg == SCORE:
                r.take('<B')
                self.leader = r.take('<Q')
                self.score = {}
                for _ in range(r.take('<B')):
                    pid = r.take('<Q')
                    r.str()
                    self.score[pid] = r.take('<H')
            elif msg == MATCH_OVER:
                self.winner = r.str()
            elif msg == HOST:
                self.host = r.take('<Q')
            elif msg == HOSTILES:
                self.hostiles.append((r.take('<Q'), r.take('<H')))
            elif msg == HOSTILE_HIT:
                self.hostile_hits.append((r.take('<Q'), r.take('<H'), r.take('<f')))
            elif msg == ALLY_DAMAGE:
                r.take('<Q')
                self.ally_damage.append((r.take('<Q'), r.take('<f')))


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

    # PvP: hits only travel between players in the same PvP instance; downs
    # score for whoever did them; first to the limit wins the match.
    e, g, h = Fake(port, 3001, 'ECHO'), Fake(port, 3002, 'GOLF'), Fake(port, 3003, 'HOTEL')
    e.hello('THE PIT', 'pvp')
    g.hello('THE PIT', 'pvp')
    h.hello('DUST SHELF', 'freeroam')
    settle([e, g, h], 0.4, activity='pvp')
    e.send(HIT, struct.pack('<QfB', g.id, 30.0, 1))
    e.send(HIT, struct.pack('<QfB', h.id, 30.0, 0))
    settle([e, g, h], 0.3, activity='pvp')
    check('a PvP hit reaches its target', any(x[0] == e.id and abs(x[1] - 30.0) < 0.01 and x[2] == 1 for x in g.hits),
          g.hits)
    check('and never anyone outside the Pit', not h.hits, h.hits)
    g.send(KILL, struct.pack('<QB', e.id, 2))
    settle([e, g], 0.3, activity='pvp')
    check('a down scores for whoever did it', e.score.get(e.id) == 1 and g.score.get(e.id) == 1, (e.score, g.score))
    check('the leader is singing', e.leader == e.id and g.leader == e.id, (e.leader, g.leader))
    g.send(KILL, struct.pack('<QB', e.id, 2))
    settle([e, g], 0.3, activity='pvp')
    check('first to the limit wins the match', e.winner == 'ECHO' and g.winner == 'ECHO', (e.winner, g.winner))
    for f in (e, g, h):
        f.send(BYE)

    # Co-op: the first in an instance runs the Cybers, only its word on them
    # is passed on, guests' hits go to it, and it hands over when it leaves.
    i, j, k = Fake(port, 4001, 'INDIA'), Fake(port, 4002, 'JULIET'), Fake(port, 4003, 'KILO')
    i.hello('RED MESA', 'freeroam')
    settle([i], 0.2)
    j.hello('RED MESA', 'freeroam')
    k.hello('RED MESA', 'freeroam')
    settle([i, j, k], 0.4)
    check('the first one in hosts the Cybers', i.host == i.id and j.host == i.id and k.host == i.id,
          (i.host, j.host, k.host))
    blob = struct.pack('<H', 1) + struct.pack('<H5fBB', 0, 1, 0, 2, 0, 1.0, 255, 1)[:2 + 16 + 4] + bytes([255, 1])
    i.send(HOSTILES, blob)
    j.send(HOSTILES, blob)
    settle([i, j, k], 0.3)
    check('the host\'s Cybers reach everyone else', any(x[0] == i.id for x in k.hostiles) and
          any(x[0] == i.id for x in j.hostiles), (j.hostiles, k.hostiles))
    check('and nobody else speaks for them', not any(x[0] == j.id for x in k.hostiles + i.hostiles),
          (i.hostiles, k.hostiles))
    j.send(HOSTILE_HIT, struct.pack('<HfB', 3, 42.0, 0))
    settle([i, j, k], 0.3)
    check('a guest\'s hit goes to the host', any(x[0] == j.id and x[1] == 3 for x in i.hostile_hits) and
          not k.hostile_hits, (i.hostile_hits, k.hostile_hits))
    i.send(ALLY_DAMAGE, struct.pack('<Qf', k.id, 12.0))
    settle([i, j, k], 0.3)
    check('a Cyber\'s hit on a guest reaches only them', any(x[0] == k.id for x in k.ally_damage) and
          not j.ally_damage, (j.ally_damage, k.ally_damage))
    i.send(BYE)
    settle([j, k], 0.4)
    check('the host hands over when it leaves', j.host == j.id and k.host == j.id, (j.host, k.host))
    for f in (j, k):
        f.send(BYE)

    # Joining a friend lands you in their instance, wherever random would
    # have put you. Two instances of the place: the friend's (with room) and
    # another; ten tries, every one has to land with the friend.
    friend = Fake(port, 6000, 'NOVEMBER')
    friend.hello('ASH FIELDS', 'freeroam')
    settle([friend], 0.2)
    crowd2 = [Fake(port, 6100 + n, 'Q%d' % n) for n in range(11)]
    for f in crowd2:
        f.hello('ASH FIELDS', 'freeroam')
    settle([friend] + crowd2, 0.4)
    other = Fake(port, 6200, 'OSCAR')
    other.hello('ASH FIELDS', 'freeroam')   # instance 1 is full: a second opens
    settle([friend, other] + crowd2, 0.3)
    crowd2[0].send(BYE)                       # room for one in the friend's
    settle([friend, other] + crowd2[1:], 0.3)
    landed = []
    for n in range(10):
        j = Fake(port, 6300 + n, 'J%d' % n)
        j.hello('ASH FIELDS', 'freeroam', follow=friend.id)
        settle([j, friend, other], 0.15)
        landed.append(j.instance == friend.instance)
        j.send(BYE)
        settle([friend, other], 0.1)
    check('joining a friend lands you in their instance', all(landed), landed)
    for f in [friend, other] + crowd2[1:]:
        f.send(BYE)

    # A story mission is private: two strangers on the same mission do not
    # share an instance.
    l, m = Fake(port, 5001, 'LIMA'), Fake(port, 5002, 'MIKE')
    l.hello('THE GREAT CAVE', 'mission')
    m.hello('THE GREAT CAVE', 'mission')
    settle([l, m], 0.3, activity='mission')
    check('strangers never share a story mission', l.instance and m.instance and l.instance != m.instance,
          (l.instance, m.instance))
    for f in (l, m):
        f.send(BYE)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
