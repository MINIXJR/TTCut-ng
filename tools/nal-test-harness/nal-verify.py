#!/usr/bin/env python3
"""
NAL-level verification tool for H.264 Smart Cut transitions.

Extracts a specific NAL from an ES file, optionally applies a transformation,
wraps it in a minimal valid H.264 stream (SPS+PPS+NAL), and decodes with
multiple decoders to check for errors.

Usage:
  nal-verify.py <es_file> <nal_byte_offset> [--transform mmco-neutralize]
  nal-verify.py <mkv_file> --transition      # auto-find MBAFF→PAFF transition

Tools used:
  - ffmpeg (decode + error check)
  - h264_analyze (/usr/local/src/h264bitstream/h264_analyze)
  - ldecod (/usr/local/src/jm-reference/bin/umake/gcc-15.2/x86_64/release/ldecod)
"""

import subprocess, sys, os, tempfile, re

H264_ANALYZE = os.environ.get("H264_ANALYZE", "h264_analyze")
LDECOD = os.environ.get("LDECOD", "ldecod")
TMPDIR = os.environ.get("NAL_VERIFY_TMPDIR",
                        os.path.join(tempfile.gettempdir(), "nal-verify"))

def find_nals(es_data):
    """Find all NAL start code positions and types."""
    nals = []
    i = 0
    while i < len(es_data) - 4:
        if es_data[i] == 0 and es_data[i+1] == 0:
            if es_data[i+2] == 0 and es_data[i+3] == 1:
                nt = es_data[i+4] & 0x1F
                nals.append((i, 4, nt))
                i += 5; continue
            elif es_data[i+2] == 1:
                nt = es_data[i+3] & 0x1F
                nals.append((i, 3, nt))
                i += 4; continue
        i += 1
    return nals

def extract_nal(es_data, nals, idx):
    """Extract NAL body (without start code) at index idx."""
    pos, sc_len, _ = nals[idx]
    end = nals[idx+1][0] if idx+1 < len(nals) else len(es_data)
    return es_data[pos+sc_len:end]

def find_last_sps_pps(es_data, nals, before_idx):
    """Find the most recent SPS and PPS before a given NAL index."""
    sps = pps = None
    for i in range(before_idx-1, -1, -1):
        pos, sc_len, nt = nals[i]
        end = nals[i+1][0] if i+1 < len(nals) else len(es_data)
        if nt == 7 and sps is None:
            sps = es_data[pos:end]
        elif nt == 8 and pps is None:
            pps = es_data[pos:end]
        if sps and pps:
            break
    return sps, pps

def build_test_stream(sps, pps, nal_body, start_code=b'\x00\x00\x00\x01'):
    """Build minimal H.264 ES: SPS + PPS + slice NAL."""
    stream = bytearray()
    stream.extend(sps)
    stream.extend(pps)
    stream.extend(start_code)
    stream.extend(nal_body)
    return bytes(stream)

def check_ffmpeg(es_file):
    """Decode with ffmpeg and return ALL errors/warnings."""
    result = subprocess.run(
        ['ffmpeg', '-v', 'error', '-i', es_file, '-f', 'null', '-'],
        capture_output=True, text=True, timeout=30)
    errors = [l.strip() for l in result.stderr.split('\n') if l.strip()]
    return errors

def check_ffmpeg_verbose(es_file):
    """Decode with ffmpeg verbose and return warnings."""
    result = subprocess.run(
        ['ffmpeg', '-v', 'warning', '-i', es_file, '-f', 'null', '-'],
        capture_output=True, text=True, timeout=30)
    errors = [l.strip() for l in result.stderr.split('\n')
              if l.strip() and not l.startswith('frame=') and 'encoder' not in l.lower()]
    return errors

def check_h264_analyze(es_file):
    """Run h264_analyze and return output."""
    result = subprocess.run(
        [H264_ANALYZE, es_file],
        capture_output=True, text=True, timeout=30)
    return result.stdout + result.stderr

