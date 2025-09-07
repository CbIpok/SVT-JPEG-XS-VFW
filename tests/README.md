# Tests

To run integration tests with large media files, place the following files in `tests/data/override/` or specify their locations via environment variables:

- `input.yuv` (`BUFFER_TEST_INPUT_YUV`)
- `stream.jxs` (`BUFFER_TEST_STREAM_JXS`)
- `enc.cfg` (`BUFFER_TEST_ENC_CFG`)
- `dec.cfg` (`BUFFER_TEST_DEC_CFG`)

When overriding `input.yuv` or `stream.jxs`, the corresponding `enc.cfg` or `dec.cfg` must reside in the same directory.
