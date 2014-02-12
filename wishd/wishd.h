// Wish daemon header


#ifndef _WISHD_H_
#define _WISHD_H_

#include <getopt.h>

#include "libwish.h"
#include "heartbeat.h"
#include "process.h"
#include "http.h"
#include "envar.h"
#include "barrier.h"

#define MIN(a,b) ((a) < (b) ? (a) : (b))

#define DEFAULT_CONFIG_PATH "/etc/wish/wishd.conf"

#define WISH_TMPDIR_ENV "WISH_TMPDIR"
#define WISH_DATADIR_ENV "WISH_DATADIR"

#endif