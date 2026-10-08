# Board parity: C data model against the product's shell

On an MTK board running icwmpd with the C data model, the product's easycwmp
function library is still installed (`/usr/share/easycwmp/functions`, driven by
`/usr/share/icwmp/icwmp_dm.sh`).  Both can be asked for the whole tree on the
same board within seconds, and every difference explained.

```sh
# on the board (the dev-access image opens SSH)
sh parity_dump.sh /tmp/icwmp_parity      # ~30 s: C via ubus tr069 dm, shell via icwmp_dm.sh
# images built --disable-dm-script-compat (PH5) have no icwmp_dm.sh: copy
# userspace/public/libs/libicwmp_dm/src/sdk/mtk/compat/icwmp_dm.sh to the board
DM_SH=/tmp/icwmp_dm.sh sh parity_dump.sh /tmp/icwmp_parity
# on the host, after copying /tmp/icwmp_parity back
python3 tests/board/parity.py <dir>      # exit 1 on any unexplained difference
```

`parity.py` sorts each difference into: equal, dynamic (counters and clocks
read seconds apart), known (deliberate, each with the place it is written
down), quote (the shell printed the quotes of an `echo \"...\"` getter) or
UNEXPECTED.  Names present on one side only and GPN writable flags are checked
the same way; types are listed, not judged (analysis section 63).

With the shell bridge compiled in (before PH5) a path the C tree lacks is
answered by the shell in both dumps, so only a compat-off image proves the C
tree complete.  Limits: a counter wrong on the C side hides in the dynamic class (the
LANEthernet `gsw_stats` bug of 0101 was found by asking `.Stats.` directly);
reading the tree runs every getter of both sides, including the shell's GET
side effects (instance numbers committed, as the C does too).
