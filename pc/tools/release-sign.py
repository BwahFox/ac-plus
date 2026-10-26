#!/usr/bin/env python3
"""release-sign.py - sign ac-plus release zips, so the in-game updater only installs releases you made.

    release-sign.py keygen          make the signing key (once): ~/.config/ac-plus/release-signing-key.pem,
                                    and print the public key for pc_updater.c (UPD_SIGNING_KEY)
    release-sign.py sign ZIP        write ZIP.sig: upload it to the GitHub release next to the zip
    release-sign.py verify ZIP      check ZIP.sig against the key, as the game does

The signature is ECDSA P-256 over the zip's SHA-256, stored raw (r then s, 32 bytes each): what Windows' BCrypt
checks (pc_updater.c), so the game needs no crypto code of its own. Needs the `cryptography` package.

Keep the private key safe and backed up somewhere offline. Anyone who has it can ship updates to every player;
without it, no further update can be signed and players have to install the next version by hand.
"""
import hashlib
import os
import sys

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

KEY = os.path.expanduser(os.environ.get('AC_PLUS_SIGNING_KEY', '~/.config/ac-plus/release-signing-key.pem'))


def load_key():
    with open(KEY, 'rb') as f:
        return serialization.load_pem_private_key(f.read(), password=None)


def public_c(pub):
    nums = pub.public_numbers()
    raw = nums.x.to_bytes(32, 'big') + nums.y.to_bytes(32, 'big')
    lines = [', '.join('0x%02x' % b for b in raw[i:i + 16]) for i in range(0, 64, 16)]
    return '/* X then Y */\n    ' + ',\n    '.join(lines) + ','


def digest(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.digest()


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ('keygen', 'sign', 'verify') or (sys.argv[1] != 'keygen' and len(sys.argv) != 3):
        sys.exit(__doc__)
    op = sys.argv[1]
    if op == 'keygen':
        if os.path.exists(KEY):
            sys.exit(f'{KEY} already exists; not replacing it (every release after it would stop verifying)')
        os.makedirs(os.path.dirname(KEY), mode=0o700, exist_ok=True)
        key = ec.generate_private_key(ec.SECP256R1())
        pem = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption())
        fd = os.open(KEY, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, 'wb') as f:
            f.write(pem)
        print(f'private key: {KEY} (keep it secret, back it up offline)')
        print('public key for pc_updater.c:')
        print(public_c(key.public_key()))
        return
    key = load_key()
    path = sys.argv[2]
    if op == 'sign':
        der = key.sign(digest(path), ec.ECDSA(utils.Prehashed(hashes.SHA256())))
        r, s = utils.decode_dss_signature(der)
        with open(path + '.sig', 'wb') as f:
            f.write(r.to_bytes(32, 'big') + s.to_bytes(32, 'big'))
        print('wrote', path + '.sig')
    else:
        raw = open(path + '.sig', 'rb').read()
        if len(raw) != 64:
            sys.exit('bad signature file')
        der = utils.encode_dss_signature(int.from_bytes(raw[:32], 'big'), int.from_bytes(raw[32:], 'big'))
        key.public_key().verify(der, digest(path), ec.ECDSA(utils.Prehashed(hashes.SHA256())))
        print('good signature')


if __name__ == '__main__':
    main()
