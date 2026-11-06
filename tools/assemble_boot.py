"""Assemble the installed GLES build using the target device's own private MAC."""
from pathlib import Path
import argparse,hashlib,json,re

def digest(data):return hashlib.sha256(data).hexdigest()
def personalize(template,mac,profile):
    if len(template)!=profile['bytes'] or digest(template)!=profile['template_sha256']:
        raise ValueError('BOOT template does not match the installed GLES profile')
    if not re.fullmatch(rb'(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}\n',mac):
        raise ValueError('MAC input must be the target device original 18-byte mac.info file')
    address=bytes.fromhex(mac.decode().strip().replace(':',''))
    if address[0]&1 or address==bytes(6):raise ValueError('A nonzero unicast MAC is required')
    offset=profile['mac_offset']
    if template[offset:offset+18]!=profile['mac_placeholder'].encode():
        raise ValueError('Template identity slot differs')
    return template[:offset]+mac+template[offset+18:]

def assemble(template,stock,mac,profile):
    prefix=personalize(template,mac,profile)
    if len(stock)!=profile['boot_partition_bytes'] or digest(stock)!=profile['original_boot_sha256']:
        raise ValueError('Stock BOOT does not match the required original firmware')
    tail=stock[len(prefix):]
    if digest(template+tail)!=profile['template_full_boot_sha256']:
        raise ValueError('Assembled template differs from the verified build')
    return prefix+tail

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix-template',required=True,type=Path)
    parser.add_argument('--mac-info',required=True,type=Path)
    parser.add_argument('--stock-boot',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args()
    profile=json.loads((Path(__file__).resolve().parents[1]/'profiles/installed-gles.json').read_text())
    data=assemble(args.prefix_template.read_bytes(),args.stock_boot.read_bytes(),args.mac_info.read_bytes(),profile)
    with args.output.open('xb') as output:output.write(data)
    print('Verified per-device GLES BOOT SHA-256: '+digest(data))
