## Copyright (C) 1996-2025 The Squid Software Foundation and contributors
##
## Squid software is distributed under GPLv2+ license and includes
## contributions from numerous individuals and organizations.
## Please see the COPYING and CONTRIBUTORS files for details.
##

dnl these checks must be performed in the same order as here defined,
dnl and have mostly been lifted out of an inlined configure.ac.

AC_DEFUN([SQUID_CHECK_SOLARIS_KRB5],[
  # no pkg-config for solaris native Kerberos
  AS_IF([test "$cross_compiling" = "no" -a "x$with_mit_krb5" != "xyes" -a "x$with_mit_krb5" != "xno"],[
    AC_PATH_PROG(krb5_config,krb5-config,no,[$PATH:$with_mit_krb5/bin])
  ])
  AC_MSG_NOTICE([Use krb5-config to get CFLAGS and LIBS])
  LIBMIT_KRB5_CFLAGS="-I/usr/include/kerberosv5 `$ac_krb5_config --cflags krb5 2>/dev/null`"
  LIBMIT_KRB5_LIBS="`$ac_krb5_config --libs krb5 2>/dev/null`"
  # Solaris 10 Update 11 patches the krb5-config tool to produce stderr messages on stdout.
  SOLARIS_BROKEN_KRB5CONFIG_GSSAPI="`$ac_krb5_config --libs gssapi 2>/dev/null | grep "krb5-config"`"
  AS_IF([test "x$SOLARIS_BROKEN_KRB5CONFIG_GSSAPI" = "x"],[
    LIBMIT_KRB5_CFLAGS="$LIBMIT_KRB5_CFLAGS `$ac_krb5_config --cflags gssapi 2>/dev/null`"
    LIBMIT_KRB5_LIBS="$LIBMIT_KRB5_LIBS `$ac_krb5_config --libs gssapi 2>/dev/null`"
  ])
  CPPFLAGS="$LIBMIT_KRB5_CFLAGS $CPPFLAGS"
  LIBS="$LIBMIT_KRB5_LIBS $LIBS"
  missing_required=no
  AC_MSG_NOTICE([Try to find Kerberos libraries in given path])
  AC_CHECK_LIB(krb5, [main], [LIBMIT_KRB5_LIBS="$LIBMIT_KRB5_LIBS -lkrb5"],[
    AC_MSG_WARN([library 'krb5' is required for Solaris Kerberos])
    missing_required=yes
  ])
  AC_CHECK_LIB(gss, [main], [LIBMIT_KRB5_LIBS="$LIBMIT_KRB5_LIBS -lgss"],[
    AC_MSG_WARN([library 'gss' is required for Solaris Kerberos])
    missing_required=yes
  ])
  AS_IF([test "x$missing_required" = "xyes"],[LIBMIT_KRB5_LIBS=""],[
    LIBS="$LIBMIT_KRB5_LIBS $LIBS"
    AC_DEFINE(USE_SOLARIS_KRB5,1,[Solaris Kerberos support is available])
    SQUID_CHECK_KRB5_SOLARIS_BROKEN_KRB5_H
    AS_IF([test "x$squid_cv_broken_krb5_h" = "xyes"],[
      AC_DEFINE(HAVE_BROKEN_SOLARIS_KRB5_H, 1, [Define to 1 if Solaris krb5.h is broken for C++])
      AC_MSG_WARN([You have a broken Solaris <krb5.h> system include.])
      AC_MSG_WARN([Please see http://bugs.opensolaris.org/bugdatabase/view_bug.do?bug_id=6837512])
      AC_MSG_WARN([If you need Kerberos support you will have to patch])
      AC_MSG_WARN([your system. See contrib/solaris/solaris-krb5-include.patch])
    ])
    AC_CHECK_HEADERS(gssapi.h gssapi/gssapi.h gssapi/gssapi_krb5.h)
    AC_CHECK_HEADERS(gssapi/gssapi_ext.h gssapi/gssapi_generic.h)
    AC_CHECK_HEADERS(krb5.h com_err.h et/com_err.h)
    AC_CHECK_HEADERS(profile.h)
    SQUID_CHECK_KRB5_FUNCS
  ])
])

dnl checks for a broken solaris header file, and sets squid_cv_broken_krb5_h
dnl to yes if that's the case
AC_DEFUN([SQUID_CHECK_KRB5_SOLARIS_BROKEN_KRB5_H], [
  AC_CACHE_CHECK([for broken Solaris krb5.h],squid_cv_broken_krb5_h, [
    SQUID_STATE_SAVE(squid_krb5_solaris_test)
    CPPFLAGS="-I${srcdir:-.} $CPPFLAGS"
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <krb5.h>
int i;
]])], [ squid_cv_broken_krb5_h=no ], [
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#define HAVE_BROKEN_SOLARIS_KRB5_H  1
#include "compat/krb5.h"
int i;
]])], [ squid_cv_broken_krb5_h=yes ], [ squid_cv_broken_krb5_h=no ])
    ])
    SQUID_STATE_ROLLBACK(squid_krb5_solaris_test)
  ])
]) dnl SQUID_CHECK_KRB5_SOLARIS_BROKEN_KRB5_H


