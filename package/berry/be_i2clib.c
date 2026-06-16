// SPDX-License-Identifier: MIT
/*
 * Native I2C module for Berry on Fruit Jam Linux.
 *
 *   import i2c
 *   i2c.write(0x44, bytes("FD"))   # write command bytes to an address
 *   i2c.delay(10)                  # milliseconds (measurement wait)
 *   var r = i2c.read(0x44, 6)      # read N bytes -> bytes object
 *
 * Bus defaults to /dev/i2c-0; pass an optional trailing int to pick another.
 */
#define _DEFAULT_SOURCE 1   /* expose usleep() under -std=c99 */
#include "berry.h"

#if BE_USE_I2C_MODULE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#ifndef I2C_RDWR
#define I2C_RDWR 0x0707
#endif
#ifndef I2C_M_RD
#define I2C_M_RD 0x0001
#endif

struct fj_i2c_msg {
    unsigned short addr;
    unsigned short flags;
    unsigned short len;
    unsigned char *buf;
};

struct fj_i2c_rdwr {
    struct fj_i2c_msg *msgs;
    unsigned int nmsgs;
};

static int fj_open_bus(bvm *vm, int bus_arg)
{
    int bus = 0;
    int fd;
    char path[24];

    if (be_top(vm) >= bus_arg && be_isint(vm, bus_arg))
        bus = be_toint(vm, bus_arg);
    snprintf(path, sizeof(path), "/dev/i2c-%d", bus);
    fd = open(path, O_RDWR);
    if (fd < 0)
        be_raise(vm, "i2c_error", "cannot open i2c bus");
    return fd;
}

static int m_write(bvm *vm)
{
    int addr, fd, rc;
    size_t len;
    const void *buf;
    struct fj_i2c_msg msg;
    struct fj_i2c_rdwr data;

    if (be_top(vm) < 2 || !be_isint(vm, 1) || !be_isbytes(vm, 2))
        be_raise(vm, "type_error", "i2c.write(addr:int, data:bytes [,bus:int])");
    addr = be_toint(vm, 1);
    buf = be_tobytes(vm, 2, &len);
    fd = fj_open_bus(vm, 3);

    msg.addr = (unsigned short)addr;
    msg.flags = 0;
    msg.len = (unsigned short)len;
    msg.buf = (unsigned char *)buf;
    data.msgs = &msg;
    data.nmsgs = 1;
    rc = ioctl(fd, I2C_RDWR, &data);
    close(fd);

    if (rc != 1)
        be_raise(vm, "i2c_error", "i2c write failed (no ACK?)");
    be_return_nil(vm);
}

static int m_read(bvm *vm)
{
    int addr, n, fd, rc;
    unsigned char tmp[128];
    struct fj_i2c_msg msg;
    struct fj_i2c_rdwr data;

    if (be_top(vm) < 2 || !be_isint(vm, 1) || !be_isint(vm, 2))
        be_raise(vm, "type_error", "i2c.read(addr:int, n:int [,bus:int])");
    addr = be_toint(vm, 1);
    n = be_toint(vm, 2);
    if (n < 1 || n > (int)sizeof(tmp))
        be_raise(vm, "value_error", "i2c.read length out of range (1..128)");
    fd = fj_open_bus(vm, 3);

    msg.addr = (unsigned short)addr;
    msg.flags = I2C_M_RD;
    msg.len = (unsigned short)n;
    msg.buf = tmp;
    data.msgs = &msg;
    data.nmsgs = 1;
    rc = ioctl(fd, I2C_RDWR, &data);
    close(fd);

    if (rc != 1)
        be_raise(vm, "i2c_error", "i2c read failed (no ACK?)");
    be_pushbytes(vm, tmp, (size_t)n);
    be_return(vm);
}

static int m_delay(bvm *vm)
{
    int ms = 0;

    if (be_top(vm) >= 1 && be_isint(vm, 1))
        ms = be_toint(vm, 1);
    if (ms > 0)
        usleep((useconds_t)ms * 1000u);
    be_return_nil(vm);
}

#if !BE_USE_PRECOMPILED_OBJECT
be_native_module_attr_table(i2c) {
    be_native_module_function("write", m_write),
    be_native_module_function("read", m_read),
    be_native_module_function("delay", m_delay)
};
be_define_native_module(i2c, NULL);
#else
/* @const_object_info_begin
module i2c (scope: global, depend: BE_USE_I2C_MODULE) {
    write, func(m_write)
    read, func(m_read)
    delay, func(m_delay)
}
@const_object_info_end */
#include "../generate/be_fixed_i2c.h"
#endif

#endif /* BE_USE_I2C_MODULE */
