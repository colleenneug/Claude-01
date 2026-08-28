/* ============================================================
   A minimal RFC 6455 WebSocket server.

   The rest of this project has no dependencies and runs from a bare
   checkout, and the co-op server should not be the thing that breaks
   that. This implements just enough of the protocol for the game:
   the upgrade handshake, text frames in both directions, ping/pong
   and close. No extensions, no fragmentation on send.
   ============================================================ */
'use strict';

const crypto = require('crypto');
const GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';

class Socket {
  constructor(raw) {
    this.raw = raw;
    this.open = true;
    this.buffer = Buffer.alloc(0);
    this.handlers = { message: [], close: [], pong: [] };
    this.isAlive = true;

    raw.on('data', (chunk) => this._onData(chunk));
    raw.on('error', () => this.close());
    raw.on('close', () => this._fireClose());
  }

  on(event, fn) { (this.handlers[event] || []).push(fn); return this; }
  _fire(event, arg) { for (const fn of this.handlers[event] || []) fn(arg); }

  _fireClose() {
    if (!this.open) return;
    this.open = false;
    this._fire('close');
  }

  /* ---------- reading ---------- */
  _onData(chunk) {
    this.buffer = Buffer.concat([this.buffer, chunk]);
    for (;;) {
      const frame = this._readFrame();
      if (!frame) return;
      const { opcode, payload } = frame;

      if (opcode === 0x8) { this.close(); return; }          // close
      if (opcode === 0x9) { this._send(0xA, payload); continue; }  // ping -> pong
      if (opcode === 0xA) { this.isAlive = true; this._fire('pong'); continue; }
      if (opcode === 0x1) {
        let msg = null;
        try { msg = JSON.parse(payload.toString('utf8')); } catch (err) { continue; }
        this._fire('message', msg);
      }
      // binary and continuation frames are not used by this protocol
    }
  }

  _readFrame() {
    const b = this.buffer;
    if (b.length < 2) return null;

    const opcode = b[0] & 0x0f;
    const masked = (b[1] & 0x80) === 0x80;
    let len = b[1] & 0x7f;
    let offset = 2;

    if (len === 126) {
      if (b.length < offset + 2) return null;
      len = b.readUInt16BE(offset); offset += 2;
    } else if (len === 127) {
      if (b.length < offset + 8) return null;
      const big = b.readBigUInt64BE(offset);
      if (big > 4194304n) { this.close(); return null; }     // 4 MB sanity cap
      len = Number(big); offset += 8;
    }

    let mask = null;
    if (masked) {
      if (b.length < offset + 4) return null;
      mask = b.slice(offset, offset + 4); offset += 4;
    }
    if (b.length < offset + len) return null;

    const payload = Buffer.from(b.slice(offset, offset + len));
    if (mask) for (let i = 0; i < payload.length; i++) payload[i] ^= mask[i % 4];

    this.buffer = b.slice(offset + len);
    return { opcode, payload };
  }

  /* ---------- writing ---------- */
  _send(opcode, payload) {
    if (!this.open) return;
    const len = payload.length;
    let header;
    if (len < 126) {
      header = Buffer.alloc(2);
      header[1] = len;
    } else if (len < 65536) {
      header = Buffer.alloc(4);
      header[1] = 126;
      header.writeUInt16BE(len, 2);
    } else {
      header = Buffer.alloc(10);
      header[1] = 127;
      header.writeBigUInt64BE(BigInt(len), 2);
    }
    header[0] = 0x80 | opcode;                                // FIN + opcode
    try { this.raw.write(Buffer.concat([header, payload])); }
    catch (err) { this.close(); }
  }

  send(obj) { this._send(0x1, Buffer.from(JSON.stringify(obj), 'utf8')); }
  ping() { this.isAlive = false; this._send(0x9, Buffer.alloc(0)); }

  close() {
    if (!this.open) return;
    this._send(0x8, Buffer.alloc(0));
    this.open = false;
    try { this.raw.end(); } catch (err) { /* already gone */ }
    this._fire('close');
  }
}

/* Attach to a node http server; calls onConnection(socket, request). */
function attach(server, path, onConnection) {
  server.on('upgrade', (req, raw, head) => {
    if (path && req.url.split('?')[0] !== path) { raw.destroy(); return; }
    const key = req.headers['sec-websocket-key'];
    if (!key) { raw.destroy(); return; }

    const accept = crypto.createHash('sha1').update(key + GUID).digest('base64');
    raw.write(
      'HTTP/1.1 101 Switching Protocols\r\n' +
      'Upgrade: websocket\r\n' +
      'Connection: Upgrade\r\n' +
      'Sec-WebSocket-Accept: ' + accept + '\r\n\r\n'
    );
    raw.setNoDelay(true);
    const sock = new Socket(raw);
    if (head && head.length) sock._onData(head);
    onConnection(sock, req);
  });
}

module.exports = { attach, Socket };
