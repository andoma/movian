#ifndef IPC_H__
#define IPC_H__

void ipc_init(void);

void mpkeys_grab(void);

void dbus_start(void);

void lirc_start(void);

void serdev_start(void);

void stdin_start(void);

#endif /* IPC_H__ */
