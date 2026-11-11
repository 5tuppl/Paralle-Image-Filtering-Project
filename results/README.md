# Measurement artifacts

- `mac-release/`: final release build, 158 configurations, seven samples each, Mac single-machine
  scaling. `environment.json` records binary/kernel hashes and the actual build commands.
- `baseline-mac/`: three launches per configuration of the original blur, before refactoring.
  Its allocation timing differs by mode; use only as contextual before/after evidence.
- `historical-linux/`: unchanged original Linux CSV/SVG/environment artifacts. These are historical
  results for the old implementation, not evidence that the new implementation was tested on Linux.
- `development-mac/`: earlier full run of the same kernels before final I/O/thread-setup hardening.
  Retained to distinguish it from the final binary rather than silently replacing measurements.
- `demo/`: instructions for generating demonstration images. Optional local artwork and
  filtered PNG files are ignored; see its README.

See `../docs/RESULTS.md` for interpretation. CSV values are primary evidence. Plot lines connect
measured worker counts, and time whiskers show minimum/maximum observed samples. The dashed line
is the ideal curve; no ideal scaling is claimed.

The saved Mac metadata uses repository-relative binary paths instead of personal checkout
paths. Measurements, hashes, environment details, and the original dirty-tree records are
preserved. New run directories are ignored by default; intentionally select evidence for
version control rather than committing every local experiment.
