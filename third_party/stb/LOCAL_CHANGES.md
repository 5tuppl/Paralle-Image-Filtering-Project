# Local changes to the pinned upstream revision

`stb_image_write.h`: the JPEG writer bit accumulator and associated pointer parameters
use `unsigned int`, including the pre-shift cast. This removes signed-left-shift undefined
behavior found by UBSan while generating the JPEG test fixture. No other upstream code
is intentionally changed. Production output remains PNG/PPM; JPEG output is test-only.

The upstream revision and license are recorded in `REVISION` and `LICENSE`.
