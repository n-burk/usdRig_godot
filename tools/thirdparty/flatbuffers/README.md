# FlatBuffers Python runtime

The pure-Python runtime of [Google FlatBuffers](https://github.com/google/flatbuffers)
release **25.12.19**: the `python/flatbuffers` package of tag `v25.12.19`
(commit `7e163021e59cca4f8e1e35a7c828b5c6b7915953`), copied **unmodified**
from the PyPI wheel `flatbuffers==25.12.19`, whose files equal the tag's.
The tag's generated `reflection` subpackage is not included; nothing here
uses it.

`LICENSE` is the project's Apache License 2.0, unmodified. FlatBuffers is not
relicensed under this repository's MIT license.

## What uses it

`tools/export_ball_assets.py` builds the rolling ball's presentation buffer
(`presentation.rexp`) with the code `tools/gen_presentation.py` generates into
`tools/generated` with the `flatc` 25.12.19 release binary. The exporter puts
`python/` first on `sys.path`, ahead of any installed copy, and asserts the
version, so the generated code always runs against the runtime of its own
release. The exporter packs its numeric tables with `CreateNumpyVector`, so
it also needs numpy.
