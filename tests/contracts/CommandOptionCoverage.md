# Public option coverage outside `run` and `build`

This matrix is intentionally empty at the start of the CLI-wide audit. The
gate extracts options from each command's live help and fails with the exact
missing `vix <command> --option` entry. Rows may be `PASS`, `FAIL`,
`UNAVAILABLE`, `UNCOVERED`, or `BLOCKED`; only `PASS` requires a registered
contract name.

| Command | Option | Contract | Class | Status |
| ------- | ------ | -------- | ----- | ------ |
