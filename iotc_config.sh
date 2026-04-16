#!/bin/bash
# iotc_config.sh — Configure IoTConnect device credentials and build options
#
# Usage:
#   1. Download your device config JSON from the IoTConnect portal.
#      It will be saved to ~/Downloads as iotcDeviceConfig.json (or
#      iotcDeviceConfig (N).json if you have downloaded it before).
#   2. Download your device certificates zip from the IoTConnect portal
#      and copy it into the repository root.
#   3. Run: ./iotc_config.sh [-codec h.264|h.265]
#
# Flags:
#   -codec h.264   Use H.264 video codec (default — widest compatibility)
#   -codec h.265   Use H.265/HEVC video codec (lower bandwidth, ~462 Kbps vs ~1.4 Mbps)
#
# The script will:
#   - Set the video codec in project/webrtc_demo.cmake
#   - Find the most recently downloaded iotcDeviceConfig*.json in ~/Downloads
#   - Write CPID, ENV, and Device UID into iotconnect_config.h
#   - Extract the certificate and private key from the certificates zip
#   - Write them into iotconnect_certs.c

set -e

# --- Parse arguments -------------------------------------------------------

CODEC="h264"
while [[ $# -gt 0 ]]; do
    case "$1" in
        -codec)
            shift
            case "${1,,}" in
                h.264|h264) CODEC="h264" ;;
                h.265|h265) CODEC="h265" ;;
                *) echo "Error: unknown codec '$1'. Use h.264 or h.265."; exit 1 ;;
            esac
            ;;
        *)
            echo "Unknown argument: $1"
            echo "Usage: $0 [-codec h.264|h.265]"
            exit 1
            ;;
    esac
    shift
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG_H="${SCRIPT_DIR}/project/realtek_amebapro2_webrtc_application/src/iotconnect/iotconnect_config.h"
CERTS_C="${SCRIPT_DIR}/project/realtek_amebapro2_webrtc_application/src/iotconnect/iotconnect_certs.c"
DOWNLOADS_DIR="${HOME}/Downloads"

python3 - "$DOWNLOADS_DIR" "$SCRIPT_DIR" "$CONFIG_H" "$CERTS_C" "$CODEC" <<'PYEOF'
import sys
import os
import re
import glob
import json
import zipfile

downloads_dir, script_dir, config_h_path, certs_c_path, codec = sys.argv[1:]

# ── 0. Apply codec selection in project/webrtc_demo.cmake ────────────────────

cmake_path = os.path.join(script_dir, 'project', 'webrtc_demo.cmake')

if not os.path.isfile(cmake_path):
    sys.exit('Error: webrtc_demo.cmake not found: ' + cmake_path)

with open(cmake_path, 'r') as f:
    cmake_src = f.read()

cmake_src, n = re.subn(
    r'(set\(\s*IOTC_VIDEO_CODEC\s+")[^"]*(")',
    r'\g<1>' + codec + r'\g<2>',
    cmake_src
)
if n == 0:
    sys.exit('Error: IOTC_VIDEO_CODEC setting not found in ' + cmake_path)

with open(cmake_path, 'w') as f:
    f.write(cmake_src)

print('Video codec   : ' + ('H.265 (HEVC) — ~462 Kbps' if codec == 'h265' else 'H.264 — ~1.4 Mbps'))

# ── 1. Find the most recently downloaded iotcDeviceConfig*.json ──────────────

json_matches = glob.glob(os.path.join(downloads_dir, 'iotcDeviceConfig*.json'))

if not json_matches:
    sys.exit(
        'Error: no iotcDeviceConfig*.json found in ' + downloads_dir + '\n'
        'Download your device config JSON from the IoTConnect portal and '
        'ensure it lands in your Downloads folder.'
    )

def download_index(path):
    """Return the numeric suffix from 'iotcDeviceConfig (N).json', or -1 for
    the plain 'iotcDeviceConfig.json' (which is the first/oldest download)."""
    m = re.search(r'\((\d+)\)', os.path.basename(path))
    return int(m.group(1)) if m else -1

json_matches.sort(key=download_index, reverse=True)
chosen_json = json_matches[0]
print('Device config : ' + os.path.basename(chosen_json))

if len(json_matches) > 1:
    print('  (multiple iotcDeviceConfig files found — using the most recent)')

# ── 2. Parse CPID, ENV, and UID from the JSON ────────────────────────────────

with open(chosen_json, 'r') as f:
    device_cfg = json.load(f)

for field in ('cpid', 'env', 'uid'):
    if field not in device_cfg:
        sys.exit('Error: field "' + field + '" not found in ' + chosen_json)

cpid = device_cfg['cpid']
env  = device_cfg['env']
uid  = device_cfg['uid']

print('  CPID : ' + cpid)
print('  ENV  : ' + env)
print('  UID  : ' + uid)

