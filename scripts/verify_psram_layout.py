"""Reject PSRAM images that violate the DISPLAY loader startup ABI."""

from __future__ import annotations

import pathlib
import re
import struct
import subprocess
import sys

PSRAM_START = 0x11000000
PSRAM_END = 0x11800000
SRAM_START = 0x20000000
SRAM_END = 0x20070000


def in_psram(address: int) -> bool:
    return PSRAM_START <= address < PSRAM_END


def in_sram(address: int) -> bool:
    return SRAM_START <= address <= SRAM_END


def symbols(elf: str, objdump: str) -> dict[str, int]:
    output = subprocess.check_output([objdump, "-t", elf], text=True)
    result: dict[str, int] = {}
    for line in output.splitlines():
        match = re.match(r"^([0-9a-fA-F]+)\s+.*\s(\S+)$", line)
        if match:
            result[match.group(2)] = int(match.group(1), 16)
    return result


def require_region(found: dict[str, int], name: str, predicate, region: str) -> None:
    address = found.get(name)
    if address is None:
        raise SystemExit(f"layout check: missing symbol {name}")
    if not predicate(address):
        raise SystemExit(f"layout check: {name}=0x{address:08x} is not in {region}")


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit("usage: verify_psram_layout.py APP.elf OBJDUMP APP.uf2")
    found = symbols(sys.argv[1], sys.argv[2])
    for name in ("__vectors", "_entry_point", "main"):
        require_region(found, name, in_psram, "PSRAM")
    for name in ("fw2_psram_bootstrap", "board_init_psram", "set_sys_clock_pll",
                 "psram_reinitialize", "__StackTop"):
        require_region(found, name, in_sram, "SRAM")
    if found["__vectors"] != PSRAM_START:
        raise SystemExit("layout check: vector table is not first in PSRAM")

    image = pathlib.Path(sys.argv[3]).read_bytes()
    if not image or len(image) % 512:
        raise SystemExit("layout check: malformed UF2 length")
    first_payload = None
    for offset in range(0, len(image), 512):
        block = image[offset:offset + 512]
        magic0, magic1, _flags, address, size = struct.unpack_from("<5I", block)
        magic_end = struct.unpack_from("<I", block, 508)[0]
        if (magic0, magic1, magic_end) != (0x0A324655, 0x9E5D5157, 0x0AB16F30):
            raise SystemExit("layout check: malformed UF2 magic")
        if not 0 < size <= 476 or not in_psram(address) or not in_psram(address + size - 1):
            raise SystemExit(f"layout check: non-PSRAM payload at 0x{address:08x}")
        if address == PSRAM_START:
            first_payload = block[32:32 + size]
    if first_payload is None or len(first_payload) < 8:
        raise SystemExit("layout check: missing vector-table payload")
    initial_sp, reset = struct.unpack_from("<2I", first_payload)
    if not in_sram(initial_sp):
        raise SystemExit(f"layout check: initial SP 0x{initial_sp:08x} is not SRAM")
    if not reset & 1 or not in_psram(reset & ~1):
        raise SystemExit(f"layout check: reset vector 0x{reset:08x} is not Thumb PSRAM")
    print("layout check: PSRAM app + SRAM bootstrap + PSRAM-only UF2")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
