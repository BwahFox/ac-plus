# ac-plus's changes to Nookery's submodules

These are applied to `extern/aurora` when CMake configures (see `CMakeLists.txt`). Each is applied once; a patch
already in the submodule is left alone. If Nookery updates Aurora and a patch no longer applies, configuring stops
with a message: update the patch against the new Aurora (`git -C extern/aurora diff > patches/<name>.patch`).

- `aurora-curved-world.patch`: `GXAuroraSetCurve`, the City Folk style curved world. A GX command that sets two
  vectors; the generated vertex shader lowers each point after the position matrix by `k * u^2`, `u` its distance
  ahead of the pivot. Off (all zeros) unless ac-plus sets it, and never for orthographic (2D) drawing.
