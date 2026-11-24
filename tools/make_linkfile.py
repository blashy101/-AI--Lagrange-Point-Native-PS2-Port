"""Keep the dispatch loop and hot handlers together in the SDK text segment."""
import argparse
from pathlib import Path

def build(source, destination):
    text=source.read_text()
    marker='\t\t*(.text.*)\n'
    if text.count(marker)!=1:
        raise ValueError('Expected one text-section wildcard in the PS2SDK linker script.')
    replacement='''\t\t. = ALIGN(64);
        __lp_hot_start = .;
        *(.text.hot .text.hot.*)
        __lp_hot_end = .;
'''+marker
    destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_text(text.replace(marker,replacement))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path);parser.add_argument('destination',type=Path)
    args=parser.parse_args();build(args.source,args.destination)
