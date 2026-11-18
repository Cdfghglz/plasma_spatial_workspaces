#!/bin/sh
# Workaround for iris Gen12 GPU hangs (rcs0, ecode 12:1:0020fffe/84dffffb) - Mesa #11429, drm/i915 #10582
export INTEL_DEBUG=reemit
