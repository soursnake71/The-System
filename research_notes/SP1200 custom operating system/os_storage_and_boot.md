# E-mu SP-1200: Where the Operating System Is Stored and How It Boots

Bottom line, up front: the evidence I found supports the user. The original E-mu SP-1200 (1987) and the 1990s black units do not keep their operating system in an OS EPROM. They load the OS from the inserted 3.5" floppy at power-up, and formatting a disk on the unit can write a copy of the OS onto it ("Copy Software"). The OS EPROM belongs to the earlier **SP-12** (1985), which is probably where the "it runs from ROM" belief comes from. The 2021 Rossum reissue is the exception: its firmware lives in the unit and is updated from an SD card file (`sp1200fw.bin`).

Caveat on sources: nearly all of the explicit "OS loads from disk" statements come from user forums (Gearspace, HxC forum, dxarmy/tapatalk SP1200 forum). They agree with each other and with the owner's manual's table of contents ("Formatting Disks and Copying Software"), but I could not fetch the owner's or service manual text to quote E-mu verbatim. The autistici.org PDF host was unreachable, and the archive.org service-manual OCR contains only watermark text.

## 1. Which chips hold the firmware, and how did E-mu distribute OS versions?

### Takeaway
The SP-1200 OS is distributed on floppy, not as an EPROM set. OS "versions" were passed around as boot disks, and any disk formatted with "Copy Software" becomes an OS disk. EPROM-swap upgrades (e.g. OS 2.4 to 2.6 using "2 OS ROM chips") apply to the **SP-12**, not the SP-1200. I found no primary source giving part numbers or a board location for a small boot/loader ROM, although one must logically exist.

