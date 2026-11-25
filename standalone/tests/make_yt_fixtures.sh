#!/bin/sh
# Generate the fragmented-MP4 fixtures for tests/test_yt_demux.c.
# Usage: make_yt_fixtures.sh <output-dir>
# Fragmented MP4 (empty moov + moof/mdat pairs) is the shape YouTube
# serves, and the shape the demuxer is exercised against on hardware.
set -e
OUT="${1:-build/ytfx}"
mkdir -p "$OUT"
FRAG="-movflags frag_keyframe+empty_moov+default_base_moof -frag_duration 500000"
# The muxed fixture is a PLAIN MP4 (faststart): that is the shape of
# YouTube's progressive formats (itag 18/22) - one moov, both sample
# tables built at open, audio pumped alongside video. Only the
# adaptive streams (vfrag/afrag below) are fragmented, single-track.
ffmpeg -y -v error -f lavfi -i testsrc=duration=2:size=640x360:rate=30 \
    -f lavfi -i sine=frequency=440:duration=2:sample_rate=44100 \
    -c:v libx264 -pix_fmt yuv420p -g 30 -c:a aac -b:a 96k -movflags +faststart "$OUT/muxfrag.mp4"
ffmpeg -y -v error -f lavfi -i testsrc=duration=2:size=640x360:rate=30 \
    -c:v libx264 -pix_fmt yuv420p -g 30 $FRAG "$OUT/vfrag.mp4"
ffmpeg -y -v error -f lavfi -i sine=frequency=440:duration=2:sample_rate=44100 \
    -c:a aac -b:a 96k $FRAG "$OUT/afrag.m4a"
echo "fixtures in $OUT"