AC_DEFUN([SQUID_CHECK_KRB5_HEIMDAL_BROKEN_KRB5_H], [
  AC_CACHE_CHECK([for broken Heimdal krb5.h],squid_cv_broken_heimdal_krb5_h, [
    SQUID_STATE_SAVE(squid_krb5_heimdal_test)
    CPPFLAGS="-I${srcdir:-.} $CPPFLAGS"
    AC_LINK_IFELSE([AC_LANG_SOURCE([[
#include <krb5.h>
int
main(void)
{
        krb5_context context;
        krb5_init_context(&context);
        return 0;
}
]])], [ squid_cv_broken_heimdal_krb5_h=no ], [
    AC_LINK_IFELSE([AC_LANG_SOURCE([[
#define HAVE_BROKEN_HEIMDAL_KRB5_H  1
#include "compat/krb5.h"
int
main(void)
{
        krb5_context context;
        krb5_init_context(&context);
        return 0;
}
]])], [ squid_cv_broken_heimdal_krb5_h=yes ], [ squid_cv_broken_heimdal_krb5_h=no ])
    ])
    SQUID_STATE_ROLLBACK(squid_krb5_heimdal_test)
  ])
  SQUID_DEFINE_BOOL(HAVE_BROKEN_HEIMDAL_KRB5_H,$squid_cv_broken_heimdal_krb5_h,[Heimdal krb5.h is broken for C++])
]) dnl SQUID_CHECK_KRB5_HEIMDAL_BROKEN_KRB5_H

dnl check whether the kerberos context has a memory cache. Sets
dnl squid_cv_memory_cache if that's the case.
AC_DEFUN([SQUID_CHECK_KRB5_CONTEXT_MEMORY_CACHE],[
  AC_CACHE_CHECK([for memory cache], squid_cv_memory_cache, [
    SQUID_STATE_SAVE(squid_krb5_test)
    CPPFLAGS="-I${srcdir:-.} $CPPFLAGS"
    AC_RUN_IFELSE([
      AC_LANG_SOURCE([[
#include "compat/krb5.h"
int main(int argc, char *argv[])
{
    krb5_context context;
    krb5_ccache cc;

    krb5_init_context(&context);
    return krb5_cc_resolve(context, "MEMORY:test_cache", &cc);
}
      ]])
    ], [ squid_cv_memory_cache=yes ], [ squid_cv_memory_cache=no ], [:])
    SQUID_STATE_ROLLBACK(squid_krb5_test)
  ])
])

