## Copyright (C) 1996-2025 The Squid Software Foundation and contributors
##
## Squid software is distributed under GPLv2+ license and includes
## contributions from numerous individuals and organizations.
## Please see the COPYING and CONTRIBUTORS files for details.
##

include $(top_srcdir)/src/Common.am

libexec_PROGRAMS = ext_file_userip_acl
man_MANS = ext_file_userip_acl.8

ext_file_userip_acl_SOURCES = \
	ext_file_userip_acl.cc
ext_file_userip_acl_LDADD= \
	$(top_builddir)/lib/libmiscencoding.la \
	$(COMPAT_LIB) \
	$(XTRA_LIBS)

EXTRA_DIST = \
	example.conf \
	ext_file_userip_acl.8 \
	required.m4
