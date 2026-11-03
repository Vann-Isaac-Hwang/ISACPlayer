# NCM Track Number Probe

This standalone command-line utility compares the decrypted track fields in
each NCM file with the metadata returned by ISACPlayer's `NcmReader`. Normal
inspection does not modify the library database or decode the audio payload.

Build from the repository root:

```sh
cmake -S tools/ncm-track-probe -B /tmp/ncm-track-probe-build
cmake --build /tmp/ncm-track-probe-build -j2
```

Inspect one file or recursively inspect a directory:

```sh
/tmp/ncm-track-probe-build/ncm-track-probe "/path/to/track.ncm"
/tmp/ncm-track-probe-build/ncm-track-probe "/path/to/album"
```

To explicitly decode one NCM file to a temporary audio file and inspect its
container tags with `metaflac`:

```sh
/tmp/ncm-track-probe-build/ncm-track-probe \
  --materialize "/path/to/track.ncm" "/tmp/track.flac"
metaflac --show-tag=TRACKNUMBER --show-tag=DISCNUMBER "/tmp/track.flac"
```

For each file, the report includes the raw decrypted NCM metadata JSON, any
recognized track-number keys, and the normalized `NcmReader` result. A positive
raw track value with a zero/different reader result points to NCM parsing.
The reader can also find track and disc numbers inside the decrypted audio
container metadata, which may not be present in the NCM JSON header. If the JSON
header has no track number but the reader reports one, the result may come from
embedded audio metadata or the filename heuristic; this report does not
distinguish those sources. The optional materialize mode decodes the audio to a
temporary file so tools such as `metaflac` can verify its container tags.
