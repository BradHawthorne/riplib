Compatibility fixture corpus for `test_compat`.

Each fixture is a raw `.rip` byte stream paired with a `.expect` file:

- `frame_hash=0x...` is the FNV-1a 64-bit hash of the final 640x400 framebuffer.
- `tx_hex=...` is the exact outbound host byte stream in hex.

Add real historical RIP files and captured BBS exchanges here as they are
collected. The harness replays the raw stream through `rip_process()` so it
acts as a compatibility guardrail for parser and renderer refactors.
