Status: done

# Audio engine core

- [x] AudioDeviceManager setup (CoreAudio), settings dialog
- [x] Engine callback: RT-safe, no locks/allocs on audio thread
- [x] Transport: BPM, PPQ position, play/stop/loop, pattern vs song mode
- [x] Lock-free UI<->audio messaging (FIFOs)
- [x] Master output + peak metering