dnl check whether the kerberos context has a memory keytab. Sets
dnl squid_cv_memory_keytab if that's the case.
AC_DEFUN([SQUID_CHECK_KRB5_CONTEXT_MEMORY_KEYTAB],[
  AC_CACHE_CHECK([for memory keytab], squid_cv_memory_keytab, [
    SQUID_STATE_SAVE(squid_krb5_test)
    CPPFLAGS="-I${srcdir:-.} $CPPFLAGS"
    AC_RUN_IFELSE([
      AC_LANG_SOURCE([[
#include "compat/krb5.h"
int main(int argc, char *argv[])
{
    krb5_context context;
    krb5_keytab kt;

    krb5_init_context(&context);
    return krb5_kt_resolve(context, "MEMORY:test_keytab", &kt);
}
      ]])
    ], [ squid_cv_memory_keytab=yes ], [ squid_cv_memory_keytab=no ], [:])
    SQUID_STATE_ROLLBACK(squid_krb5_test)
  ])
])


dnl checks that gssapi is ok, and sets squid_cv_working_gssapi accordingly
AC_DEFUN([SQUID_CHECK_WORKING_GSSAPI], [
  AC_CACHE_CHECK([for working gssapi], squid_cv_working_gssapi, [
    AC_LINK_IFELSE([AC_LANG_SOURCE([[
#if HAVE_GSS_H
#include <gss.h>
#endif
#if USE_APPLE_KRB5
#define GSSKRB_APPLE_DEPRECATED(x)
#endif
#if HAVE_GSSAPI_GSSAPI_H
#include <gssapi/gssapi.h>
#elif HAVE_GSSAPI_H
#include <gssapi.h>
#endif
#if HAVE_GSSAPI_GSSAPI_KRB5_H
#include <gssapi/gssapi_krb5.h>
#endif
#if HAVE_GSSAPI_GSSAPI_GENERIC_H
#include <gssapi/gssapi_generic.h>
#endif
int
main(void)
{
        OM_uint32 val;
        gss_OID_set set;

        gss_create_empty_oid_set(&val, &set);

        return 0;
}
  ]])],  [ squid_cv_working_gssapi=yes ], [ squid_cv_working_gssapi=no ], [:])])
  AS_IF([test "x$squid_cv_working_gssapi" = "xno" -a `echo $LIBS | grep -i -c "(-)L"` -gt 0],[
    AC_MSG_NOTICE([Check Runtime library path !])
  ])
])

dnl check for a working spnego, and set squid_cv_have_spnego
AC_DEFUN([SQUID_CHECK_SPNEGO_SUPPORT], [
  AC_CACHE_CHECK([for spnego support], squid_cv_have_spnego, [
    AC_RUN_IFELSE([AC_LANG_SOURCE([[
#if HAVE_GSS_H
#include <gss.h>
#endif
#if USE_APPLE_KRB5
#define GSSKRB_APPLE_DEPRECATED(x)
#endif
#if HAVE_GSSAPI_GSSAPI_H
#include <gssapi/gssapi.h>
#elif HAVE_GSSAPI_H
#include <gssapi.h>
#endif
#if HAVE_GSSAPI_GSSAPI_KRB5_H
#include <gssapi/gssapi_krb5.h>
#endif
#if HAVE_GSSAPI_GSSAPI_GENERIC_H
#include <gssapi/gssapi_generic.h>
#endif
#include <string.h>
int main(int argc, char *argv[]) {
 OM_uint32 major_status,minor_status;
 gss_OID_set gss_mech_set;
 int i;

static gss_OID_desc _gss_mech_spnego  = {6, (void *)"\x2b\x06\x01\x05\x05\x02"};
gss_OID gss_mech_spnego = &_gss_mech_spnego;

 major_status = gss_indicate_mechs( &minor_status, &gss_mech_set);

 for (i=0;i<gss_mech_set->count;i++) {
     if (!memcmp(gss_mech_set->elements[i].elements,gss_mech_spnego->elements,gss_mech_set->elements[i].length)) {
        return 0;
     }
 }

 return 1;
}
  ]])],
  [ squid_cv_have_spnego=yes ], [ squid_cv_have_spnego=no ],[:])])
])

