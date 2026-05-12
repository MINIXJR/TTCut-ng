#!/usr/bin/env python3
"""
inject_fieldpictures.py — convert selected MPEG-2 frame_picture frames
into top/bottom field-picture pairs at the bitstream level.

Used for generating deterministic test data for TTCut-ng's MPEG-2
field-picture detection. The resulting stream decodes to visually broken
output (the same payload is reused for both fields) but is bitstream-valid
and exercises the picture_coding_extension/picture_structure parser.

Usage:
  inject_fieldpictures.py SOURCE.m2v OUTPUT.m2v --every N

Behaviour:
  - Reads SOURCE.m2v byte-for-byte.
  - Walks picture_start_codes (0x00 0x00 0x01 0x00).
  - For every Nth picture (1-indexed), duplicates the entire picture
    block and patches picture_structure in the picture_coding_extension
    of each copy: first copy=1 (top), second copy=2 (bottom).
  - Pictures not selected are written through unchanged.
  - Writes the modified stream to OUTPUT.m2v.

Notes:
  - picture_coding_extension is identified by extension_start_code
    (0x00 0x00 0x01 0xB5) followed by a byte whose upper nibble is 0x8.
  - picture_structure occupies bits 1-0 of the third byte after the
    start_code_identifier (i.e. data[2] in the parseExtensionData domain).
"""

import argparse
import sys

PICTURE_START   = b'\x00\x00\x01\x00'
EXTENSION_START = b'\x00\x00\x01\xB5'

def find_next_start_code(data: bytes, pos: int) -> int:
    """Return index of next start code (0x00 0x00 0x01 0xXX) at or after pos.
    Returns len(data) if none found."""
    n = len(data)
    while pos < n - 3:
        if data[pos] == 0 and data[pos+1] == 0 and data[pos+2] == 1:
            return pos
        pos += 1
    return n

def find_picture_coding_ext(data: bytes, pic_start: int, search_limit: int = 2048) -> int:
    """Given the byte position of a picture_start_code (0x00 0x00 0x01 0x00),
    return the byte position of the picture_coding_extension byte 0
    (the first byte after the 0xB5 identifier). Returns -1 if not found
    within search_limit bytes."""
    n = len(data)
    end = min(pic_start + search_limit, n - 4)
    j = pic_start + 4
    while j < end:
        if data[j] == 0 and data[j+1] == 0 and data[j+2] == 1 and data[j+3] == 0xB5:
            # Next byte high nibble must be 0x8 for picture_coding_extension.
            if (data[j+4] & 0xF0) == 0x80:
                return j + 4  # byte 0 of extension data
        j += 1
    return -1

def patch_picture_structure(picture_block: bytearray, ext_offset_in_block: int, new_value: int):
    """Rewrite picture_structure bits in the given picture block.
    ext_offset_in_block is the byte index within picture_block where
    picture_coding_extension byte 0 starts. picture_structure is at
    byte 2 (bits 1-0)."""
    idx = ext_offset_in_block + 2
    if idx >= len(picture_block):
        raise ValueError("ext_offset_in_block out of range")
    picture_block[idx] = (picture_block[idx] & 0xFC) | (new_value & 0x03)

def main():
    p = argparse.ArgumentParser(description="Inject field-picture pairs into MPEG-2 elementary stream")
    p.add_argument("source", help="Input .m2v file")
    p.add_argument("output", help="Output .m2v file")
    p.add_argument("--every", type=int, default=50,
                   help="Convert every Nth picture into a field-pair (default: 50)")
    args = p.parse_args()

    with open(args.source, "rb") as f:
        data = f.read()
    n = len(data)

    out = bytearray()
    pic_idx = 0
    converted = 0

    # Locate the first picture_start_code
    pos = 0
    while pos < n - 3:
        if data[pos:pos+4] == PICTURE_START:
            break
        pos += 1

    # Emit everything before the first picture as-is
    out.extend(data[:pos])

    while pos < n:
        # End of current picture block = position of next start_code that is
        # either picture_start, group_start, sequence_start or sequence_end.
        # Anything else (extensions, user_data, slices) belongs to this picture.
        scan = pos + 4
        block_end = n
        while scan < n - 3:
            if data[scan] == 0 and data[scan+1] == 0 and data[scan+2] == 1:
                code = data[scan+3]
                if code in (0x00, 0xB3, 0xB7, 0xB8):  # picture, sequence, end, GOP
                    block_end = scan
                    break
            scan += 1

        block = bytearray(data[pos:block_end])

        # Find picture_coding_extension within this block
        ext_pos_abs = find_picture_coding_ext(data, pos)
        if ext_pos_abs >= 0 and ext_pos_abs < block_end:
            ext_offset_in_block = ext_pos_abs - pos
        else:
            ext_offset_in_block = -1

        pic_idx += 1
        convert = (pic_idx % args.every == 0) and (ext_offset_in_block >= 0)

        if convert:
            top_copy    = bytearray(block)
            bottom_copy = bytearray(block)
            patch_picture_structure(top_copy,    ext_offset_in_block, 1)
            patch_picture_structure(bottom_copy, ext_offset_in_block, 2)
            out.extend(top_copy)
            out.extend(bottom_copy)
            converted += 1
        else:
            out.extend(block)

        # Skip past this picture's payload
        pos = block_end
        # If next start_code is not a picture, copy non-picture data through
        # and advance until the next picture_start
        while pos < n - 3:
            if data[pos:pos+4] == PICTURE_START:
                break
            # Copy this byte through
            out.append(data[pos])
            pos += 1

    with open(args.output, "wb") as f:
        f.write(out)

    print(f"Source pictures:     {pic_idx}")
    print(f"Converted to field-pair: {converted}")
    print(f"Output picture_start_codes: {pic_idx + converted}")
    print(f"Output size: {len(out)} bytes (source: {n})")

if __name__ == "__main__":
    main()
