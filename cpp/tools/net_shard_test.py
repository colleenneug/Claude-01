#!/usr/bin/env python3
"""Many servers for one game: a main server and two instance servers.

    net_shard_test.py <path to erebus_native>

Starts them itself (on free ports), then checks with fake players that new
copies of places are spread over the instance servers, that a copy works
through its instance server (players see each other, the co-op host, PvP
hits), that squads still work across instance servers (they are the main
server's), and that when an instance server dies its players are moved.
"""
import os
import random
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import net_test as nt


def main():
    binary = os.path.abspath(sys.argv[1])
    base = 29000 + random.randint(0, 800)
    env = dict(os.environ, EREBUS_SHARD_KEY='test-key', EREBUS_SERVER_SECONDS='60',
               EREBUS_SERVER_TAGS=os.path.join(os.environ.get('TMPDIR', '/tmp'), 'erebus_shard_tags_%d.txt' % base))
    procs = [subprocess.Popen([binary, '--server', str(base)], env=env, stdout=subprocess.DEVNULL)]
    time.sleep(0.4)
    for k in (1, 2):
        procs.append(subprocess.Popen([binary, '--server', str(base + k), '--join', '127.0.0.1:%d' % base],
                                      env=env, stdout=subprocess.DEVNULL))
    fails = 0

    def check(name, ok, detail=''):
        nonlocal fails
        print(('  PASS ' if ok else '  FAIL ') + name + ('' if ok else '  (' + str(detail) + ')'))
        fails += 0 if ok else 1

    try:
        time.sleep(2.5)   # both instance servers say hello
        places = ['GLACIUS', 'KARRASH', 'THE DUST SHELF', 'THE OUTSKIRTS']
        players = [nt.Fake(base, 100 + i, 'S%d' % i) for i in range(8)]
        for i, f in enumerate(players):
            f.hello(places[i % 4], 'freeroam')
            nt.settle(players[:i + 1], 0.1)
        nt.settle(players, 1.0)
        servers = set(f.inst[1] for f in players if f.inst)
        check('copies of places run on the instance servers', all(f.inst for f in players),
              [f.inst for f in players])
        check('new copies are spread over both of them', servers == {base + 1, base + 2}, servers)
        a, b = players[0], players[4]   # same place
        check('players in one copy see each other through its server', b.id in a.seen and a.id in b.seen,
              (a.seen, b.seen))
        check('the co-op host is set by the instance server', a.host and a.host == b.host, (a.host, b.host))

        # Squads are the main server's, whichever instance servers the two are on.
        c = players[1]
        a.send(nt.SINV, struct.pack('<Q', c.id))
        nt.settle(players, 0.4)
        c.send(nt.SACC, struct.pack('<Q', a.id))
        nt.settle(players, 0.6)
        check('a squad forms across instance servers', sorted(a.squad) == sorted([a.id, c.id]), a.squad)

        # PvP through an instance server.
        p1, p2 = nt.Fake(base, 300, 'PITA'), nt.Fake(base, 301, 'PITB')
        p1.hello('THE PIT', 'pvp')
        p2.hello('THE PIT', 'pvp')
        nt.settle(players + [p1, p2], 0.8, activity='pvp')
        p1.send(nt.HIT, struct.pack('<QfB', p2.id, 25.0, 0))
        nt.settle([p1, p2], 0.4, activity='pvp')
        check('a PvP hit travels through the instance server', any(h[0] == p1.id for h in p2.hits), p2.hits)

        # An instance server dies: its players are placed again.
        dead = a.inst[1]
        victims = [f for f in players if f.inst and f.inst[1] == dead]
        procs[dead - base].kill()
        end = time.time() + 16
        while time.time() < end and any(f.inst and f.inst[1] == dead for f in victims):
            nt.settle(players, 0.5)
        check('when an instance server dies its players are moved', victims and
              all(f.inst is None or f.inst[1] != dead for f in victims), [f.inst for f in victims])
        nt.settle(victims, 0.6)
        check('...and land somewhere that works', all(f.instance for f in victims), [f.instance for f in victims])
        for f in players + [p1, p2]:
            f.send(nt.BYE)
    finally:
        for p in procs:
            p.kill()
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
