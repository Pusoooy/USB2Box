"""Generate a C table of fixed DMA routes from the F401RC schema."""
import csv
from pathlib import Path
import sys

with Path(sys.argv[1]).open(newline="") as source:
    rows = list(csv.DictReader(source))
with Path(sys.argv[2]).open("w") as target:
    for row in rows:
        dma = int(row["dma"])
        stream = int(row["stream"])
        for channel in range(8):
            for request in filter(None, row[f"ch{channel}"].split("|")):
                print(f'    {{"{request}", {dma}, {stream}, {channel}}},', file=target)
