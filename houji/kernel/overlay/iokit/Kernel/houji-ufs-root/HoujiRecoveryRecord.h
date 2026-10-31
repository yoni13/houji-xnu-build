// SPDX-License-Identifier: BSD-2-Clause-Patent
// AOSP bootloader_message fields: command[32], status[32], recovery[768], stage[32].
// Preserve reserved bytes and the adjacent A/B control data without alteration.
static bool houjiRecoveryRecord(uint8_t *page,unsigned n) {
  if(!page || n!=4096)return false;
  for(unsigned i=0;i<864;i++)page[i]=0;
  static const char command[]="boot-recovery",args[]="recovery\n";
  for(unsigned i=0;i<sizeof(command);i++)page[i]=uint8_t(command[i]);
  for(unsigned i=0;i<sizeof(args);i++)page[64+i]=uint8_t(args[i]);
  return true;
}