### Cited Findings
- A Gearspace poster states: "The SP 1200 has no OS (Operating System) Rom or EProm built in (as the SP-12 does). It always LOADS the operating system from the inserted floppy disk." — [Gearspace: sp1200 drum sounds on os disk](https://gearspace.com/threads/sp1200-drum-sounds-on-os-disk.950167/) (seen via search snippet; the page returned 403 to direct fetch)
- SP-12 owners discuss OS versions 1.6, 1.8, 2.1, 2.4, 2.5 and 2.6. Upgrading 2.4 to 2.6 is described as "installing the 2 OS ROM chips" (an EPROM swap). This concerns the SP-12, not the SP-1200. — [Gearspace: emu sp12 operating system](https://gearspace.com/threads/emu-sp12-operating-system.659017/); [Tapatalk SP1200 forum: sp-12 os upgrade](https://www.tapatalk.com/groups/sp1200/sp-12-os-upgrade-t1187.html) (search snippets; direct fetch 403)
- HxC forum users (2011–2013) looked specifically for an SP-1200 "system disk image (OS 1.2 if possible)", which shows that SP-1200 OS versions circulate as disk images. — [HxC forum: EMU SP1200](https://hxc2001.com/floppy/forum/viewtopic.php?t=527)
- A forum post says "the OS is contained in the first 5 or 6 tracks of all SP disks." — [Tapatalk SP1200 forum: Hacking the SP's Operating System](https://www.tapatalk.com/groups/sp1200/viewtopic.php?p=12441) (search snippet only; direct fetch 403, so unverified)
- "SP1200 OS (Bootup) Disk" is sold as a distinct item on eBay. — [eBay listing](https://www.ebay.com/itm/151894073800)
- The SP-1200 is a Z80-based design: Wikipedia says E-mu used the Z80 "through SP1200 in 1987". — [Wikipedia: E-mu SP-1200](https://en.wikipedia.org/wiki/E-mu_SP-1200)
- A community GitHub repo with SP-1200 schematics and BOM notes that the "Control Sequencer is IC67 74S288" (a bipolar PROM) and that much logic is held in programmed (EE)PROMs. It does not identify an OS EPROM. — [GitHub Lytrix/EMU-SP1200](https://github.com/Lytrix/EMU-SP1200) (search summary; repo contents not inspected)
- The service manual (Rev 1, 1987) is on archive.org and includes a "ROM Tests" section per ManualsLib's table of contents. — [archive.org service manual](https://archive.org/details/emu-sp-1200-service-manual-1987_202010); [ManualsLib](https://www.manualslib.com/manual/827730/E-Mu-Sp1200.html)

### Inferences
- A Z80 machine cannot read a floppy without some resident code. The SP-1200 almost certainly has a small boot/loader ROM: it initializes hardware, shows the power-up message (e.g. "Rev B" on one black unit), and reads the OS from disk into RAM. The forum claim of "no OS ROM" should be read as "no ROM holding the full operating system," not "no ROM at all." The service manual's "ROM Tests" section and schematics would confirm the chip. I did not verify this.
- So "firmware versions" for the SP-1200 mostly means "versions of the disk-resident OS". It does not mean EPROM revisions.

### Gaps
- No primary source found for the boot-ROM part number, capacity or board location. The service manual PDF would need page-image review, because the OCR text is unusable.
- No authoritative list of SP-1200 OS versions or release notes. Forum mentions include "OS 1.2" and a power-up "Rev B" display on a black unit, which may be a hardware rather than OS revision. I found no E-mu release notes.

## 2. Does any SP-1200 version load its OS from the floppy? Were there OS disks? What about the Rossum reissue?

### Takeaway
Yes. The original 1987 SP-1200 and the 1990s black reissue both boot their OS from floppy. Formatting a disk on the unit offers "copy software? Y/N", and a disk becomes bootable only if the OS was copied to it. The 2020 35th Anniversary "Renovation" units added an SD-card floppy emulator to the original software design. The 2021 Rossum reissue replaces the floppy with SD and updates its own firmware from an SD file.

### Cited Findings
- The owner's manual (Craig Anderton, E-mu PN FI 332 Rev. E) has a section "5C Formatting Disks and Copying Software (DISK 9)" with separate format and copy-software procedures. Per the search summary, the manual describes a formatted disk loading software, then sequences, then sounds, in turn. — [SP-1200 Owner's Manual PDF (autistici.org mirror)](https://www.autistici.org/2000-maniax/texts/sp1200_manual.pdf) (search summary only; the host was unreachable for direct fetch)
- HxC/Gotek user (Mar 2020): when formatting, the SP-1200 asks "copy software? Y/N", and yes makes the disk bootable. "Only the first couple images provide the OS, while others are simply just blank disks to hold sounds and sequences." Images without the OS stall: "It begins to load and simply stops." — [HxC forum: SP1200 + Gotek Boot up quirks](https://hxc2001.com/floppy/forum/viewtopic.php?p=22343)
- A Gotek user with a **black reissue** unit (Oct 2017) found the OS would not load until the OS image was correctly named (DSKA0000.HFE). This shows the 1990s black units also boot from disk. — [HxC forum: SP1200 with Gotek HxC, page 2](https://hxc2001.com/floppy/forum/viewtopic.php?t=1750&start=15)
- "Any new disk that you format within the unit essentially becomes an SP1200 OS Disk." — [Gearspace: Archiving SP1200 Disks](https://gearspace.com/board/electronic-music-instruments-and-electronic-music-production/816011-archiving-sp1200-disks.html) (search snippet)
- Forum procedure: in Disk mode choose 9 (Format), answer No to formatting, and it proceeds to "copy software" mode. — [MPC-Forums: clean system disk for sp1200](https://www.mpc-forums.com/viewtopic.php?p=253746); [Tapatalk: HxC SD Card Reader and SP1200 Workflow Tips](https://www.tapatalk.com/groups/sp1200/hxc-sd-card-reader-and-sp1200-workflow-tips-t1645.html) (search snippets)
- **Rossum 2021 reissue:** firmware is updated by placing `sp1200fw.bin` in the SD card root, then Setup > 23 (Special Functions) > 28 (Update Software), then a power cycle. The recommended version is Main Firmware 1.04 (fixes a truncation click and an RGB backlight bug). Firmware 1.02+ adds a power-on hold of TUNE/MIX/MULTI to disable filter envelopes. — [Rossum documentation](https://shop.rossum-electro.com/pages/documentation) (via search summary; a direct fetch hit HTTP 429)
- The reissue replaces the 3.5" floppy with an SD slot, supports files and folders, and imports vintage SP-1200 disk images in .HFE format. It ships with a 3.5" disk of modified software for **original vintage** units that adds a Special function to transfer sounds and sequences over MIDI. — [MusicTech](https://musictech.com/news/e-mu-sp-1200-reissue-dave-rossum-electro-music-sp1200); [Gearnews](https://www.gearnews.com/rossum-sp-1200-authentic-reissue-of-the-iconic-sp-1200-sampling-drum-machine); [Wikipedia](https://en.wikipedia.org/wiki/E-mu_SP-1200)
- The 2020 35th Anniversary "Renovation" units had "a new 3.5″ disk drive and SD card floppy emulator integrated into the original SP-1200 software." — [Gearnews](https://www.gearnews.com/?p=111986) (via search summary)

### Inferences
- The disk Rossum ships with the reissue is itself evidence that original units load software from floppy. It is a modified OS for vintage machines, delivered on disk, so it boots on an original SP-1200 and adds a new function. Its existence implies the original OS is disk-resident.
- For the reissue, the firmware is stored internally and only updated from SD. The power-on behavior and the update procedure ending in a power cycle imply it boots from internal flash, not from the SD card. This is inferred; I did not see a Rossum statement of the storage chip.

### Gaps
- No E-mu-issued "OS update disk" release notes found. It is unknown whether E-mu shipped dated OS update disks to owners or simply new boot disks with units.
- I could not confirm whether the 1993 black units or the 1998 "Final Edition" changed the boot ROM or OS version.

## 3. What happens when a stock unit powers on with no disk inserted?

### Takeaway
According to user reports, it waits in a loading state and is unusable until a disk carrying the OS is inserted. I found no primary-manual quote describing the exact display.

### Cited Findings
- "Without a sp1200 disk of any kind you will not be able to use the machine as it will be in a perpetual state of a Loading disk state." — [Gearspace: SP1200 Help](https://gearspace.com/board/rap-hip-hop-engineering-and-production/950557-sp1200-help.html) (search snippet)
- A disk without the OS also fails to boot. The unit "begins to load and simply stops." — [HxC forum: SP1200 + Gotek Boot up quirks](https://hxc2001.com/floppy/forum/viewtopic.php?p=22343)
- Users with Gotek/HxC report the unit always loads the disk image at startup. — [HxC forum: SP1200 with Gotek HxC](https://torlus.com/floppy/forum/viewtopic.php?p=16535) (search snippet)

### Gaps
- The exact power-up text with no disk (e.g. "Insert Disk" or "Loading") is not confirmed from the manual.

## 4. What does the floppy store, and in what format?

### Takeaway
A 3.5" double-sided double-density (720 KB) disk, formatted by the unit itself, holds an optional OS copy plus the user's sounds and sequences. Sounds live in volatile RAM, so they and the sequences must be reloaded from disk.

### Cited Findings
- "The SP1200 uses Double Sided Double Density disks, (DS, DD) which holds 720 Kb of data", identified by a single hole. Using 1.44 MB HD disks reportedly risks data corruption, and some later units are said to read HD disks. — [Tapatalk SP1200 FAQ](https://www.tapatalk.com/groups/sp1200/sp1200-faq-t2.html) (search snippets; direct fetch 403)
- Samples are stored in volatile RAM and loaded from 3.5" disk. The drive is for "storing and loading sounds and sequences" and replaced the SP-12's Commodore 1541 interface. — [Wikipedia](https://en.wikipedia.org/wiki/E-mu_SP-1200)
- You must format a disk in the SP-1200 before use, and "once you format the disk it will also put a copy of the OS on the disk." — [Gearspace: SP1200 Help](https://gearspace.com/board/rap-hip-hop-engineering-and-production/950557-sp1200-help.html) (search snippet)
- The OS reportedly sits on the first 5–6 tracks of an SP disk. — [Tapatalk: Hacking the SP's OS](https://www.tapatalk.com/groups/sp1200/viewtopic.php?p=12441) (search snippet, unverified)
- Disks are archived as raw images with Kryoflux or HxC (HFE format). The reissue imports .HFE images. — [Gearspace: Archiving SP1200 Disks](https://gearspace.com/board/electronic-music-instruments-and-electronic-music-production/816011-archiving-sp1200-disks.html); [MusicTech](https://musictech.com/news/e-mu-sp-1200-reissue-dave-rossum-electro-music-sp1200)

### Inferences
- The disk uses a proprietary E-mu format, not a PC/FAT format: it must be formatted on the unit and is archived as raw flux/HFE images.

### Gaps
- No documented track/sector layout or sample file format was found.

## 5. Aftermarket mods that change where the OS lives

### Takeaway
Floppy emulators (Gotek running HxC firmware, HxC SD) are common. They do not move the OS into ROM. They simply serve an HFE disk image that contains the OS, so the unit still "boots from floppy," now virtual. I found no evidence of a community ROM-resident SP-1200 OS. The "v2.5"/"2.6" EPROM upgrades people mention are SP-12 parts.

### Cited Findings
- Gotek/HxC users boot from an HFE image containing the OS, named DSKA0000.HFE, and keep "the HFE image with the OS on it for all future projects." — [HxC forum: Boot up quirks](https://hxc2001.com/floppy/forum/viewtopic.php?p=22343); [HxC forum: SP1200 with Gotek HxC p.2](https://hxc2001.com/floppy/forum/viewtopic.php?t=1750&start=15)
- Reported emulator problems include read/verify errors on format-then-copy-software and "NoDataTransferred" on save. — [HxC forum: SP1200 with Gotek HxC p.2](https://hxc2001.com/floppy/forum/viewtopic.php?t=1750&start=15)
- The forum thread "Hacking the SP's Operating System… Is it possible" discusses modifying the disk-resident OS. — [Tapatalk](https://www.tapatalk.com/groups/sp1200/viewtopic.php?p=12441) (content not retrievable)
- Rossum's modified vintage-unit software (MIDI sound/sequence transfer) is distributed on floppy. — [MusicTech](https://musictech.com/news/e-mu-sp-1200-reissue-dave-rossum-electro-music-sp1200)

### Inferences
- Because the OS is disk-resident, OS modification on vintage units, whether Rossum's official MIDI-transfer disk or community hacks, is done by distributing modified disks or images, not EPROMs.

### Gaps
- Whether any shop sells a ROM-boot mod for the SP-1200: none found.