dnl checks that krb5 is functional. Sets squid_cv_working_krb5
AC_DEFUN([SQUID_CHECK_WORKING_KRB5],[
  AC_CACHE_CHECK([for working krb5], squid_cv_working_krb5, [
    SQUID_STATE_SAVE(squid_krb5_test)
    CPPFLAGS="-I${srcdir:-.} $CPPFLAGS"
    AC_LINK_IFELSE([AC_LANG_SOURCE([[
#include "compat/krb5.h"
int
main(void)
{
        krb5_context context;

        krb5_init_context(&context);

        return 0;
}
  ]])], [ squid_cv_working_krb5=yes ], [ squid_cv_working_krb5=no ],[:])])
  SQUID_STATE_ROLLBACK(squid_krb5_test)
  AS_IF([test "x$squid_cv_working_krb5" = "xno" -a `echo $LIBS | grep -i -c "(-)L"` -gt 0],[
    AC_MSG_NOTICE([Check Runtime library path !])
  ])
])


dnl checks for existence of krb5 functions
AC_DEFUN([SQUID_CHECK_KRB5_FUNCS],[

  ac_com_error_message=no
  AS_IF([test "x$ac_cv_header_com_err_h" = "xyes"],[
    AC_EGREP_HEADER(error_message,com_err.h,ac_com_error_message=yes)],
  [test "x$ac_cv_header_et_com_err_h" = "xyes"],[
    AC_EGREP_HEADER(error_message,et/com_err.h,ac_com_error_message=yes)
  ])

  AS_IF([test `echo "$LIBMIT_KRB5_LIBS $LIBHEIMDAL_KRB5_LIBS" | grep -c com_err` -ne 0 -a "x$ac_com_error_message" = "xyes"],[
    AC_CHECK_LIB(com_err,error_message,
      AC_DEFINE(HAVE_ERROR_MESSAGE,1,[Define to 1 if you have error_message]),)
  ],[test  "x$ac_com_error_message" = "xyes"],[
    AC_CHECK_LIB(krb5,error_message,
      AC_DEFINE(HAVE_ERROR_MESSAGE,1,[Define to 1 if you have error_message]),)
  ])

  AC_CHECK_LIB(krb5,krb5_get_err_text,
    AC_DEFINE(HAVE_KRB5_GET_ERR_TEXT,1,
      [Define to 1 if you have krb5_get_err_text]),)
  AC_CHECK_LIB(krb5,krb5_get_error_message,
    AC_DEFINE(HAVE_KRB5_GET_ERROR_MESSAGE,1,
      [Define to 1 if you have krb5_get_error_message]),)
  AC_CHECK_LIB(krb5,krb5_free_error_message,
    AC_DEFINE(HAVE_KRB5_FREE_ERROR_MESSAGE,1,
      [Define to 1 if you have krb5_free_error_message]),)
  AC_CHECK_LIB(krb5,krb5_free_error_string,
    AC_DEFINE(HAVE_KRB5_FREE_ERROR_STRING,1,
      [Define to 1 if you have krb5_free_error_string]),)
  AC_CHECK_DECLS(krb5_kt_free_entry,,,[#include <krb5.h>])
  AC_CHECK_TYPE(krb5_pac,
    AC_DEFINE(HAVE_KRB5_PAC,1,
      [Define to 1 if you have krb5_pac]),,
      [#include <krb5.h>])
  AC_CHECK_LIB(krb5,krb5_kt_free_entry,
    AC_DEFINE(HAVE_KRB5_KT_FREE_ENTRY,1,
      [Define to 1 if you have krb5_kt_free_entry]),)
  AC_CHECK_LIB(krb5,krb5_get_init_creds_keytab,
    AC_DEFINE(HAVE_GET_INIT_CREDS_KEYTAB,1,
      [Define to 1 if you have krb5_get_init_creds_keytab]),)
  AC_CHECK_LIB(krb5,krb5_get_profile,
    AC_DEFINE(HAVE_KRB5_GET_PROFILE,1,
      [Define to 1 if you have krb5_get_profile]),)
  AC_CHECK_LIB(krb5,profile_get_integer,
    AC_DEFINE(HAVE_PROFILE_GET_INTEGER,1,
      [Define to 1 if you have profile_get_integer]),)
  AC_CHECK_LIB(krb5,profile_release,
    AC_DEFINE(HAVE_PROFILE_RELEASE,1,
      [Define to 1 if you have profile_release]),)
  AC_CHECK_LIB(krb5,krb5_get_renewed_creds,
    AC_DEFINE(HAVE_KRB5_GET_RENEWED_CREDS,1,
      [Define to 1 if you have krb5_get_renewed_creds]),)
  AC_CHECK_LIB(krb5,krb5_principal_get_realm,
    AC_DEFINE(HAVE_KRB5_PRINCIPAL_GET_REALM,1,
      [Define to 1 if you have krb5_principal_get_realm]),)
  AC_CHECK_LIB(krb5, krb5_get_init_creds_opt_alloc,
    AC_DEFINE(HAVE_KRB5_GET_INIT_CREDS_OPT_ALLOC,1,
      [Define to 1 if you have krb5_get_init_creds_opt_alloc]),)
  AC_MSG_CHECKING([for krb5_get_init_creds_free requires krb5_context])
  SQUID_STATE_SAVE(squid_krb5_test)
  CPPFLAGS="-I${srcdir:-.} $CPPFLAGS"
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include "compat/krb5.h"
    ]],[[krb5_context context;
	 krb5_get_init_creds_opt *options;
	 krb5_get_init_creds_opt_free(context, options)]])],[
	AC_DEFINE(HAVE_KRB5_GET_INIT_CREDS_FREE_CONTEXT,1,
		  [Define to 1 if you krb5_get_init_creds_free requires krb5_context])
	AC_MSG_RESULT(yes)
    ],[AC_MSG_RESULT(no)],[AC_MSG_RESULT(no)])
  SQUID_STATE_ROLLBACK(squid_krb5_test)

  AC_CHECK_FUNCS(gss_map_name_to_any,
    AC_DEFINE(HAVE_GSS_MAP_ANY_TO_ANY,1,
      [Define to 1 if you have gss_map_name_to_any]),)
  AC_CHECK_FUNCS(gsskrb5_extract_authz_data_from_sec_context,
    AC_DEFINE(HAVE_GSSKRB5_EXTRACT_AUTHZ_DATA_FROM_SEC_CONTEXT,1,
      [Define to 1 if you have gsskrb5_extract_authz_data_from_sec_context]),)

  SQUID_CHECK_KRB5_CONTEXT_MEMORY_CACHE
  SQUID_DEFINE_BOOL(HAVE_KRB5_MEMORY_CACHE,$squid_cv_memory_cache,
       [Define if kerberos has MEMORY: cache support])

  SQUID_CHECK_KRB5_CONTEXT_MEMORY_KEYTAB
  SQUID_DEFINE_BOOL(HAVE_KRB5_MEMORY_KEYTAB,$squid_cv_memory_keytab,
       [Define if kerberos has MEMORY: keytab support])

  SQUID_CHECK_WORKING_GSSAPI
  SQUID_DEFINE_BOOL(HAVE_GSSAPI,$squid_cv_working_gssapi,[GSSAPI support])

  SQUID_CHECK_SPNEGO_SUPPORT
  SQUID_DEFINE_BOOL(HAVE_SPNEGO,$squid_cv_have_spnego,[SPNEGO support])

  SQUID_CHECK_WORKING_KRB5
  SQUID_DEFINE_BOOL(HAVE_KRB5,$squid_cv_working_krb5,[KRB5 support])
])

