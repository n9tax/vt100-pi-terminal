// Start-at-boot control: install/enable (or disable) the systemd unit that runs
// this program on tty1 at boot. Requires running as root (the app already does).
#ifndef SERVICE_H
#define SERVICE_H

int  service_boot_enabled(void);   // 1 if the boot service is enabled
int  service_set_boot(int on);     // install+enable / disable+restore getty; 0 = ok

#endif // SERVICE_H
