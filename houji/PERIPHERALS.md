# houji peripherals: state and next steps

Device facts come from the handset's own devicetree, the SteamOS houji port and mainline Linux; code borrowed from UniXNU-sheng
(SM8550, same PMICs and Wi-Fi chip) is credited in each file.

## Battery and charging — working
`iokit/Kernel/houji-adsp/`: the stock ADSP firmware (read in place from `modem_a`) runs the battery manager; PMIC GLINK feeds an
`IOPMPowerSource`. On a USB-PD supply the host ends Xiaomi's adapter verification (board property 20 = 0), which lifts the 100 mA input
limit; the battery current vote is `houji_fcc_ma` (default 1000, 500 mA outside 10–40 °C, 0 = leave the firmware alone).

## Wi-Fi — working (WCN7850 on PCIe0)
`iokit/Kernel/houji-wifi/`, module built by `tools/build-ath12k.py` from UniXNU/Ath12kWifi plus `kernel/ath12k-module/*.patch`.
On by default (`houji_wifi=0` off). Verified: scan, WPA2 join on 5 GHz (80 MHz) and 2.4 GHz from Settings, DHCP, default route, DNS,
Safari loading a page by name.
- Firmware: stock Kiwi v2 files from `modem_a:/image/kiwi` (amss20, phy_ucode20, bd_n3gl, regdb_xiaomi); MAC from the chip ID.
- DMA: a 48 MiB buffer behind a stage 1 SMMU table on stream 0x1401 when no memory below 4 GiB is free.
- `/arm-io/wlan` is added so launchd starts wifid.
- Root image: `WiFiManager/LinkUpEventAfter4way` must be off (`unixnu-root/add-wifi-flags.py`); with it wifid holds the link-up
  waiting for a supplicant event this driver does not send.
- Packets are tapped into BPF in both directions; without it IPConfiguration's router ARP check never succeeds and Wi-Fi is never
  made primary (no default route, no DNS).
- Known gaps: the network password is not kept across reboots (keychain); the firmware dozes unless power save is turned off (done
  after each join); a heartbeat HTT stats request crashed this firmware (removed).
- USB console: `wifi scan | join SSID PASS | leave | status`; `type TEXT` (`\n` = Return) types through a virtual keyboard.

## Buttons — implemented, not yet pressed on hardware
`iokit/Kernel/houji-buttons/HoujiButtons.cpp`: power (PMK8550 PON pwrkey), volume down (resin), volume up (PM8550 GPIO 6) read
every 20 ms through the SPMI observer (read only) and sent to SweetHID. Verified: both peripherals found, no read errors, idle state
reads released. `houji_buttons=0` turns it off.

## GPS — started
GNSS runs in the modem (MPSS). Done: the stock modem image is read from `modemfirmware_a` and placed against the devicetree
carveouts without starting anything (`HOUJI MODEM: stage=3`, 162 MB, 36 segments, relocatable, span = the 0xf400000 mpss region).
The modem runs (`houji_modem=2`, opt-in; `HoujiAdsp.cpp` "the modem (MPSS)" and "GNSS"):
- start: SMP2P 435/428 and the MPSS doorbells, AOSS `load_state modem`, CX/MSS votes and a CE0 bus vote, SCM MP assign of the DSM and
  qlink regions to MSS_MSA and of the rmtfs buffer (0xd7c00000) to HLOS + MSA, the dtb (PAS 0x26) then the modem (PAS 4). Verified:
  ready about 1.05 s after the start, GLINK and QRTR up, 52 services announced (LOC 0x10 among them).
- The PAS metadata must lie below 4 GiB (Xiaomi's driver: dma_phys_below_32b) in normal non-cacheable memory and must not be in an SHM
  bridge of our own. This kernel's RAM is above 4 GiB, so the metadata goes to fixed pages at 0xd7a00000 / 0xd7a10000 (unused RAM
  outside the UEFI map). Above 4 GiB the secure world answered 0xffcfffbb; with our own bridge 0xffcffff6.
- rmtfs (QMI service 14): `/boot/modem_fs1`, `fs2`, `fsg`, `fsc` served from RAM copies of modemst1/2, fsg, fsc; iovec buffer
  fields are physical addresses. Verified: 4 MiB read, 8 MiB written (kept in RAM, never written to UFS); no modem crash.
- LOC client: register (stock fields HAL/1/0), 0x3b (the engine answers lock ALL), 0xbf, 0x36, SET_ENGINE_LOCK none (0x3a), START
  (0x22) — all accepted. No position or satellite reports follow yet.
Next: the QRTR TFTP file service for the modem's MCFG requests before MPSS starts (SteamOS: needed before satellites were reported),
then time injection once the iOS clock is network-synced, then the iOS side.
Remaining steps once the modem starts (the SteamOS port shows each is required):
1. rmtfs: a QMI server (QRTR) giving the modem its EFS partitions `modemst1`, `modemst2`, `fsg`, `fsc`; writes kept in RAM first.
2. A QRTR file server for the modem's MCFG requests (SteamOS: `tqftpserv-mbnconfig.patch`).
3. Start MPSS: PAS 4 (+ dtb), the DSM regions 0x86b00000 + 0x4900000 and 0x8b400000 + 0x800000 (not in the UEFI memory map, so not
   OS RAM), CX/MX votes, AOSS `modem` load state, SMP2P stop state, GLINK edge remote pid 1 (IPCC client 2), QRTR on `IPCRTR`.
   SteamOS saw a phone reset when MPSS was restarted within one boot: start it once per boot.
4. QMI LOC client (SteamOS `gps/houji-loc-test.c`: hold one QRTR client for the session, XTRA/UTC assistance).
5. iOS: feed positions to locationd (no Qualcomm GNSS path exists in iOS).

## Cameras — not started
No reference implementation: SteamOS lists cameras as not implemented, UniXNU-sheng has none, mainline has only camcc and CCI for
SM8650 (no ISP driver). First step: power the Titan top GDSC and CCI, then read each sensor's chip ID over CCI with the power, reset
and MCLK sequence from the stock devicetree overlay.