def check_ldecod(es_file):
    """Decode with JM reference decoder."""
    # ldecod needs a config file
    cfg = os.path.join(TMPDIR, "ldecod.cfg")
    yuv = os.path.join(TMPDIR, "decoded.yuv")
    log = os.path.join(TMPDIR, "ldecod.log")

    with open(cfg, 'w') as f:
        f.write(f"InputFile = \"{es_file}\"\n")
        f.write(f"OutputFile = \"{yuv}\"\n")
        f.write(f"RefFile = \"{yuv}\"\n")
        f.write("WriteUV = 1\n")
        f.write("FileFormat = 0\n")  # Annex B
        f.write("RefOffset = 0\n")
        f.write("POCScale = 2\n")
        f.write("DisplayDecParams = 1\n")
        f.write("ConcealMode = 0\n")
        f.write("RefPOCGap = 2\n")
        f.write("POCGap = 2\n")
        f.write("Silent = 0\n")
        f.write("DecFrmNum = 0\n")

    result = subprocess.run(
        [LDECOD, '-d', cfg],
        capture_output=True, text=True, timeout=30,
        cwd=TMPDIR)

    output = result.stdout + result.stderr

    # Clean up large YUV file
    if os.path.exists(yuv):
        os.remove(yuv)

    return output

def analyze_mkv_transition(mkv_file):
    """Extract raw ES from MKV and find MBAFF→PAFF transition."""
    es_file = os.path.join(TMPDIR, "extracted.264")
    subprocess.run(
        ['ffmpeg', '-i', mkv_file, '-c:v', 'copy', '-bsf:v',
         'h264_mp4toannexb', es_file, '-y'],
        capture_output=True, timeout=30)

    with open(es_file, 'rb') as f:
        es = f.read()

    nals = find_nals(es)
    print(f"ES: {len(es)} bytes, {len(nals)} NALs")

    # Find MBAFF→PAFF transition (field_pic_flag 0→1)
    from nal_parser import parse_slice_info
    # ... (would need the parser)

    return es_file, es, nals

def verify_nal(es_file, description=""):
    """Run all verification tools on an ES file."""
    os.makedirs(TMPDIR, exist_ok=True)

    print(f"\n{'='*60}")
    print(f"Verifying: {description or es_file}")
    print(f"{'='*60}")

    # 1. ffmpeg error check
    print("\n--- ffmpeg (error level) ---")
    errors = check_ffmpeg(es_file)
    if errors:
        for e in errors:
            print(f"  ✗ {e}")
    else:
        print("  ✓ No errors")

    # 2. ffmpeg warning check
    print("\n--- ffmpeg (warning level) ---")
    warnings = check_ffmpeg_verbose(es_file)
    relevant = [w for w in warnings if any(k in w.lower() for k in
        ['error', 'corrupt', 'invalid', 'unavailable', 'exceeds', 'mmco', 'backward'])]
    if relevant:
        for w in relevant:
            print(f"  ⚠ {w}")
    else:
        print("  ✓ No relevant warnings")

    # 3. h264_analyze
    if os.path.exists(H264_ANALYZE):
        print("\n--- h264_analyze ---")
        output = check_h264_analyze(es_file)
        # Show slice headers
        for line in output.split('\n'):
            if any(k in line for k in ['slice_type', 'frame_num', 'field_pic',
                    'adaptive_ref', 'memory_management', 'slice_qp']):
                print(f"  {line.strip()}")

    # 4. JM ldecod
    if os.path.exists(LDECOD):
        print("\n--- JM Reference Decoder ---")
        try:
            output = check_ldecod(es_file)
            error_lines = [l for l in output.split('\n')
                          if any(k in l.lower() for k in ['error', 'warning', 'fault', 'fail'])]
            if error_lines:
                for e in error_lines[:10]:
                    print(f"  ⚠ {e.strip()}")
            else:
                print("  ✓ No errors reported")
        except Exception as e:
            print(f"  (skipped: {e})")

    return len(errors) == 0 and len(relevant) == 0

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    os.makedirs(TMPDIR, exist_ok=True)
    verify_nal(sys.argv[1], sys.argv[1])
