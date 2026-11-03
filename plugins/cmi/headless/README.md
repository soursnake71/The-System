# Omega CMI headless

The Omega CMI engine (Fairlight CMI Series II channel card model, Page R, the disk and voice-file reader) as a plain
Linux program: ALSA audio out, ALSA MIDI in, no screen. For a sampler running its own Linux (an MPC with a custom OS,
a Raspberry Pi, anything with ALSA).

## Get it
- Prebuilt: the `omega-cmi` release has `cmi-headless-linux-aarch64.tar.gz` (64-bit ARM), `-armhf` (32-bit ARM) and
  `-x86_64`. Check the device with `uname -m` (aarch64, armv7l, x86_64).
- Or build it on the device: copy `plugins/cmi/core` and `plugins/cmi/headless` (the source tarball in the release has
  both), then `cd headless && make`. Needs a C++17 compiler and the ALSA headers (libasound2-dev / alsa-lib-devel).

## Run
    ./cmi-headless /sdcard/CMI/STRINGS4.IMD /sdcard/CMI/CHORAL.img     # load disks, play from MIDI
    ./cmi-headless -d hw:0,0 -p 64 -n 2 /sdcard/CMI                     # a whole folder, lower latency
    ./cmi-headless -l /sdcard/CMI                                       # list the voices and their program numbers

- MIDI songs (.mid) given on the command line play on the CMI voices: `PLAY` / `STOP` (or MIDI start / stop), `SONGS`, `SELECT <n>` (0 = Page R patterns), `LOOP ON|OFF`.
- Disks (.IMD, .IMG / .BIN), .VC voice files and WAVs load; every voice on a disk goes in. The voices you load and
  Page R are kept in `~/.omega-cmi.state` (`-s` to move it), so the next start has them.
- MIDI: every hardware MIDI input is connected (or `-m client:port`). Notes, velocity, pitch bend, mod wheel,
  sustain; program change picks a voice (`-l` shows the numbers); CC 7 level, 74 filter, 73 attack, 72 damping.
  MIDI start / stop / clock run Page R at the MPC's tempo. `-t`: channels 2-9 play Page R tracks 1-8.
- Commands on stdin: `LOAD <path>`, `VOICE <number or name>`, `PLAY`, `STOP`, `SPEED <bpm>`, `PATTERN <n>`,
  `SONG ON|OFF`, `LIST`, `SAVE`, `QUIT`.
- It asks for real-time priority (SCHED_FIFO); run it as a user allowed to, or as root, for the lowest latency.

## Start it with the system (systemd)
    [Unit]
    Description=Omega CMI
    After=sound.target
    [Service]
    ExecStart=/usr/local/bin/cmi-headless -d hw:0,0 /data/CMI
    Restart=always
    LimitRTPRIO=95
    [Install]
    WantedBy=multi-user.target
