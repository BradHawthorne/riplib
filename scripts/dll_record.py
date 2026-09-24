"""Literal prefix in a 40-byte RIP driver dispatch record (D-34)."""
def dispatch_level(raw):
    if len(raw) != 40:
        raise ValueError("incomplete dispatch record")
    if b"\0" not in raw[5:15]:
        raise ValueError("unterminated dispatch prefix")
    prefix = raw[5:15].split(b"\0", 1)[0]
    if not prefix:
        return 0
    if not prefix.isdigit():
        raise ValueError("invalid dispatch prefix: %r" % prefix)
    return int(prefix)
