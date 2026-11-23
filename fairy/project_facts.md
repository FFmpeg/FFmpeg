<!-- This file is passed to all stages: triage, draft reviews, combining, vetting and issue investigation. Use it only for facts that are true for all of them, not for instructions specific to a subset. -->
<!-- The file is always read from the default branch, not from the pull request head: this avoids prompt injection and applies the latest facts to old branches too. -->
##FFmpeg project facts:

Implementation definitions:
* `int` is 32 bits wide, and a byte is 8 bits.
* two’s complement
* Signed >> acts on negative numbers by sign extension

The FFmpeg test suite is called FATE. Its tests must be portable.
Each new codec, parser, filter and format should have a fate test, when possible.
Multimedia files used in fate tests are stored in fate-suite.ffmpeg.org.
To upload a new file to the fate samples, the pull request author has to send a mail to samples-request at ffmpeg dot org. (you can tell this email address and procedure when it seems that this is not understood)
FATE samples that are 10kb (100kb for video) or less do not need to be trimmed. samples that are over 1mb should be trimmed if possible (sometimes its not possible and that's ok)
API and FATE tests should not hard-code expected values in source code. Expected output belongs in tests/ref/*, with the test printing actual results and FATE comparing them against the reference files. It is ok to also print the expected result as part of printing the current value when the expected is very stable. Tests that compare to hardcoded values and fail directly should not be approved, even when hidden behind helper macros such as CHECK(). Exception is CMP = grep
side data attached by ffmpeg code complies with the documented constraints of its type. Producers must ensure this; consumers may assume it.
AVCodecContext.get_buffer2() buffers need to respect avcodec_align_dimensions2(). Decoders and Encoders may assume this additional padding has been allocated.
Big-endian AArch64 is not supported.
Its best to upload missing fate samples / contact samples-request when a PR is mature, approved or close to being approved. Uploading them earlier risks accumulating stale files.
If the needed fate samples have been attached to the PR, then their absence from the public repository is no reason to block the PR.
