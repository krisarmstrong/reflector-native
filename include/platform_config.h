/*
 * platform_config.h - Platform configuration detection
 *
 * Copyright (c) 2025 Kris Armstrong
 */

#ifndef PLATFORM_CONFIG_H
#define PLATFORM_CONFIG_H

/* Check for AF_XDP support at compile time */
#ifdef __linux__
#ifdef __has_include
#if __has_include(<xdp/xsk.h>)
#define HAVE_AF_XDP 1
#elif __has_include(<bpf/xsk.h>)
#define HAVE_AF_XDP 1
#else
#define HAVE_AF_XDP 0
#endif
#else
/* Assume XDP available if we can't check */
#define HAVE_AF_XDP 1
#endif
#else
#define HAVE_AF_XDP 0
#endif

#endif /* PLATFORM_CONFIG_H */
