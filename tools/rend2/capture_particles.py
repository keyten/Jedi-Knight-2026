"""Compatibility entry point for the latched particle capture/benchmark matrix.

See capture_smoke_volume.py --help. Variants now run in separate game processes,
because renderer controls are latched rather than live. --sorting, --batching,
--benchmark and the existing installation/binary arguments remain accepted.
"""
from capture_smoke_volume import main

if __name__ == '__main__':
    main()