# ── 3. Write CPID, ENV, UID into iotconnect_config.h ─────────────────────────

if not os.path.isfile(config_h_path):
    sys.exit('Error: config header not found: ' + config_h_path)

with open(config_h_path, 'r') as f:
    src = f.read()

src, n = re.subn(r'(#define\s+IOTCONNECT_CPID\s+)"[^"]*"', r'\g<1>"' + cpid + '"', src)
if n == 0:
    sys.exit('Error: IOTCONNECT_CPID define not found in ' + config_h_path)

src, n = re.subn(r'(#define\s+IOTCONNECT_ENV\s+)"[^"]*"', r'\g<1>"' + env + '"', src)
if n == 0:
    sys.exit('Error: IOTCONNECT_ENV define not found in ' + config_h_path)

src, n = re.subn(r'(#define\s+IOTCONNECT_DUID\s+)"[^"]*"', r'\g<1>"' + uid + '"', src)
if n == 0:
    sys.exit('Error: IOTCONNECT_DUID define not found in ' + config_h_path)

with open(config_h_path, 'w') as f:
    f.write(src)

print('Written to    : ' + config_h_path)

# ── 4. Find the certificates zip (repo root, then ~/Downloads) ───────────────
# The IoTConnect portal names the zip "{uid}-certificates.zip", so search for
# that specific filename first. Fall back to any *certificates*.zip if not found.

target_zip_name = uid + '-certificates.zip'

def find_zip(directory):
    exact = os.path.join(directory, target_zip_name)
    if os.path.isfile(exact):
        return [exact]
    return glob.glob(os.path.join(directory, '*certificates*.zip'))

zip_matches = find_zip(script_dir)
if not zip_matches:
    zip_matches = find_zip(downloads_dir)

if not zip_matches:
    print(
        '\nWarning: no certificates zip found in the repository root or ~/Downloads.\n'
        'Expected: ' + target_zip_name + '\n'
        'Download the certificate package from the IoTConnect portal and re-run\n'
        'this script to install the certs.'
    )
    sys.exit(0)

if len(zip_matches) > 1:
    sys.exit(
        'Error: multiple certificates zip files found:\n  ' +
        '\n  '.join(zip_matches) +
        '\nRemove all but the one for device "' + uid + '".'
    )

zip_path = zip_matches[0]
print('\nCertificates  : ' + os.path.basename(zip_path))

# ── 5. Extract cert and key from the zip ─────────────────────────────────────

cert_pem = None
key_pem  = None

with zipfile.ZipFile(zip_path) as z:
    for name in z.namelist():
        lower = name.lower()
        if lower.startswith('cert_') and lower.endswith('.crt'):
            cert_pem = z.read(name).decode('utf-8')
            print('  Certificate : ' + name)
        elif lower.startswith('pk_') and lower.endswith('.pem'):
            key_pem = z.read(name).decode('utf-8')
            print('  Private key : ' + name)

if cert_pem is None:
    sys.exit('Error: no file matching cert_*.crt found inside ' + zip_path)
if key_pem is None:
    sys.exit('Error: no file matching pk_*.pem found inside ' + zip_path)

# ── 6. Format PEM files as C string literals ─────────────────────────────────

def pem_to_c_string(pem_text):
    lines = pem_text.splitlines()
    while lines and not lines[-1].strip():
        lines.pop()
    return '\n'.join('"' + line + '\\n"' for line in lines)

cert_c = pem_to_c_string(cert_pem)
key_c  = pem_to_c_string(key_pem)

# ── 7. Write cert and key into iotconnect_certs.c ────────────────────────────

CERT_PLACEHOLDER = '"PASTE YOUR FORMATTED CERT HERE"'
KEY_PLACEHOLDER  = '"PASTE YOUR FORMATTED KEY HERE"'

if not os.path.isfile(certs_c_path):
    sys.exit('Error: certs source file not found: ' + certs_c_path)

with open(certs_c_path, 'r') as f:
    certs_src = f.read()

if CERT_PLACEHOLDER not in certs_src and KEY_PLACEHOLDER not in certs_src:
    print(
        '\nNote: certificates are already installed in iotconnect_certs.c.\n'
        'Remove the existing cert/key content and replace with the placeholder\n'
        'strings to reinstall, or edit the file directly.'
    )
    sys.exit(0)

if CERT_PLACEHOLDER not in certs_src:
    sys.exit('Error: certificate placeholder not found in ' + certs_c_path)
if KEY_PLACEHOLDER not in certs_src:
    sys.exit('Error: private key placeholder not found in ' + certs_c_path)

certs_src = certs_src.replace(CERT_PLACEHOLDER, cert_c)
certs_src = certs_src.replace(KEY_PLACEHOLDER,  key_c)

with open(certs_c_path, 'w') as f:
    f.write(certs_src)

print('Written to    : ' + certs_c_path)
PYEOF
